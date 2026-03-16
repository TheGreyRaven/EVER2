#include "ever/browser/native_overlay_renderer_internal.h"

#include <windowsx.h>

namespace ever::browser::native_overlay_internal {

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
namespace {

int GetCefMouseModifiers(WPARAM wparam) {
    int modifiers = EVENTFLAG_NONE;
    if (wparam & MK_CONTROL) {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }
    if (wparam & MK_SHIFT) {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    if (wparam & MK_LBUTTON) {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if (wparam & MK_RBUTTON) {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }
    if (wparam & MK_MBUTTON) {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_COMMAND_DOWN;
    }
    return modifiers;
}

int GetCefMouseModifiersFromAsyncState() {
    int modifiers = EVENTFLAG_NONE;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_COMMAND_DOWN;
    }
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }
    if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    return modifiers;
}

int GetCefKeyModifiers() {
    int modifiers = EVENTFLAG_NONE;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers |= EVENTFLAG_COMMAND_DOWN;
    }
    if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
        modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    }
    if ((GetKeyState(VK_NUMLOCK) & 0x0001) != 0) {
        modifiers |= EVENTFLAG_NUM_LOCK_ON;
    }
    return modifiers;
}

CefMouseEvent BuildCefMouseEvent(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    POINT cursor = {};
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        // Wheel messages carry screen coordinates in lparam.
        cursor.x = GET_X_LPARAM(lparam);
        cursor.y = GET_Y_LPARAM(lparam);
        ScreenToClient(hwnd, &cursor);
    } else if (msg == WM_MOUSELEAVE) {
        if (GetCursorPos(&cursor)) {
            ScreenToClient(hwnd, &cursor);
        }
    } else {
        cursor.x = GET_X_LPARAM(lparam);
        cursor.y = GET_Y_LPARAM(lparam);
    }

    CefMouseEvent event;
    event.x = cursor.x;
    event.y = cursor.y;
    event.modifiers = GetCefMouseModifiers(wparam);
    return event;
}

}

bool IsMouseMessageForRouting(UINT msg) {
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

bool IsKeyboardMessageForRouting(UINT msg) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        return true;
    default:
        return false;
    }
}

bool ForwardMouseMessageToCef(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    CefRefPtr<CefBrowser> browser = g_browser;
    if (browser == nullptr) {
        return false;
    }

    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host == nullptr) {
        return false;
    }

    if (msg == WM_MOUSEMOVE && !g_cef_tracking_mouse_leave.exchange(true, std::memory_order_acq_rel)) {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
    }

    const CefMouseEvent event = BuildCefMouseEvent(hwnd, msg, wparam, lparam);
    MarkCefInputActivity();

    switch (msg) {
    case WM_MOUSEMOVE:
        host->SendMouseMoveEvent(event, false);
        return true;
    case WM_MOUSELEAVE:
        g_cef_tracking_mouse_leave.store(false, std::memory_order_release);
        host->SendMouseMoveEvent(event, true);
        return true;
    case WM_MOUSEWHEEL:
        host->SendMouseWheelEvent(event, 0, GET_WHEEL_DELTA_WPARAM(wparam));
        return true;
    case WM_MOUSEHWHEEL:
        host->SendMouseWheelEvent(event, GET_WHEEL_DELTA_WPARAM(wparam), 0);
        return true;
    case WM_LBUTTONDOWN:
        host->SendMouseClickEvent(event, MBT_LEFT, false, 1);
        return true;
    case WM_LBUTTONUP:
        host->SendMouseClickEvent(event, MBT_LEFT, true, 1);
        return true;
    case WM_LBUTTONDBLCLK:
        host->SendMouseClickEvent(event, MBT_LEFT, false, 2);
        return true;
    case WM_RBUTTONDOWN:
        host->SendMouseClickEvent(event, MBT_RIGHT, false, 1);
        return true;
    case WM_RBUTTONUP:
        host->SendMouseClickEvent(event, MBT_RIGHT, true, 1);
        return true;
    case WM_RBUTTONDBLCLK:
        host->SendMouseClickEvent(event, MBT_RIGHT, false, 2);
        return true;
    case WM_MBUTTONDOWN:
        host->SendMouseClickEvent(event, MBT_MIDDLE, false, 1);
        return true;
    case WM_MBUTTONUP:
        host->SendMouseClickEvent(event, MBT_MIDDLE, true, 1);
        return true;
    case WM_MBUTTONDBLCLK:
        host->SendMouseClickEvent(event, MBT_MIDDLE, false, 2);
        return true;
    default:
        return false;
    }
}

bool ForwardKeyboardMessageToCef(UINT msg, WPARAM wparam, LPARAM lparam) {
    CefRefPtr<CefBrowser> browser = g_browser;
    if (browser == nullptr) {
        return false;
    }

    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host == nullptr) {
        return false;
    }

    CefKeyEvent event;
    event.windows_key_code = static_cast<int>(wparam);
    event.native_key_code = static_cast<int>(lparam);
    event.modifiers = GetCefKeyModifiers();
    event.is_system_key =
        (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR || msg == WM_SYSDEADCHAR);

    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        MarkCefInputActivity();
        event.type = KEYEVENT_RAWKEYDOWN;
        host->SendKeyEvent(event);
        return true;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        MarkCefInputActivity();
        event.type = KEYEVENT_KEYUP;
        host->SendKeyEvent(event);
        return true;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        MarkCefInputActivity();
        event.type = KEYEVENT_CHAR;
        event.character = static_cast<char16_t>(wparam);
        event.unmodified_character = static_cast<char16_t>(wparam);
        host->SendKeyEvent(event);
        return true;
    default:
        return false;
    }
}

void PumpPolledMouseButtonsToCef() {
    static bool previous_left_down = false;
    static bool previous_right_down = false;
    static bool previous_middle_down = false;

    const bool current_left_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool current_right_down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool current_middle_down = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    if (!g_cef_interactions_enabled.load(std::memory_order_acquire)) {
        previous_left_down = current_left_down;
        previous_right_down = current_right_down;
        previous_middle_down = current_middle_down;
        return;
    }

    CefRefPtr<CefBrowser> browser = g_browser;
    if (browser == nullptr) {
        previous_left_down = current_left_down;
        previous_right_down = current_right_down;
        previous_middle_down = current_middle_down;
        return;
    }

    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host == nullptr) {
        previous_left_down = current_left_down;
        previous_right_down = current_right_down;
        previous_middle_down = current_middle_down;
        return;
    }

    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_input_hook_mutex);
        hwnd = g_input_hook_window;
    }

    POINT cursor = {};
    if (!GetCursorPos(&cursor)) {
        previous_left_down = current_left_down;
        previous_right_down = current_right_down;
        previous_middle_down = current_middle_down;
        return;
    }
    if (hwnd != nullptr) {
        ScreenToClient(hwnd, &cursor);
    }

    CefMouseEvent event;
    event.x = cursor.x;
    event.y = cursor.y;
    event.modifiers = GetCefMouseModifiersFromAsyncState();

    auto emit_click_transition = [&](bool previous_down,
                                     bool current_down,
                                     cef_mouse_button_type_t button,
                                     const wchar_t* button_name) {
        if (previous_down == current_down) {
            return;
        }

        host->SendMouseClickEvent(event, button, !current_down, 1);
        MarkCefInputActivity();
        g_input_mouse_forwarded_cef.fetch_add(1, std::memory_order_relaxed);
        g_input_mouse_consumed_cef.fetch_add(1, std::memory_order_relaxed);

        (void)button_name;
    };

    emit_click_transition(previous_left_down, current_left_down, MBT_LEFT, L"left");
    emit_click_transition(previous_right_down, current_right_down, MBT_RIGHT, L"right");
    emit_click_transition(previous_middle_down, current_middle_down, MBT_MIDDLE, L"middle");

    previous_left_down = current_left_down;
    previous_right_down = current_right_down;
    previous_middle_down = current_middle_down;
}

#endif

}
