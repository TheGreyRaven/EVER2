#include "ever/features/rockstar_editor_menu/rockstar_editor_menu_bridge.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/features/rockstar_editor_menu/montage_load/montage_load_hook.h"
#include "ever/features/rockstar_editor_menu/move_staging_clip_to_project/resolve_move_staging_clip_to_project.h"
#include "ever/features/rockstar_editor_menu/prepare_staging_clip_by_name/prepare_staging_clip_by_name_resolver.h"
#include "ever/features/rockstar_editor_menu/project_start_load/project_start_load_hook.h"
#include "ever/features/rockstar_editor_menu/save_project/resolve_save_project.h"
#include "ever/features/rockstar_editor_menu/start_load_project/start_load_project_resolver.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

namespace ever::features::rockstar_editor_menu {

void EnsureHooksInstalled() {
    if (g_montage_load_detour != nullptr && g_prepare_clip_by_name != nullptr &&
        g_move_clip_original != nullptr && g_save_project_original != nullptr &&
        g_project_start_load_detour != nullptr && g_start_load_project != nullptr) {
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

    InstallMontageLoadHookNoThrow();
    ResolvePrepareStagingClipByNameNoThrow();
    ResolveMoveStagingClipToProjectNoThrow();
    ResolveSaveProjectNoThrow();
    InstallProjectStartLoadHookNoThrow();
    ResolveStartLoadProjectNoThrow();
}

bool HasProjectContext() {
    return g_last_project_ptr.load(std::memory_order_acquire) != nullptr;
}

bool AddClipToCurrentProjectByName(
    const std::string& clip_name,
    uint64_t owner_id,
    int destination_index,
    std::wstring& out_error,
    int& out_new_clip_count) {
    out_error.clear();
    out_new_clip_count = 0;
    EnsureHooksInstalled();

    if (g_prepare_clip_by_name == nullptr || g_move_clip_original == nullptr) {
        out_error =
            L"Native add-clip functions are not available yet. Open montage editing once, then retry.";
        return false;
    }

    if (clip_name.empty()) {
        out_error = L"clipName must be non-empty.";
        return false;
    }

    void* project_ptr = g_last_project_ptr.load(std::memory_order_acquire);
    if (project_ptr == nullptr) {
        out_error =
            L"No active CVideoEditorProject context captured yet. Load a project through native StartLoadProject, then retry.";
        return false;
    }

    // Compute live destination index from CMontage clip count (montage + 0x08 = u16 clip count).
    void* montage = *reinterpret_cast<void**>(static_cast<uint8_t*>(project_ptr) + 0x320);
    if (montage == nullptr) {
        out_error = L"CMontage pointer at project+0x320 is null. Project not fully loaded.";
        return false;
    }
    const int live_dest_index = static_cast<int>(
        *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(montage) + 0x08));

    const bool prepared = g_prepare_clip_by_name(project_ptr, clip_name.c_str(), owner_id, 0u, 0u);
    if (!prepared) {
        out_error = L"PrepareStagingClip(name, ownerId) returned false.";
        return false;
    }

    const bool move_result = g_move_clip_original(project_ptr, live_dest_index);

    out_new_clip_count = static_cast<int>(
        *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(montage) + 0x08));

    const std::wstring log =
        L"[EVER2] AddClip(name) executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr)) +
        L" ownerId=" + std::to_wstring(owner_id) +
        L" liveDestIndex=" + std::to_wstring(live_dest_index) +
        L" requestedDestIndex=" + std::to_wstring(destination_index) +
        L" moveResult=" + std::to_wstring(move_result ? 1 : 0) +
        L" newClipCount=" + std::to_wstring(out_new_clip_count);
    ever::platform::LogDebug(log.c_str());

    return true;
}

bool SaveCurrentProject(std::wstring& out_error) {
    out_error.clear();
    EnsureHooksInstalled();

    if (g_save_project_original == nullptr) {
        out_error =
            L"Native SaveProject function is not available yet. Ensure Rockstar Editor hooks are initialized.";
        return false;
    }

    void* project_ptr = g_last_project_ptr.load(std::memory_order_acquire);
    if (project_ptr == nullptr) {
        out_error =
            L"No active CVideoEditorProject context captured yet. Load a project through native StartLoadProject, then retry.";
        return false;
    }

    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_project_path_mutex);
        path = g_last_project_path;
    }

    if (path.empty()) {
        out_error =
            L"Native save filename is unknown. Load a project first so StartLoad path can be captured, then retry save.";
        return false;
    }

    std::string stem = path;
    const size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) {
        stem = stem.substr(slash + 1);
    }
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }

    const int save_result = g_save_project_original(project_ptr, stem.c_str(), true);

    const std::wstring log =
        L"[EVER2] SaveProject executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr)) +
        L" stem='" + std::wstring(stem.begin(), stem.end()) +
        L"' resultCode=" + std::to_wstring(save_result);
    ever::platform::LogDebug(log.c_str());

    if (save_result == 638) {
        out_error =
            L"SaveProject returned error 638 (invalid save context or filename).";
        return false;
    }

    return true;
}

bool StartLoadProjectByPath(const std::string& project_path, std::wstring& out_error) {
    out_error.clear();
    EnsureHooksInstalled();

    if (g_start_load_project == nullptr) {
        out_error =
            L"Native StartLoadProject function is not available yet. Ensure Rockstar Editor hooks are initialized.";
        return false;
    }

    if (project_path.empty()) {
        out_error = L"projectPath must be a non-empty string.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_project_path_mutex);
        g_last_project_path = project_path;
    }

    g_montage_load_complete.store(false, std::memory_order_release);

    const bool result = g_start_load_project(project_path.c_str());
    if (!result) {
        out_error = L"StartLoadProject returned false. Project may be invalid or editor state does not allow loading.";
        return false;
    }

    const std::wstring log =
        L"[EVER2] Native StartLoadProject requested. path='" +
        std::wstring(project_path.begin(), project_path.end()) + L"'";
    ever::platform::LogDebug(log.c_str());

    return true;
}

}

