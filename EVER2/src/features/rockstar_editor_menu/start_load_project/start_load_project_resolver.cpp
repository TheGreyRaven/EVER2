#include "ever/features/rockstar_editor_menu/start_load_project/start_load_project_resolver.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

void ResolveStartLoadProjectNoThrow() {
    if (g_start_load_project != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorInterfaceStartLoadProject,
        L"[EVER2] Editor interface start-load resolve");
    if (function_start == 0) {
        return;
    }

    g_start_load_project = reinterpret_cast<StartLoadProjectFn>(function_start);

    const std::wstring message =
        L"[EVER2] Editor interface start-load resolved. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
