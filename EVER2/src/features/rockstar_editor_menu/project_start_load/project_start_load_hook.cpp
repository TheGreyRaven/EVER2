#include "ever/features/rockstar_editor_menu/project_start_load/project_start_load_hook.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

#include <nlohmann/json.hpp>

namespace ever::features::rockstar_editor_menu {

namespace {

bool __fastcall HookedProjectStartLoad(void* this_ptr, const char* path) {
    CaptureProjectContext(this_ptr, L"StartLoad(projectPath)");

    if (path != nullptr && path[0] != '\0') {
        std::lock_guard<std::mutex> lock(g_project_path_mutex);
        g_last_project_path = path;
    }

    g_montage_load_complete.store(false, std::memory_order_release);

    if (g_project_start_load_original == nullptr) {
        return false;
    }

    const bool result = g_project_start_load_original(this_ptr, path);

    g_montage_load_complete.store(true, std::memory_order_release);

    nlohmann::json evt;
    evt["event"] = "ever2_project_loaded";
    ever::browser::SendCefMessage(evt.dump());

    ever::platform::LogDebug(L"[EVER2] ProjectStartLoad complete — CMontage ready, signalled ever2_project_loaded.");

    return result;
}

}

void InstallProjectStartLoadHookNoThrow() {
    if (g_project_start_load_detour != nullptr && g_project_start_load_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectStartLoad,
        L"[EVER2] Editor project start-load hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedProjectStartLoad),
        &g_project_start_load_original,
        g_project_start_load_detour);
    if (FAILED(hr) || g_project_start_load_detour == nullptr || g_project_start_load_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Editor project start-load hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Editor project start-load hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
