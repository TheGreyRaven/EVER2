#include "ever/features/rockstar_editor_menu/rockstar_editor_menu_bridge.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/features/rockstar_editor_menu/move_staging_clip_to_project/move_staging_clip_to_project_hook.h"
#include "ever/features/rockstar_editor_menu/prepare_staging_clip_by_index/prepare_staging_clip_by_index_hook.h"
#include "ever/features/rockstar_editor_menu/prepare_staging_clip_by_name/prepare_staging_clip_by_name_resolver.h"
#include "ever/features/rockstar_editor_menu/project_start_load/project_start_load_hook.h"
#include "ever/features/rockstar_editor_menu/save_project/save_project_hook.h"
#include "ever/features/rockstar_editor_menu/start_load_project/start_load_project_resolver.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace ever::features::rockstar_editor_menu {

void EnsureHooksInstalled() {
    if (g_prepare_clip_detour != nullptr && g_prepare_clip_by_name != nullptr &&
        g_move_clip_detour != nullptr && g_save_project_detour != nullptr &&
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

    InstallPrepareStagingClipByIndexHookNoThrow();
    ResolvePrepareStagingClipByNameNoThrow();
    InstallMoveStagingClipToProjectHookNoThrow();
    InstallSaveProjectHookNoThrow();
    InstallProjectStartLoadHookNoThrow();
    ResolveStartLoadProjectNoThrow();
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
            L"No active CVideoEditorProject context captured yet. Load a project through native StartLoadProject, then retry.";
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

bool AddClipToCurrentProjectByName(
    const std::string& clip_name,
    uint64_t owner_id,
    int destination_index,
    std::wstring& out_error) {
    out_error.clear();
    EnsureHooksInstalled();

    if (g_prepare_clip_by_name == nullptr || g_move_clip_original == nullptr) {
        out_error =
            L"Native add-clip(name) functions are not available yet. Open montage editing once, then retry.";
        return false;
    }

    if (clip_name.empty() || destination_index < 0) {
        out_error = L"clipName and destinationIndex must be valid.";
        return false;
    }

    void* project_ptr = g_last_project_ptr.load(std::memory_order_acquire);
    if (project_ptr == nullptr) {
        out_error =
            L"No active CVideoEditorProject context captured yet. Open montage editing once, then retry.";
        return false;
    }

    const bool prepared = g_prepare_clip_by_name(project_ptr, clip_name.c_str(), owner_id, 0u, 0u);
    if (!prepared) {
        out_error = L"PrepareStagingClip(name, ownerId, transitions) returned false.";
        return false;
    }

    const bool move_result = g_move_clip_original(project_ptr, destination_index);

    const std::wstring message =
        L"[EVER2] AddClip(name) command executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr)) +
        L" ownerId=" + std::to_wstring(owner_id) +
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

    std::string filename;
    bool show_spinner_with_text = true;
    {
        std::lock_guard<std::mutex> lock(g_project_path_mutex);
        filename = g_last_project_path;
        show_spinner_with_text = g_last_save_spinner_preference;
    }

    if (filename.empty()) {
        out_error =
            L"Native save filename is unknown. Load/edit a project first so StartLoad path can be captured, then retry save.";
        return false;
    }

    auto push_candidate = [](std::vector<std::string>& list, const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (std::find(list.begin(), list.end(), value) == list.end()) {
            list.push_back(value);
        }
    };

    std::vector<std::string> filename_candidates;
    push_candidate(filename_candidates, filename);

    std::string basename = filename;
    const size_t slash = basename.find_last_of("/\\");
    if (slash != std::string::npos) {
        basename = basename.substr(slash + 1);
    }
    std::string stem = basename;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }
    push_candidate(filename_candidates, stem);
    push_candidate(filename_candidates, basename);

    int save_result = 638;
    std::string attempted_filename;
    for (const std::string& candidate : filename_candidates) {
        attempted_filename = candidate;
        save_result = g_save_project_original(project_ptr, candidate.c_str(), show_spinner_with_text);
        if (save_result != 638) {
            break;
        }
    }

    if (save_result == 638) {
        out_error =
            L"SaveProject returned error 638 (invalid save context or filename) after trying path and basename variants.";
        return false;
    }

    const std::wstring message =
        L"[EVER2] SaveProject command executed. projectPtr=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(project_ptr)) +
        L" filename='" + std::wstring(attempted_filename.begin(), attempted_filename.end()) +
        L"' showSpinner=" + std::to_wstring(show_spinner_with_text ? 1 : 0) +
        L" resultCode=" + std::to_wstring(save_result);
    ever::platform::LogDebug(message.c_str());

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

    const bool result = g_start_load_project(project_path.c_str());
    if (!result) {
        out_error = L"StartLoadProject returned false. Project may be invalid or editor state does not allow loading.";
        return false;
    }

    const std::wstring message =
        L"[EVER2] Native StartLoadProject requested. path='" +
        std::wstring(project_path.begin(), project_path.end()) + L"'";
    ever::platform::LogDebug(message.c_str());

    return true;
}

}
