#include "ever/core/plugin.h"
#include "ever/browser/native_overlay_renderer.h"
#include "ever/platform/debug_console.h"
#include "ever/platform/dll_runtime_loader.h"
#include "ever/features/commands/command_dispatcher.h"
#include "shv/main.h"
#include "shv/script.h"

#include <polyhook2/ErrorLog.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace {

std::atomic<bool> g_monitor_running{false};
std::atomic<uint64_t> g_present_count{0};
HANDLE g_monitor_thread = nullptr;
std::atomic<uint64_t> g_monitor_heartbeat_count{0};
std::atomic<bool> g_standalone_runtime_running{false};
HANDLE g_standalone_runtime_thread = nullptr;
std::atomic<bool> g_console_deferred_started{false};
ULONGLONG g_monitor_start_tick = 0;

class PolyHookLogger final : public PLH::Logger {
public:
    void log(const std::string& msg, PLH::ErrorLevel level) override {
        const std::wstring wide(msg.begin(), msg.end());
        const wchar_t* level_prefix = L"DBG";

        switch (level) {
        case PLH::ErrorLevel::INFO:
            level_prefix = L"INF";
            break;
        case PLH::ErrorLevel::WARN:
            level_prefix = L"WRN";
            break;
        case PLH::ErrorLevel::SEV:
            level_prefix = L"ERR";
            break;
        case PLH::ErrorLevel::NONE:
        default:
            level_prefix = L"DBG";
            break;
        }

        const std::wstring out = L"[EVER2][PolyHook][" + std::wstring(level_prefix) + L"] " + wide;
        ever::platform::LogDebug(out.c_str());
    }
};

void LogHostModuleState() {
    const HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    const HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    const HMODULE shv = GetModuleHandleW(L"ScriptHookV.dll");
    const std::wstring message =
        L"[EVER2] Host module state: d3d11=" + std::to_wstring(reinterpret_cast<uintptr_t>(d3d11)) +
        L" dxgi=" + std::to_wstring(reinterpret_cast<uintptr_t>(dxgi)) +
        L" ScriptHookV=" + std::to_wstring(reinterpret_cast<uintptr_t>(shv));
    ever::platform::LogDebug(message.c_str());
}

DWORD WINAPI StandaloneRuntimeThreadProc(LPVOID) {
    ever::platform::LogDebug(L"[EVER2] Standalone native runtime thread started.");

    bool init_ok = false;
    ULONGLONG last_init_attempt = 0;

    while (g_standalone_runtime_running.load(std::memory_order_acquire)) {
        const ULONGLONG now = GetTickCount64();
        if (!init_ok && (last_init_attempt == 0 || (now - last_init_attempt) >= 5000)) {
            last_init_attempt = now;
            ever::platform::LogDebug(L"[EVER2] Standalone runtime attempting native overlay initialization.");
            init_ok = ever::browser::InitializeNativeOverlayRenderer();
            ever::platform::LogDebug(
                init_ok
                    ? L"[EVER2] Standalone runtime native overlay initialization succeeded."
                    : L"[EVER2] Standalone runtime native overlay initialization failed; will retry.");
        }

        if (init_ok) {
            ever::features::commands::PumpQueuedCommands();
            ever::browser::TickNativeOverlayRenderer();
        }

        Sleep(16);
    }

    if (ever::browser::IsNativeOverlayRendererActive()) {
        ever::platform::LogDebug(L"[EVER2] Standalone runtime shutting down native overlay renderer.");
        ever::browser::ShutdownNativeOverlayRenderer();
    }

    ever::platform::LogDebug(L"[EVER2] Standalone native runtime thread exiting.");
    return 0;
}

void EnsureStandaloneRuntimeThreadStarted() {
    if (g_standalone_runtime_running.load(std::memory_order_acquire)) {
        return;
    }

    g_standalone_runtime_running.store(true, std::memory_order_release);
    g_standalone_runtime_thread = CreateThread(nullptr, 0, StandaloneRuntimeThreadProc, nullptr, 0, nullptr);
    if (g_standalone_runtime_thread == nullptr) {
        g_standalone_runtime_running.store(false, std::memory_order_release);
        ever::platform::LogDebug(L"[EVER2] Failed to start standalone native runtime thread.");
        return;
    }

    ever::platform::LogDebug(L"[EVER2] Standalone native runtime thread launched because ScriptMain is inactive.");
}

// TODO: This whole monitoring and fallback mechanism is a bit hacky and had weird issues in the beginning, this should be replaced with a more robust solution in the future.
DWORD WINAPI RuntimeMonitorThreadProc(LPVOID) {
    while (g_monitor_running.load(std::memory_order_acquire)) {
        const uint64_t heartbeat = g_monitor_heartbeat_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed_ms = (g_monitor_start_tick == 0) ? 0 : (now - g_monitor_start_tick);
        const bool is_fivem_host = ever::platform::IsFiveMHostProcess();
        const uint64_t present_count = g_present_count.load(std::memory_order_relaxed);
        const bool native_active = ever::browser::IsNativeOverlayRendererActive();
        const bool script_started = ever::core::HasScriptMainStarted();
        const bool init_attempted = ever::core::IsOverlayInitAttempted();
        const bool overlay_initialized = ever::core::IsOverlayInitialized();
        const unsigned long long script_loops = ever::core::GetScriptMainLoopCount();
        const std::wstring message =
            L"[EVER2] DllMain monitor heartbeat=" + std::to_wstring(heartbeat) +
            L" elapsedMs=" + std::to_wstring(elapsed_ms) +
            L" presentCount=" + std::to_wstring(present_count) +
            L" nativeRendererActive=" + std::to_wstring(native_active ? 1 : 0) +
            L" scriptMainStarted=" + std::to_wstring(script_started ? 1 : 0) +
            L" scriptLoops=" + std::to_wstring(script_loops) +
            L" overlayInitAttempted=" + std::to_wstring(init_attempted ? 1 : 0) +
            L" overlayInitialized=" + std::to_wstring(overlay_initialized ? 1 : 0);
        ever::platform::LogDebug(message.c_str());

        if (!g_console_deferred_started.load(std::memory_order_acquire) && heartbeat >= 2 && (present_count > 0 || native_active)) {
            ever::platform::InitializeDebugConsole();
            ever::platform::LogDebug(
                L"[EVER2] Deferred debug console initialization completed after runtime activity detection.");
            g_console_deferred_started.store(true, std::memory_order_release);
        }

        const bool internal_dxgi_active =
            is_fivem_host && native_active && ever::browser::IsNativeOverlayUsingDxgiHook();

        if (present_count == 0 && heartbeat >= 2 && !internal_dxgi_active) {
            ever::platform::LogDebug(L"[EVER2] DllMain monitor warning: Present callback has not fired yet.");
        }
        if (!script_started && heartbeat >= 2) {
            const bool standalone_mode_active = g_standalone_runtime_running.load(std::memory_order_acquire);

            if (!standalone_mode_active) {
                const std::wstring script_warning =
                    L"[EVER2] DllMain monitor warning: ScriptMain has not started yet (elapsedMs=" +
                    std::to_wstring(elapsed_ms) + L").";
                ever::platform::LogDebug(script_warning.c_str());
            }

            const ULONGLONG fallback_timeout_ms = is_fivem_host ? 10000 : 90000;
            const bool late_timeout_reached = elapsed_ms >= fallback_timeout_ms;
            if (late_timeout_reached && !standalone_mode_active) {
                const std::wstring fallback_message =
                    L"[EVER2] DllMain monitor: enabling standalone runtime fallback (host=" +
                    std::wstring(is_fivem_host ? L"FiveM" : L"non-FiveM") +
                    L", elapsedMs=" + std::to_wstring(elapsed_ms) + L").";
                ever::platform::LogDebug(fallback_message.c_str());
                EnsureStandaloneRuntimeThreadStarted();
            }
        }

        if (heartbeat == 2 || (heartbeat % 6) == 0) {
            LogHostModuleState();
        }
        Sleep(5000);
    }

    ever::platform::LogDebug(L"[EVER2] DllMain monitor thread exiting.");
    return 0;
}

void OnPresent(void* swap_chain_ptr) {
    const uint64_t count = g_present_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || (count % 600) == 0) {
        const std::wstring message =
            L"[EVER2] DllMain OnPresent callback observed. count=" + std::to_wstring(count) +
            L" swapChainPtr=" + std::to_wstring(reinterpret_cast<uintptr_t>(swap_chain_ptr));
        ever::platform::LogDebug(message.c_str());
    }

    if (ever::platform::IsFiveMHostProcess() && ever::browser::IsNativeOverlayUsingDxgiHook()) {
        return;
    }

    ever::browser::OnPresentNativeOverlay(swap_chain_ptr);
}

}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_monitor_start_tick = GetTickCount64();
        g_console_deferred_started.store(false, std::memory_order_release);
        ever::platform::LogDebug(L"[EVER2] DLL_PROCESS_ATTACH.");
        {
            const std::wstring message =
                L"[EVER2] DllMain attach context: module=" + std::to_wstring(reinterpret_cast<uintptr_t>(hInstance)) +
                L" processId=" + std::to_wstring(GetCurrentProcessId()) +
                L" threadId=" + std::to_wstring(GetCurrentThreadId()) +
                L" presentCallbackRegister=" + std::to_wstring(reinterpret_cast<uintptr_t>(&presentCallbackRegister)) +
                L" presentCallbackUnregister=" + std::to_wstring(reinterpret_cast<uintptr_t>(&presentCallbackUnregister)) +
                L" scriptRegister=" + std::to_wstring(reinterpret_cast<uintptr_t>(&scriptRegister));
            ever::platform::LogDebug(message.c_str());
        }
        DisableThreadLibraryCalls(hInstance);
        if (!ever::platform::ConfigureRuntimeDllDirectory(hInstance)) {
            ever::platform::LogDebug(L"[EVER2] Runtime DLL directory configuration failed. Aborting attach.");
            return FALSE;
        }

        PLH::Log::registerLogger(std::make_unique<PolyHookLogger>());
        ever::platform::LogDebug(L"[EVER2] PolyHook logger registered.");

        g_monitor_running.store(true, std::memory_order_release);
        g_monitor_thread = CreateThread(nullptr, 0, RuntimeMonitorThreadProc, nullptr, 0, nullptr);
        if (g_monitor_thread == nullptr) {
            ever::platform::LogDebug(L"[EVER2] Failed to create DllMain monitor thread.");
        } else {
            ever::platform::LogDebug(L"[EVER2] DllMain monitor thread started.");
        }

        presentCallbackRegister(OnPresent);
        ever::platform::LogDebug(L"[EVER2] Present callback registered.");
        scriptRegister(hInstance, ever::core::ScriptMain);
        ever::platform::LogDebug(L"[EVER2] Script registered.");

        if (ever::platform::IsFiveMHostProcess()) {
            ever::platform::LogDebug(
                L"[EVER2] FiveM host detected: starting standalone runtime immediately (ScriptMain treated as optional)." );
            EnsureStandaloneRuntimeThreadStarted();
        }
        break;
    case DLL_PROCESS_DETACH:
        ever::platform::LogDebug(L"[EVER2] DLL_PROCESS_DETACH.");
        presentCallbackUnregister(OnPresent);
        ever::platform::LogDebug(L"[EVER2] Present callback unregistered.");

        g_monitor_running.store(false, std::memory_order_release);
        if (g_monitor_thread != nullptr) {
            WaitForSingleObject(g_monitor_thread, 2000);
            CloseHandle(g_monitor_thread);
            g_monitor_thread = nullptr;
        }

        g_standalone_runtime_running.store(false, std::memory_order_release);
        if (g_standalone_runtime_thread != nullptr) {
            WaitForSingleObject(g_standalone_runtime_thread, 3000);
            CloseHandle(g_standalone_runtime_thread);
            g_standalone_runtime_thread = nullptr;
        }

        ever::core::Shutdown();
        scriptUnregister(hInstance);
        ever::platform::ShutdownDebugConsole();
        break;
    default:
        break;
    }

    return TRUE;
}
