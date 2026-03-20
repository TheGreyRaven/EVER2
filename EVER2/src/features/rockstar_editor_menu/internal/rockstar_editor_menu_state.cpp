#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

namespace {

using RtlLookupFunctionEntryFn = PRUNTIME_FUNCTION(NTAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);

}

std::shared_ptr<PLH::x64Detour> g_prepare_clip_detour;
std::shared_ptr<PLH::x64Detour> g_move_clip_detour;
std::shared_ptr<PLH::x64Detour> g_save_project_detour;
std::shared_ptr<PLH::x64Detour> g_project_start_load_detour;

PrepareStagingClipByIndexFn g_prepare_clip_original = nullptr;
PrepareStagingClipByNameFn g_prepare_clip_by_name = nullptr;
MoveStagingClipToProjectFn g_move_clip_original = nullptr;
SaveProjectFn g_save_project_original = nullptr;
StartLoadProjectFn g_start_load_project = nullptr;
ProjectStartLoadFn g_project_start_load_original = nullptr;

std::atomic<void*> g_last_project_ptr{nullptr};
std::atomic<uint64_t> g_hook_hits{0};
std::atomic<uint32_t> g_install_attempts{0};
std::atomic<ULONGLONG> g_last_install_attempt_tick{0};
std::mutex g_install_mutex;
std::mutex g_project_path_mutex;
std::string g_last_project_path;
bool g_last_save_spinner_preference = true;

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

}
