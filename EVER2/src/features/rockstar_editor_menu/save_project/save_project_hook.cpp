#include "ever/features/rockstar_editor_menu/save_project/save_project_hook.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

namespace {

int __fastcall HookedSaveProject(void* this_ptr, const char* filename, bool show_spinner_with_text) {
    CaptureProjectContext(this_ptr, L"SaveProject");

    if (filename != nullptr && filename[0] != '\0') {
        std::lock_guard<std::mutex> lock(g_project_path_mutex);
        g_last_project_path = filename;
    }
    g_last_save_spinner_preference = show_spinner_with_text;

    if (g_save_project_original == nullptr) {
        return 638;
    }

    return g_save_project_original(this_ptr, filename, show_spinner_with_text);
}

}

void InstallSaveProjectHookNoThrow() {
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

}
