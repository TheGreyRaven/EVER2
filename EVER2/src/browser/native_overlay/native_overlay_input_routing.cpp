#include "ever/browser/native_overlay_renderer_internal.h"

#include <windowsx.h>

namespace ever::browser::native_overlay_internal {

std::atomic<uint64_t> g_input_mouse_consumed_conhost{0};
std::atomic<uint64_t> g_input_mouse_forwarded_cef{0};
std::atomic<uint64_t> g_input_mouse_consumed_cef{0};
std::atomic<uint64_t> g_input_mouse_passthrough{0};
std::atomic<uint64_t> g_input_keyboard_consumed_conhost{0};
std::atomic<uint64_t> g_input_keyboard_forwarded_cef{0};
std::atomic<uint64_t> g_input_keyboard_consumed_cef{0};
std::atomic<uint64_t> g_input_keyboard_passthrough{0};
std::atomic<bool> g_cef_interactions_enabled{false};
std::atomic<bool> g_cef_keyboard_interactions_enabled{false};
std::atomic<bool> g_cef_browser_focused{false};
std::atomic<ULONGLONG> g_last_cef_input_activity_tick{0};

std::mutex g_input_hook_mutex;
HWND g_input_hook_window = nullptr;
WNDPROC g_original_input_wndproc = nullptr;
std::atomic<bool> g_cef_tracking_mouse_leave{false};

void MarkCefInputActivity() {
    g_last_cef_input_activity_tick.store(GetTickCount64(), std::memory_order_relaxed);
}

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
LRESULT CALLBACK HookedInputWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WNDPROC original = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_input_hook_mutex);
        original = g_original_input_wndproc;
    }

    if (IsMouseMessageForRouting(msg)) {
        if (!g_cef_interactions_enabled.load(std::memory_order_acquire)) {
            g_input_mouse_passthrough.fetch_add(1, std::memory_order_relaxed);
            if (original != nullptr) {
                return CallWindowProcW(original, hwnd, msg, wparam, lparam);
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        const bool conhost_owns_input =
            IsConHostEventStageActive() || g_conhost_console_open.load(std::memory_order_acquire);
        if (conhost_owns_input) {
            g_input_mouse_consumed_conhost.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        if (ForwardMouseMessageToCef(hwnd, msg, wparam, lparam)) {
            g_input_mouse_forwarded_cef.fetch_add(1, std::memory_order_relaxed);
            g_input_mouse_consumed_cef.fetch_add(1, std::memory_order_relaxed);
            if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK) {
                return TRUE;
            }
            return 0;
        }

        g_input_mouse_passthrough.fetch_add(1, std::memory_order_relaxed);
    }

    if (IsKeyboardMessageForRouting(msg)) {
        if (!g_cef_keyboard_interactions_enabled.load(std::memory_order_acquire)) {
            g_input_keyboard_passthrough.fetch_add(1, std::memory_order_relaxed);
            if (original != nullptr) {
                return CallWindowProcW(original, hwnd, msg, wparam, lparam);
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        const bool conhost_owns_input =
            IsConHostEventStageActive() || g_conhost_console_open.load(std::memory_order_acquire);
        if (conhost_owns_input) {
            g_input_keyboard_consumed_conhost.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        if (ForwardKeyboardMessageToCef(msg, wparam, lparam)) {
            g_input_keyboard_forwarded_cef.fetch_add(1, std::memory_order_relaxed);
            g_input_keyboard_consumed_cef.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        g_input_keyboard_passthrough.fetch_add(1, std::memory_order_relaxed);
    }

    if (original != nullptr) {
        return CallWindowProcW(original, hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
#endif

void RemoveNativeInputHook() {
    std::lock_guard<std::mutex> lock(g_input_hook_mutex);
    if (g_input_hook_window != nullptr && g_original_input_wndproc != nullptr) {
        SetWindowLongPtrW(g_input_hook_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_input_wndproc));
    }
    g_input_hook_window = nullptr;
    g_original_input_wndproc = nullptr;
    g_cef_tracking_mouse_leave.store(false, std::memory_order_release);
}

void EnsureNativeInputHookFromSwapChain(IDXGISwapChain* swap_chain) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (swap_chain == nullptr) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(swap_chain->GetDesc(&desc))) {
        return;
    }

    HWND output_window = desc.OutputWindow;
    if (output_window == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_input_hook_mutex);
    if (g_input_hook_window == output_window && g_original_input_wndproc != nullptr) {
        return;
    }

    if (g_input_hook_window != nullptr && g_original_input_wndproc != nullptr) {
        SetWindowLongPtrW(g_input_hook_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_input_wndproc));
        g_input_hook_window = nullptr;
        g_original_input_wndproc = nullptr;
    }

    LONG_PTR previous = SetWindowLongPtrW(output_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedInputWindowProc));
    if (previous == 0) {
        const DWORD err = GetLastError();
        if (err != 0) {
            wchar_t line[240] = {};
            swprintf_s(line,
                       L"[EVER2] Native input hook install failed: SetWindowLongPtrW error=%lu",
                       static_cast<unsigned long>(err));
            Log(line);
            return;
        }
    }

    g_original_input_wndproc = reinterpret_cast<WNDPROC>(previous);
    g_input_hook_window = output_window;
    g_cef_tracking_mouse_leave.store(false, std::memory_order_release);
    Log((L"[EVER2] Native input hook installed on window=" + PtrToString(output_window)).c_str());
#else
    (void)swap_chain;
#endif
}

}
