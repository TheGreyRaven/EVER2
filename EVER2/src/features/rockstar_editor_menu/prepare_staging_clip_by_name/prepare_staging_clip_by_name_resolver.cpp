#include "ever/features/rockstar_editor_menu/prepare_staging_clip_by_name/prepare_staging_clip_by_name_resolver.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

void ResolvePrepareStagingClipByNameNoThrow() {
    if (g_prepare_clip_by_name != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectPrepareStagingClipByName,
        L"[EVER2] Editor project prepare clip-by-name resolve");
    if (function_start == 0) {
        return;
    }

    g_prepare_clip_by_name = reinterpret_cast<PrepareStagingClipByNameFn>(function_start);

    const std::wstring message =
        L"[EVER2] Editor project prepare clip-by-name resolved. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
