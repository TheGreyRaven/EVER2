#include "ever/features/editor_projects/editor_projects_bridge.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/hooking/x64_detour.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ever::features::editor_projects {

namespace {

using PrepareStagingClipByIndexFn = bool(__fastcall*)(void* this_ptr, int source_index);
using MoveStagingClipToProjectFn = bool(__fastcall*)(void* this_ptr, int destination_index);
using SaveProjectFn = bool(__fastcall*)(void* this_ptr);
using RtlLookupFunctionEntryFn = PRUNTIME_FUNCTION(NTAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);

std::shared_ptr<PLH::x64Detour> g_prepare_clip_detour;
std::shared_ptr<PLH::x64Detour> g_move_clip_detour;
std::shared_ptr<PLH::x64Detour> g_save_project_detour;
PrepareStagingClipByIndexFn g_prepare_clip_original = nullptr;
MoveStagingClipToProjectFn g_move_clip_original = nullptr;
SaveProjectFn g_save_project_original = nullptr;

std::atomic<void*> g_last_project_ptr{nullptr};
std::atomic<uint64_t> g_hook_hits{0};
std::atomic<uint32_t> g_install_attempts{0};
std::atomic<ULONGLONG> g_last_install_attempt_tick{0};
std::mutex g_install_mutex;

uint64_t ResolveFunctionStartFromUnwind(uint64_t hit_address, uint64_t module_base, uint64_t module_size) {
    if (hit_address == 0 || module_base == 0 || module_size == 0) {
        return 0;
    }

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return 0;
    }

    const auto rtl_lookup = reinterpret_cast<RtlLookupFunctionEntryFn>(
        GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
    if (rtl_lookup == nullptr) {
        return 0;
    }

    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION runtime_function = rtl_lookup(static_cast<DWORD64>(hit_address), &image_base, nullptr);
    if (runtime_function == nullptr || image_base == 0) {
        return 0;
    }

    const uint64_t function_start = static_cast<uint64_t>(image_base) + runtime_function->BeginAddress;
    if (function_start < module_base || function_start >= (module_base + module_size)) {
        return 0;
    }

    return function_start;
}

uint64_t ResolvePatternToFunctionStart(
    ever::hooking::PatternScanner& scanner,
    ever::hooking::GameFunctionPatternId pattern_id,
    const wchar_t* log_prefix) {
    const char* const* candidates = ever::hooking::GetGameFunctionPatternCandidates(pattern_id);
    if (candidates == nullptr) {
        const std::wstring message = std::wstring(log_prefix) + L": no registered pattern candidates.";
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    uint64_t hit_address = 0;
    int matched_candidate = -1;
    for (int i = 0; candidates[i] != nullptr; ++i) {
        const std::string key = "EditorProjectHookCandidate_" + std::to_string(static_cast<int>(pattern_id)) + "_" + std::to_string(i);
        uint64_t candidate_hit = 0;
        scanner.AddPattern(key, candidates[i], &candidate_hit);
        scanner.PerformScan();
        if (candidate_hit != 0) {
            hit_address = candidate_hit;
            matched_candidate = i;
            break;
        }
    }

    if (hit_address == 0) {
        const std::wstring message =
            std::wstring(log_prefix) + L": no candidate matched. attempts=" +
            std::to_wstring(g_install_attempts.load(std::memory_order_relaxed));
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    const uint64_t function_start = ResolveFunctionStartFromUnwind(
        hit_address,
        scanner.GetModuleBase(),
        scanner.GetModuleSize());
    if (function_start == 0) {
        const std::wstring message =
            std::wstring(log_prefix) + L": matched candidateIndex=" + std::to_wstring(matched_candidate) +
            L" hit=" + std::to_wstring(hit_address) +
            L" but unwind resolution failed.";
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    const std::wstring message =
        std::wstring(log_prefix) + L": resolved candidateIndex=" + std::to_wstring(matched_candidate) +
        L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());

    return function_start;
}

void CaptureProjectContext(void* this_ptr, const wchar_t* hook_name) {
    if (this_ptr == nullptr) {
        return;
    }

    g_last_project_ptr.store(this_ptr, std::memory_order_release);
    const uint64_t hit_count = g_hook_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit_count == 1 || (hit_count % 64) == 0) {
        const std::wstring message =
            L"[EVER2] Editor project context updated by " + std::wstring(hook_name) +
            L". projectPtr=" + std::to_wstring(reinterpret_cast<uintptr_t>(this_ptr)) +
            L" hits=" + std::to_wstring(hit_count);
        ever::platform::LogDebug(message.c_str());
    }
}

bool __fastcall HookedPrepareStagingClipByIndex(void* this_ptr, int source_index) {
    CaptureProjectContext(this_ptr, L"PrepareStagingClip(sourceIndex)");
    if (g_prepare_clip_original == nullptr) {
        return false;
    }
    return g_prepare_clip_original(this_ptr, source_index);
}

bool __fastcall HookedMoveStagingClipToProject(void* this_ptr, int destination_index) {
    CaptureProjectContext(this_ptr, L"MoveStagingClipToProject");
    if (g_move_clip_original == nullptr) {
        return false;
    }
    return g_move_clip_original(this_ptr, destination_index);
}

bool __fastcall HookedSaveProject(void* this_ptr) {
    CaptureProjectContext(this_ptr, L"SaveProject");
    if (g_save_project_original == nullptr) {
        return false;
    }
    return g_save_project_original(this_ptr);
}

void InstallPrepareHookNoThrow() {
    if (g_prepare_clip_detour != nullptr && g_prepare_clip_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectPrepareStagingClipByIndex,
        L"[EVER2] Editor project prepare clip hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedPrepareStagingClipByIndex),
        &g_prepare_clip_original,
        g_prepare_clip_detour);
    if (FAILED(hr) || g_prepare_clip_detour == nullptr || g_prepare_clip_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Editor project prepare clip hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Editor project prepare clip hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

void InstallMoveHookNoThrow() {
    if (g_move_clip_detour != nullptr && g_move_clip_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectMoveStagingClipToProject,
        L"[EVER2] Editor project move clip hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedMoveStagingClipToProject),
        &g_move_clip_original,
        g_move_clip_detour);
    if (FAILED(hr) || g_move_clip_detour == nullptr || g_move_clip_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Editor project move clip hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Editor project move clip hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

void InstallSaveHookNoThrow() {
    if (g_save_project_detour != nullptr && g_save_project_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectSaveProject,
        L"[EVER2] Editor project save hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedSaveProject),
        &g_save_project_original,
        g_save_project_detour);
    if (FAILED(hr) || g_save_project_detour == nullptr || g_save_project_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Editor project save hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Editor project save hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

} // namespace

void EnsureHooksInstalled() {
    if (g_prepare_clip_detour != nullptr && g_move_clip_detour != nullptr && g_save_project_detour != nullptr) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last_attempt = g_last_install_attempt_tick.load(std::memory_order_acquire);
    if (last_attempt != 0 && (now - last_attempt) < 1500) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_install_mutex);

    const ULONGLONG lock_now = GetTickCount64();
    const ULONGLONG lock_last_attempt = g_last_install_attempt_tick.load(std::memory_order_acquire);
    if (lock_last_attempt != 0 && (lock_now - lock_last_attempt) < 1500) {
        return;
    }

    g_last_install_attempt_tick.store(lock_now, std::memory_order_release);
    const uint32_t attempt = g_install_attempts.fetch_add(1, std::memory_order_relaxed) + 1;

    const std::wstring message =
        L"[EVER2] Editor project hooks install attempt=" + std::to_wstring(attempt);
    ever::platform::LogDebug(message.c_str());

    InstallPrepareHookNoThrow();
    InstallMoveHookNoThrow();
    InstallSaveHookNoThrow();
}

bool HasProjectContext() {
    return g_last_project_ptr.load(std::memory_order_acquire) != nullptr;
}

bool AddClipToCurrentProject(int source_index, int destination_index, std::wstring& out_error) {
    out_error.clear();
    EnsureHooksInstalled();

    if (g_prepare_clip_original == nullptr || g_move_clip_original == nullptr) {
        out_error =
            L"Native add-clip hooks are not available yet. Open montage editing once, then retry.";
        return false;
    }

    if (source_index < 0 || destination_index < 0) {
        out_error = L"sourceIndex and destinationIndex must be non-negative.";
        return false;
    }

    void* project_ptr = g_last_project_ptr.load(std::memory_order_acquire);
    if (project_ptr == nullptr) {
        out_error =
            L"No active CVideoEditorProject context captured yet. Open montage editing once, then retry.";
        return false;
    }

    const bool prepared = g_prepare_clip_original(project_ptr, source_index);
    if (!prepared) {
        out_error =
            L"PrepareStagingClip(sourceIndex) returned false. Source clip may be invalid for this project.";
        return false;
    }

    const bool move_result = g_move_clip_original(project_ptr, destination_index);

    const std::wstring message =
        L"[EVER2] AddClip command executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr)) +
        L" sourceIndex=" + std::to_wstring(source_index) +
        L" destinationIndex=" + std::to_wstring(destination_index) +
        L" moveResult=" + std::to_wstring(move_result ? 1 : 0);
    ever::platform::LogDebug(message.c_str());

    return true;
}

bool SaveCurrentProject(std::wstring& out_error) {
    out_error.clear();
    EnsureHooksInstalled();

    if (g_save_project_original == nullptr) {
        out_error =
            L"Native SaveProject hook is not available yet. Open montage editing once, then retry.";
        return false;
    }

    void* project_ptr = g_last_project_ptr.load(std::memory_order_acquire);
    if (project_ptr == nullptr) {
        out_error =
            L"No active CVideoEditorProject context captured yet. Open montage editing once, then retry.";
        return false;
    }

    const bool result = g_save_project_original(project_ptr);
    if (!result) {
        out_error = L"SaveProject returned false.";
        return false;
    }

    const std::wstring message =
        L"[EVER2] SaveProject command executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr));
    ever::platform::LogDebug(message.c_str());

    return true;
}

} // namespace ever::features::editor_projects
