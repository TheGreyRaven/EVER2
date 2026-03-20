#include "ever/features/rockstar_editor_menu/prepare_staging_clip_by_index/prepare_staging_clip_by_index_hook.h"

#include "ever/features/rockstar_editor_menu/internal/rockstar_editor_menu_state.h"
#include "ever/hooking/game_function_patterns.h"
#include "ever/platform/debug_console.h"

namespace ever::features::rockstar_editor_menu {

namespace {

bool __fastcall HookedPrepareStagingClipByIndex(void* this_ptr, int source_index) {
    CaptureProjectContext(this_ptr, L"PrepareStagingClip(sourceIndex)");
    if (g_prepare_clip_original == nullptr) {
        return false;
    }
    return g_prepare_clip_original(this_ptr, source_index);
}

}

void InstallPrepareStagingClipByIndexHookNoThrow() {
    if (g_prepare_clip_detour != nullptr && g_prepare_clip_original != nullptr) {
        return;
    }

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::VideoEditorProjectPrepareStagingClipByIndex,
        L"[EVER2] Editor project prepare clip hook");
    if (function_start == 0) {
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedPrepareStagingClipByIndex),
        &g_prepare_clip_original,
        g_prepare_clip_detour);
    if (FAILED(hr) || g_prepare_clip_detour == nullptr || g_prepare_clip_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Editor project prepare clip hook install failed. hr=" +
            std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Editor project prepare clip hook installed. functionStart=" +
        std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}
