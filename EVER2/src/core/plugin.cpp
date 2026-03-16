#include "ever/core/plugin.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/browser/overlay_manager.h"
#include "ever/features/commands/command_dispatcher.h"
#include "ever/platform/debug_console.h"
#include "shv/main.h"

#include <windows.h>
#include <atomic>
#include <string>

namespace {

bool g_initialized = false;
bool g_init_attempted = false;
ever::browser::OverlayManager g_overlay;
ULONGLONG g_last_heartbeat_tick = 0;
std::atomic<bool> g_script_main_started{false};
std::atomic<unsigned long long> g_script_loop_count{0};

}

namespace ever::core {

void ScriptMain() {
    g_script_main_started.store(true, std::memory_order_release);
    g_script_loop_count.store(0, std::memory_order_release);

    ever::platform::InitializeDebugConsole();
    ever::platform::LogDebug(L"[EVER2] ScriptMain entered.");

    {
        const std::wstring message =
            L"[EVER2] ScriptMain thread id=" + std::to_wstring(GetCurrentThreadId()) +
            L" process id=" + std::to_wstring(GetCurrentProcessId());
        ever::platform::LogDebug(message.c_str());
    }

    if (!g_initialized && !g_init_attempted) {
        ever::platform::LogDebug(L"[EVER2] ScriptMain starting overlay initialization attempt.");
        g_init_attempted = true;
        g_initialized = g_overlay.Initialize();
        const std::wstring message =
            L"[EVER2] ScriptMain overlay initialization result=" + std::to_wstring(g_initialized ? 1 : 0);
        ever::platform::LogDebug(message.c_str());
    }

    while (true) {
        const unsigned long long loop_count = g_script_loop_count.fetch_add(1, std::memory_order_relaxed) + 1;
        ever::features::commands::PumpQueuedCommands();
        g_overlay.Tick();

        if (loop_count == 1 || (loop_count % 1200ULL) == 0ULL) {
            const std::wstring message =
                L"[EVER2] ScriptMain loop activity count=" + std::to_wstring(loop_count);
            ever::platform::LogDebug(message.c_str());
        }

        const ULONGLONG now = GetTickCount64();
        if (g_last_heartbeat_tick == 0 || (now - g_last_heartbeat_tick) >= 5000) {
            g_last_heartbeat_tick = now;
            const bool native_active = ever::browser::IsNativeOverlayRendererActive();
            const std::wstring message =
                L"[EVER2] ScriptMain heartbeat: initialized=" + std::to_wstring(g_initialized ? 1 : 0) +
                L" initAttempted=" + std::to_wstring(g_init_attempted ? 1 : 0) +
                L" loopCount=" + std::to_wstring(loop_count) +
                L" nativeRendererActive=" + std::to_wstring(native_active ? 1 : 0);
            ever::platform::LogDebug(message.c_str());
        }

        WAIT(0);
    }
}

void Shutdown() {
    ever::platform::LogDebug(L"[EVER2] Plugin shutdown requested.");
    g_script_main_started.store(false, std::memory_order_release);
    g_script_loop_count.store(0, std::memory_order_release);

    if (!g_initialized) {
        return;
    }

    g_overlay.Shutdown();
    g_initialized = false;
    g_init_attempted = false;

    ever::platform::ShutdownDebugConsole();
}

bool HasScriptMainStarted() {
    return g_script_main_started.load(std::memory_order_acquire);
}

bool IsOverlayInitAttempted() {
    return g_init_attempted;
}

bool IsOverlayInitialized() {
    return g_initialized;
}

unsigned long long GetScriptMainLoopCount() {
    return g_script_loop_count.load(std::memory_order_acquire);
}

}
