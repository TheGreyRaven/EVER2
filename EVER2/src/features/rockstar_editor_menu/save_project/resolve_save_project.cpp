#include "ever/features/rockstar_editor_menu/save_project/resolve_save_project.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

void ResolveSaveProjectNoThrow() {
    if (g_save_project_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectSaveProject,
        L"[EVER2] Editor project save resolve");
    if (function_start == 0) {
        return;
    }

    g_save_project_original = reinterpret_cast<SaveProjectFn>(function_start);

    const std::wstring message =
        L"[EVER2] Editor project save resolved. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
