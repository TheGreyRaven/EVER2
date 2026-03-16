#include "ever/features/exit_rockstar_editor/exit_rockstar_editor_action.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/platform/debug_console.h"

#include <atomic>
#include <string>

namespace ever::features::exit_rockstar_editor {

namespace {

using VideoEditorCloseFn = void(*)(bool);

std::atomic<bool> g_resolve_attempted{false};
std::atomic<uint64_t> g_video_editor_close_addr{0};

bool ResolveVideoEditorCloseAddress() {
    if (g_resolve_attempted.exchange(true, std::memory_order_acq_rel)) {
        ever::platform::LogDebug(L"[EVER2] ExitEditor resolver reused cached result.");
        return g_video_editor_close_addr.load(std::memory_order_acquire) != 0;
    }

    ever::platform::LogDebug(L"[EVER2] ExitEditor resolver started.");

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const char* const* candidates =
        ever::hooking::GetGameFunctionPatternCandidates(ever::hooking::GameFunctionPatternId::VideoEditorClose);
    if (candidates == nullptr) {
        ever::platform::LogDebug(L"[EVER2] ExitEditor resolve failed: no pattern candidates registered.");
        return false;
    }

    for (size_t i = 0; candidates[i] != nullptr; ++i) {
        uint64_t found = 0;
        const std::string pattern_name = std::string("video_editor_close_") + std::to_string(i);
        {
            const std::wstring message =
                L"[EVER2] ExitEditor scanning candidate index=" + std::to_wstring(i);
            ever::platform::LogDebug(message.c_str());
        }
        scanner.AddPattern(pattern_name, candidates[i], &found);
        scanner.PerformScan();

        if (found != 0) {
            g_video_editor_close_addr.store(found, std::memory_order_release);
            const std::wstring message =
                L"[EVER2] Resolved CVideoEditorUi::Close(bool) at " + std::to_wstring(found) +
                L" using candidate index=" + std::to_wstring(i);
            ever::platform::LogDebug(message.c_str());
            return true;
        }
    }

    ever::platform::LogDebug(L"[EVER2] Failed to resolve CVideoEditorUi::Close(bool) pattern.");
    return false;
}

}

bool Execute(std::wstring& out_error) {
    out_error.clear();
    ever::platform::LogDebug(L"[EVER2] ExitEditor Execute called.");

    if (!ResolveVideoEditorCloseAddress()) {
        out_error = L"Unable to resolve CVideoEditorUi::Close(bool).";
        return false;
    }

    const uint64_t addr = g_video_editor_close_addr.load(std::memory_order_acquire);
    if (addr == 0) {
        out_error = L"Resolved CVideoEditorUi::Close(bool) address is null.";
        return false;
    }

    const auto fn = reinterpret_cast<VideoEditorCloseFn>(addr);
    fn(true);
    ever::platform::LogDebug(L"[EVER2] Executed Exit Rockstar Editor action via CVideoEditorUi::Close(true).");
    return true;
}

}
