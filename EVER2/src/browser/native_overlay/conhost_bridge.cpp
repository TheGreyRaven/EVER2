#include "ever/browser/native_overlay_renderer_internal.h"

#include "ever/platform/dll_runtime_loader.h"

#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ever::browser::native_overlay_internal {

#if defined(_WIN64)
template<typename... Args>
class CfxFwEvent {
public:
    using TFunc = std::function<bool(Args...)>;

private:
    struct callback {
        TFunc function;
        std::unique_ptr<callback> next = nullptr;
        int order = 0;
        size_t cookie = static_cast<size_t>(-1);

        explicit callback(TFunc func)
            : function(std::move(func)) {
        }

        ~callback() {
            while (next) {
                next = std::move(next->next);
            }
        }
    };

    std::unique_ptr<callback> m_callbacks;
    std::atomic<size_t> m_connect_cookie{0};

    size_t ConnectInternal(TFunc func, int order) {
        if (!func) {
            return static_cast<size_t>(-1);
        }

        auto cb = std::make_unique<callback>(std::move(func));
        const size_t cookie = m_connect_cookie.fetch_add(1, std::memory_order_relaxed);
        cb->order = order;
        cb->cookie = cookie;

        if (!m_callbacks) {
            m_callbacks = std::move(cb);
            return cookie;
        }

        auto* cur = &m_callbacks;
        callback* last = nullptr;
        while (*cur && order >= (*cur)->order) {
            last = cur->get();
            cur = &(*cur)->next;
        }

        cb->next = std::move(*cur);
        (!last ? m_callbacks : last->next) = std::move(cb);
        return cookie;
    }

public:
    template<typename T>
    size_t Connect(T func) {
        return Connect(func, 0);
    }

    template<typename T>
    size_t Connect(T func, int order) {
        if constexpr (std::is_same_v<std::invoke_result_t<T, Args...>, bool>) {
            return ConnectInternal(func, order);
        } else {
            return ConnectInternal([func](Args... args) {
                std::invoke(func, args...);
                return true;
            }, order);
        }
    }

    void Disconnect(size_t cookie) {
        if (cookie == static_cast<size_t>(-1)) {
            return;
        }

        callback* prev = nullptr;
        for (auto* cb = m_callbacks.get(); cb; cb = cb->next.get()) {
            if (cb->cookie == cookie) {
                if (prev) {
                    prev->next = std::move(cb->next);
                } else {
                    m_callbacks = std::move(cb->next);
                }
                break;
            }
            prev = cb;
        }
    }
};

using ConHostOnDrawGuiEvent = CfxFwEvent<>;
using ConHostOnShouldDrawGuiEvent = CfxFwEvent<bool*>;
using ConHostIsConsoleOpenFn = bool(*)();

struct ConHostEventBridgeState {
    ConHostOnDrawGuiEvent* on_draw_gui = nullptr;
    ConHostOnShouldDrawGuiEvent* on_should_draw_gui = nullptr;
    ConHostIsConsoleOpenFn is_console_open = nullptr;
    size_t draw_cookie = static_cast<size_t>(-1);
    size_t should_cookie = static_cast<size_t>(-1);
};

ConHostEventBridgeState g_conhost_event_bridge;
std::mutex g_conhost_event_bridge_mutex;
std::atomic<bool> g_conhost_event_bridge_installed{false};
std::atomic<bool> g_conhost_event_bridge_failed{false};
std::atomic<uint64_t> g_conhost_event_bridge_attempts{0};
std::atomic<ULONGLONG> g_conhost_event_bridge_last_try_tick{0};
std::atomic<uint64_t> g_conhost_on_draw_callbacks{0};
std::atomic<uint64_t> g_conhost_on_should_callbacks{0};
std::atomic<bool> g_conhost_console_open{false};
std::atomic<ULONGLONG> g_conhost_event_stage_draw_tick{0};
std::atomic<uint64_t> g_conhost_event_stage_present_bypasses{0};

constexpr ULONGLONG kConhostEventBridgeRetryIntervalMs = 2000;
constexpr ULONGLONG kConhostEventStageBypassWindowMs = 50;

// This is used in order to ensure that our CEF is above the FiveM CEF while staying under the F8 Imgui console.
constexpr const char* kConHostOnDrawGuiExport = "?OnDrawGui@ConHost@@3V?$fwEvent@$$V@@A";
constexpr const char* kConHostOnShouldDrawGuiExport = "?OnShouldDrawGui@ConHost@@3V?$fwEvent@PEA_N@@A";
constexpr const char* kConHostIsConsoleOpenExport = "?IsConsoleOpen@ConHost@@YA_NXZ";

bool OnConHostShouldDrawGuiEvent(bool* should_draw) {
    const bool value = (should_draw != nullptr) ? *should_draw : false;
    g_conhost_console_open.store(value, std::memory_order_release);

    if (!value) {
        g_conhost_event_stage_draw_tick.store(0, std::memory_order_relaxed);
    }

    const uint64_t calls = g_conhost_on_should_callbacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (calls == 1 || (calls % 600) == 0) {
        wchar_t line[180] = {};
        swprintf_s(line,
                   L"[EVER2] ConHost event OnShouldDrawGui callback observed: calls=%llu should=%d",
                   static_cast<unsigned long long>(calls),
                   value ? 1 : 0);
        Log(line);
    }

    return true;
}

bool OnConHostDrawGuiEvent() {
    ConHostIsConsoleOpenFn is_console_open = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_conhost_event_bridge_mutex);
        is_console_open = g_conhost_event_bridge.is_console_open;
    }

    bool console_open = g_conhost_console_open.load(std::memory_order_acquire);
    if (is_console_open != nullptr) {
        console_open = is_console_open();
    }
    g_conhost_console_open.store(console_open, std::memory_order_release);

    if (!console_open) {
        g_conhost_event_stage_draw_tick.store(0, std::memory_order_relaxed);
        return true;
    }

    const uint64_t calls = g_conhost_on_draw_callbacks.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t before_draws = g_console_stage_draw_success.load(std::memory_order_relaxed);
    RenderOverlayAtConsoleStage();
    const uint64_t after_draws = g_console_stage_draw_success.load(std::memory_order_relaxed);
    if (after_draws != before_draws) {
        g_conhost_event_stage_draw_tick.store(GetTickCount64(), std::memory_order_relaxed);
    }

    if (calls == 1 || (calls % 600) == 0) {
        wchar_t line[220] = {};
        swprintf_s(line,
                   L"[EVER2] ConHost event OnDrawGui callback observed: calls=%llu stageDraws=%llu",
                   static_cast<unsigned long long>(calls),
                   static_cast<unsigned long long>(after_draws));
        Log(line);
    }

    return true;
}

bool IsConHostEventStageActive() {
    if (!g_conhost_event_bridge_installed.load(std::memory_order_acquire)) {
        return false;
    }

    if (!g_conhost_console_open.load(std::memory_order_acquire)) {
        return false;
    }

    const ULONGLONG last_draw_tick = g_conhost_event_stage_draw_tick.load(std::memory_order_relaxed);
    if (last_draw_tick == 0) {
        return false;
    }

    return (GetTickCount64() - last_draw_tick) <= kConhostEventStageBypassWindowMs;
}

void RemoveConHostEventBridge() {
    std::lock_guard<std::mutex> lock(g_conhost_event_bridge_mutex);

    if (g_conhost_event_bridge.on_draw_gui != nullptr && g_conhost_event_bridge.draw_cookie != static_cast<size_t>(-1)) {
        g_conhost_event_bridge.on_draw_gui->Disconnect(g_conhost_event_bridge.draw_cookie);
    }
    if (g_conhost_event_bridge.on_should_draw_gui != nullptr && g_conhost_event_bridge.should_cookie != static_cast<size_t>(-1)) {
        g_conhost_event_bridge.on_should_draw_gui->Disconnect(g_conhost_event_bridge.should_cookie);
    }

    g_conhost_event_bridge = {};
    g_conhost_console_open.store(false, std::memory_order_release);
    g_conhost_event_stage_draw_tick.store(0, std::memory_order_relaxed);
    g_conhost_event_bridge_installed.store(false, std::memory_order_release);
}

bool EnsureConHostEventBridgeInstalled() {
    if (g_conhost_event_bridge_installed.load(std::memory_order_acquire)) {
        return true;
    }

    if (!::ever::platform::IsFiveMHostProcess()) {
        return false;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last_try = g_conhost_event_bridge_last_try_tick.load(std::memory_order_relaxed);
    if (last_try != 0 && (now - last_try) < kConhostEventBridgeRetryIntervalMs) {
        return false;
    }
    g_conhost_event_bridge_last_try_tick.store(now, std::memory_order_relaxed);

    const uint64_t attempts = g_conhost_event_bridge_attempts.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool verbose_log = (attempts == 1 || (attempts % 120) == 0);
    if (verbose_log) {
        wchar_t attempt_line[140] = {};
        swprintf_s(attempt_line,
                   L"[EVER2] ConHost event bridge install attempt=%llu",
                   static_cast<unsigned long long>(attempts));
        Log(attempt_line);
    }

    HMODULE conhost_module = GetModuleHandleW(L"conhost-v2.dll");
    if (conhost_module == nullptr) {
        if (verbose_log) {
            Log(L"[EVER2] ConHost event bridge waiting: conhost-v2.dll not loaded.");
        }
        return false;
    }

    auto* on_draw_gui = reinterpret_cast<ConHostOnDrawGuiEvent*>(GetProcAddress(conhost_module, kConHostOnDrawGuiExport));
    auto* on_should_draw_gui = reinterpret_cast<ConHostOnShouldDrawGuiEvent*>(GetProcAddress(conhost_module, kConHostOnShouldDrawGuiExport));
    auto* is_console_open = reinterpret_cast<ConHostIsConsoleOpenFn>(GetProcAddress(conhost_module, kConHostIsConsoleOpenExport));

    if (on_draw_gui == nullptr || on_should_draw_gui == nullptr || is_console_open == nullptr) {
        g_conhost_event_bridge_failed.store(true, std::memory_order_release);
        if (verbose_log) {
            wchar_t fail_line[260] = {};
            swprintf_s(fail_line,
                       L"[EVER2] ConHost event bridge waiting: missing exports draw=%d should=%d open=%d",
                       on_draw_gui != nullptr ? 1 : 0,
                       on_should_draw_gui != nullptr ? 1 : 0,
                       is_console_open != nullptr ? 1 : 0);
            Log(fail_line);
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(g_conhost_event_bridge_mutex);
    if (g_conhost_event_bridge_installed.load(std::memory_order_acquire)) {
        return true;
    }

    const size_t should_cookie = on_should_draw_gui->Connect(&OnConHostShouldDrawGuiEvent, 995);
    const size_t draw_cookie = on_draw_gui->Connect(&OnConHostDrawGuiEvent, -900);
    if (should_cookie == static_cast<size_t>(-1) || draw_cookie == static_cast<size_t>(-1)) {
        g_conhost_event_bridge_failed.store(true, std::memory_order_release);
        Log(L"[EVER2] ConHost event bridge failed: could not connect event callbacks.");
        return false;
    }

    g_conhost_event_bridge.on_draw_gui = on_draw_gui;
    g_conhost_event_bridge.on_should_draw_gui = on_should_draw_gui;
    g_conhost_event_bridge.is_console_open = is_console_open;
    g_conhost_event_bridge.draw_cookie = draw_cookie;
    g_conhost_event_bridge.should_cookie = should_cookie;
    g_conhost_event_bridge_failed.store(false, std::memory_order_release);
    g_conhost_event_bridge_installed.store(true, std::memory_order_release);

    wchar_t ok_line[320] = {};
    swprintf_s(ok_line,
               L"[EVER2] ConHost event bridge installed: onDraw=%s onShouldDraw=%s isConsoleOpen=%s drawCookie=%llu shouldCookie=%llu",
               PtrToString(on_draw_gui).c_str(),
               PtrToString(on_should_draw_gui).c_str(),
               PtrToString(reinterpret_cast<void*>(is_console_open)).c_str(),
               static_cast<unsigned long long>(draw_cookie),
               static_cast<unsigned long long>(should_cookie));
    Log(ok_line);
    return true;
}

bool TryEnterOverlayDrawRegion() {
    return !g_overlay_draw_in_progress.test_and_set(std::memory_order_acq_rel);
}

void LeaveOverlayDrawRegion() {
    g_overlay_draw_in_progress.clear(std::memory_order_release);
}
#endif

} // namespace ever::browser::native_overlay_internal
