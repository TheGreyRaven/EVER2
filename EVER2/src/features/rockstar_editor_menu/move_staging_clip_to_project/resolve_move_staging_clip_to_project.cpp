#include "ever/features/rockstar_editor_menu/move_staging_clip_to_project/resolve_move_staging_clip_to_project.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

void ResolveMoveStagingClipToProjectNoThrow() {
    if (g_move_clip_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectMoveStagingClipToProject,
        L"[EVER2] Editor project move-clip resolve");
    if (function_start == 0) {
        return;
    }

    g_move_clip_original = reinterpret_cast<MoveStagingClipToProjectFn>(function_start);

    const std::wstring message =
        L"[EVER2] Editor project move-clip resolved. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
