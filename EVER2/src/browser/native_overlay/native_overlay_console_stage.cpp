#include "ever/browser/native_overlay_renderer_internal.h"

#include <memory>

namespace ever::browser::native_overlay_internal {

std::atomic<uint64_t> g_script_present_bypasses{0};

bool ShouldBypassSourcePresentDraw(OverlayPresentSource source) {
    if (source != OverlayPresentSource::ScriptHook) {
        return false;
    }

    if (!g_dxgi_hook_installed.load(std::memory_order_acquire)) {
        return false;
    }

    const uint64_t bypasses = g_script_present_bypasses.fetch_add(1, std::memory_order_relaxed) + 1;
    if (bypasses == 1 || (bypasses % 600) == 0) {
        wchar_t log_line[260] = {};
        swprintf_s(log_line,
                   L"[EVER2] ScriptHook present draw bypassed (internal DXGI path is authoritative). count=%llu",
                   static_cast<unsigned long long>(bypasses));
        Log(log_line);
    }
    return true;
}

void RenderOverlayAtConsoleStage() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }

    IDXGISwapChain* swap_chain = g_swap_chain.Get();
    if (swap_chain == nullptr) {
        const uint64_t misses = g_console_stage_missing_swapchain.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(misses, 120, L"[EVER2] Console-stage render skipped: swap chain unavailable.");
        return;
    }

    if (!TryEnterOverlayDrawRegion()) {
        const uint64_t reentries = g_console_stage_reentry_skips.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(reentries, 300, L"[EVER2] Console-stage render skipped: draw region busy (reentry guard). ");
        return;
    }

    const auto guard = std::unique_ptr<void, void(*)(void*)>(reinterpret_cast<void*>(1), [](void*) {
        LeaveOverlayDrawRegion();
    });

    if (!EnsureDeviceFromSwapChain(swap_chain)) {
        const uint64_t fails = g_console_stage_device_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(fails, 120, L"[EVER2] Console-stage render skipped: EnsureDeviceFromSwapChain failed.");
        return;
    }

    UpdateTargetSizeFromSwapChain(swap_chain);

    if (!UploadLatestFrame()) {
        const uint64_t fails = g_console_stage_upload_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(fails, 120, L"[EVER2] Console-stage render skipped: UploadLatestFrame failed.");
        return;
    }

    if (!DrawOverlay(swap_chain)) {
        const uint64_t fails = g_console_stage_draw_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(fails, 120, L"[EVER2] Console-stage render skipped: DrawOverlay failed.");
        return;
    }

    const uint64_t draws = g_console_stage_draw_success.fetch_add(1, std::memory_order_relaxed) + 1;
    if (draws == 1 || (draws % 600) == 0) {
        wchar_t message[220] = {};
        swprintf_s(message,
                   L"[EVER2] Console-stage overlay draw success count=%llu.",
                   static_cast<unsigned long long>(draws));
        Log(message);
    }
}

}
