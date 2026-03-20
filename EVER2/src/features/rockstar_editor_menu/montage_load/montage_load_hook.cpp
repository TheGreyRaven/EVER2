#include "ever/features/rockstar_editor_menu/montage_load/montage_load_hook.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

#include <nlohmann/json.hpp>

namespace ever::features::rockstar_editor_menu {

namespace {

bool __fastcall HookedMontageLoad(void* this_ptr, const char* filename, uint64_t arg3, uint64_t arg4) {
    const bool result = g_montage_load_original(this_ptr, filename, arg3, arg4);

    void* project = g_last_project_ptr.load(std::memory_order_acquire);
    if (project != nullptr) {
        void* montage = *reinterpret_cast<void**>(static_cast<uint8_t*>(project) + 0x320);
        if (montage == this_ptr && result) {
            g_montage_load_complete.store(true, std::memory_order_release);

            ever::platform::LogDebug(L"[EVER2] CMontage::Load complete for active project. Signalling UI.");

            nlohmann::json event;
            event["event"] = "ever2_project_loaded";
            ever::browser::SendCefMessage(event.dump());
        }
    }

    return result;
}

}

void InstallMontageLoadHookNoThrow() {
    if (g_montage_load_detour != nullptr && g_montage_load_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::ReplayMontageLoad,
        L"[EVER2] CMontage::Load hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedMontageLoad),
        &g_montage_load_original,
        g_montage_load_detour);
    if (FAILED(hr) || g_montage_load_detour == nullptr || g_montage_load_original == nullptr) {
        const std::wstring message =
            L"[EVER2] CMontage::Load hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] CMontage::Load hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
