#include "ever/features/rockstar_editor_menu/move_staging_clip_to_project/move_staging_clip_to_project_hook.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

namespace {

bool __fastcall HookedMoveStagingClipToProject(void* this_ptr, int destination_index) {
    CaptureProjectContext(this_ptr, L"MoveStagingClipToProject");
    if (g_move_clip_original == nullptr) {
        return false;
    }
    return g_move_clip_original(this_ptr, destination_index);
}

}

void InstallMoveStagingClipToProjectHookNoThrow() {
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

}
