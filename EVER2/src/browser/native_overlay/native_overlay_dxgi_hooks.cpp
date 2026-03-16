#include "ever/browser/native_overlay_renderer.h"
#include "ever/browser/native_overlay_renderer_internal.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

namespace ever::browser {
void OnPresentNativeOverlayFromInternalHook(void* swap_chain_ptr);
}

namespace ever::browser::native_overlay_internal {

std::mutex g_hook_mutex;
std::vector<std::pair<void**, void*>> g_hooked_vtable_slots;

using SwapChainPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using SwapChainResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

SwapChainPresentFn g_original_swapchain_present = nullptr;
SwapChainResizeBuffersFn g_original_swapchain_resize_buffers = nullptr;

constexpr wchar_t kHookDummyWindowClassName[] = L"EVER2NativeDxgiHookWindow";

std::atomic<uint64_t> g_present_reentry_skips{0};
std::atomic<uint64_t> g_present_internal_source_calls{0};
std::atomic<uint64_t> g_runtime_invariant_anomalies{0};
std::atomic<bool> g_logged_internal_source_without_hook{false};
std::atomic<bool> g_logged_browser_ready_without_cef{false};
std::atomic<bool> g_logged_script_bypass_without_hook{false};
std::atomic<bool> g_logged_conhost_bypass_without_bridge{false};

std::wstring GetLastErrorText(DWORD error_code) {
    wchar_t* message_buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message_buffer),
        0,
        nullptr);

    std::wstring message = (length != 0 && message_buffer != nullptr) ? message_buffer : L"<no message>";
    if (message_buffer != nullptr) {
        LocalFree(message_buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }

    return message;
}

LRESULT CALLBACK HookDummyWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void RemoveInstalledVTableHooks() {
    std::lock_guard<std::mutex> lock(g_hook_mutex);
    for (auto it = g_hooked_vtable_slots.rbegin(); it != g_hooked_vtable_slots.rend(); ++it) {
        DWORD old_protect = 0;
        if (VirtualProtect(it->first, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
            *it->first = it->second;
            VirtualProtect(it->first, sizeof(void*), old_protect, &old_protect);
        }
    }
    g_hooked_vtable_slots.clear();
    g_original_swapchain_present = nullptr;
    g_original_swapchain_resize_buffers = nullptr;
    g_dxgi_hook_installed.store(false, std::memory_order_release);
}

bool InstallVTableHook(void** vtable, size_t index, void* hook_target, void** original_target, const wchar_t* hook_label) {
    if (vtable == nullptr || hook_target == nullptr || original_target == nullptr) {
        return false;
    }

    void** slot = &vtable[index];

    {
        std::lock_guard<std::mutex> lock(g_hook_mutex);
        bool seen_slot = false;
        for (auto& patch : g_hooked_vtable_slots) {
            if (patch.first == slot) {
                seen_slot = true;
                if (*original_target == nullptr) {
                    *original_target = patch.second;
                }
                break;
            }
        }

        if (*original_target == nullptr) {
            *original_target = reinterpret_cast<void*>(*slot);
        }

        DWORD old_protect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
            const DWORD error = GetLastError();
            wchar_t message[280] = {};
            swprintf_s(message,
                       L"[EVER2] Failed to patch %s vtable slot (VirtualProtect error=%lu).",
                       hook_label,
                       static_cast<unsigned long>(error));
            Log(message);
            return false;
        }

        *slot = hook_target;
        VirtualProtect(slot, sizeof(void*), old_protect, &old_protect);

        if (!seen_slot) {
            g_hooked_vtable_slots.emplace_back(slot, *original_target);
        }
    }

    return true;
}

HRESULT STDMETHODCALLTYPE HookedSwapChainResizeBuffers(
    IDXGISwapChain* swap_chain,
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT new_format,
    UINT swap_chain_flags) {
    g_resize_requested.store(true, std::memory_order_release);

    if (g_original_swapchain_resize_buffers == nullptr) {
        return E_FAIL;
    }

    return g_original_swapchain_resize_buffers(
        swap_chain,
        buffer_count,
        width,
        height,
        new_format,
        swap_chain_flags);
}

HRESULT STDMETHODCALLTYPE HookedSwapChainPresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    const uint64_t calls = g_hooked_present_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((flags & DXGI_PRESENT_TEST) == 0) {
        ::ever::browser::OnPresentNativeOverlayFromInternalHook(swap_chain);
    }

    if (calls == 1 || (calls % 1200) == 0) {
        wchar_t message[220] = {};
        swprintf_s(message,
                   L"[EVER2] Internal DXGI Present hook activity: calls=%llu flags=0x%08X swapChain=%s",
                   static_cast<unsigned long long>(calls),
                   static_cast<unsigned int>(flags),
                   PtrToString(swap_chain).c_str());
        Log(message);
    }

    if (g_original_swapchain_present == nullptr) {
        return E_FAIL;
    }

    return g_original_swapchain_present(swap_chain, sync_interval, flags);
}

bool InstallSwapChainHooks(IDXGISwapChain* swap_chain, const wchar_t* source_label) {
    if (swap_chain == nullptr) {
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(swap_chain);
    const bool present_ok = InstallVTableHook(
        vtable,
        8,
        reinterpret_cast<void*>(&HookedSwapChainPresent),
        reinterpret_cast<void**>(&g_original_swapchain_present),
        L"IDXGISwapChain::Present");
    const bool resize_ok = InstallVTableHook(
        vtable,
        13,
        reinterpret_cast<void*>(&HookedSwapChainResizeBuffers),
        reinterpret_cast<void**>(&g_original_swapchain_resize_buffers),
        L"IDXGISwapChain::ResizeBuffers");

    if (present_ok && resize_ok) {
        g_dxgi_hook_installed.store(true, std::memory_order_release);
        std::wstring message = L"[EVER2] Internal DXGI hooks installed from ";
        message += source_label;
        message += L" swapChain=";
        message += PtrToString(swap_chain);
        Log(message.c_str());
    }

    return present_ok && resize_ok;
}

bool InstallDxgiHooksWithDummySwapChain() {
    const HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
    if (d3d11_module == nullptr) {
        const DWORD error = GetLastError();
        std::wstring message =
            L"[EVER2] Internal DXGI hook setup failed: could not load d3d11.dll. error=" +
            std::to_wstring(error) + L" text=" + GetLastErrorText(error);
        Log(message.c_str());
        return false;
    }

    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = HookDummyWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kHookDummyWindowClassName;
    RegisterClassW(&window_class);

    HWND dummy_window = CreateWindowExW(
        0,
        kHookDummyWindowClassName,
        L"EVER2 Native DXGI Hook",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        64,
        64,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);
    if (dummy_window == nullptr) {
        const DWORD error = GetLastError();
        std::wstring message =
            L"[EVER2] Internal DXGI hook setup failed: CreateWindowExW failed. error=" +
            std::to_wstring(error) + L" text=" + GetLastErrorText(error);
        Log(message.c_str());
        UnregisterClassW(kHookDummyWindowClassName, window_class.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = dummy_window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    static constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    static constexpr D3D_FEATURE_LEVEL fallback_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL out_level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        &desc,
        swap_chain.GetAddressOf(),
        device.GetAddressOf(),
        &out_level,
        context.GetAddressOf());
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            fallback_levels,
            static_cast<UINT>(std::size(fallback_levels)),
            D3D11_SDK_VERSION,
            &desc,
            swap_chain.GetAddressOf(),
            device.GetAddressOf(),
            &out_level,
            context.GetAddressOf());
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &desc,
            swap_chain.GetAddressOf(),
            device.GetAddressOf(),
            &out_level,
            context.GetAddressOf());
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                fallback_levels,
                static_cast<UINT>(std::size(fallback_levels)),
                D3D11_SDK_VERSION,
                &desc,
                swap_chain.GetAddressOf(),
                device.GetAddressOf(),
                &out_level,
                context.GetAddressOf());
        }
    }

    bool hook_ok = false;
    if (SUCCEEDED(hr) && swap_chain != nullptr) {
        hook_ok = InstallSwapChainHooks(swap_chain.Get(), L"dummy D3D11 swap chain");
    } else {
        std::wstring message =
            L"[EVER2] Internal DXGI hook setup failed: D3D11CreateDeviceAndSwapChain failed. hr=" + HrToString(hr);
        Log(message.c_str());
    }

    DestroyWindow(dummy_window);
    UnregisterClassW(kHookDummyWindowClassName, window_class.hInstance);

    return hook_ok;
}

void EnsureDxgiHookInstalled() {
    if (g_dxgi_hook_installed.load(std::memory_order_acquire)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last_attempt = g_last_dxgi_hook_attempt_tick.load(std::memory_order_relaxed);
    if (last_attempt != 0 && (now - last_attempt) < 2000) {
        return;
    }

    g_last_dxgi_hook_attempt_tick.store(now, std::memory_order_relaxed);
    if (!InstallDxgiHooksWithDummySwapChain()) {
        g_dxgi_hook_failed.store(true, std::memory_order_release);
    }
}

void ValidateOverlayRuntimeInvariants() {
    if (g_browser_ready.load(std::memory_order_acquire) && !g_cef_initialized.load(std::memory_order_acquire)) {
        if (!g_logged_browser_ready_without_cef.exchange(true, std::memory_order_acq_rel)) {
            const uint64_t anomalies = g_runtime_invariant_anomalies.fetch_add(1, std::memory_order_relaxed) + 1;
            wchar_t line[240] = {};
            swprintf_s(line,
                       L"[EVER2] Runtime invariant anomaly: browserReady=1 while cefInitialized=0. anomalies=%llu",
                       static_cast<unsigned long long>(anomalies));
            Log(line);
        }
    }

    if (g_script_present_bypasses.load(std::memory_order_relaxed) > 0 && !g_dxgi_hook_installed.load(std::memory_order_acquire)) {
        if (!g_logged_script_bypass_without_hook.exchange(true, std::memory_order_acq_rel)) {
            const uint64_t anomalies = g_runtime_invariant_anomalies.fetch_add(1, std::memory_order_relaxed) + 1;
            wchar_t line[260] = {};
            swprintf_s(line,
                       L"[EVER2] Runtime invariant anomaly: script present bypass observed while DXGI hook is not installed. anomalies=%llu",
                       static_cast<unsigned long long>(anomalies));
            Log(line);
        }
    }

    if (g_conhost_event_stage_present_bypasses.load(std::memory_order_relaxed) > 0 &&
        !g_conhost_event_bridge_installed.load(std::memory_order_acquire)) {
        if (!g_logged_conhost_bypass_without_bridge.exchange(true, std::memory_order_acq_rel)) {
            const uint64_t anomalies = g_runtime_invariant_anomalies.fetch_add(1, std::memory_order_relaxed) + 1;
            wchar_t line[280] = {};
            swprintf_s(line,
                       L"[EVER2] Runtime invariant anomaly: ConHost event-stage present bypass observed while bridge is not installed. anomalies=%llu",
                       static_cast<unsigned long long>(anomalies));
            Log(line);
        }
    }
}

void HandleOverlayPresentForSource(void* swap_chain_ptr, OverlayPresentSource source) {
    const uint64_t present_call = g_present_calls.fetch_add(1, std::memory_order_relaxed) + 1;

    if (source == OverlayPresentSource::InternalDxgiHook) {
        g_present_internal_source_calls.fetch_add(1, std::memory_order_relaxed);
        if (!g_dxgi_hook_installed.load(std::memory_order_acquire) &&
            !g_logged_internal_source_without_hook.exchange(true, std::memory_order_acq_rel)) {
            const uint64_t anomalies = g_runtime_invariant_anomalies.fetch_add(1, std::memory_order_relaxed) + 1;
            wchar_t line[260] = {};
            swprintf_s(line,
                       L"[EVER2] Runtime invariant anomaly: internal DXGI source present callback observed while hookInstalled=0. anomalies=%llu",
                       static_cast<unsigned long long>(anomalies));
            Log(line);
        }
    }

    if (!g_initialized.load(std::memory_order_acquire)) {
        if (!g_logged_present_before_init.exchange(true, std::memory_order_acq_rel)) {
            Log(L"[EVER2] Present callback fired before native overlay initialization.");
        }
        return;
    }

    if (swap_chain_ptr == nullptr) {
        if (!g_logged_present_null_swapchain.exchange(true, std::memory_order_acq_rel)) {
            Log(L"[EVER2] Present callback received null swap chain pointer.");
        }
        return;
    }

    if (present_call == 1 || (present_call % 600) == 0) {
        const wchar_t* source_name = source == OverlayPresentSource::InternalDxgiHook ? L"internalDxgi" : L"scriptHook";
        const std::wstring message = L"[EVER2] Present callback active. count=" +
            std::to_wstring(present_call) +
            L" source=" + source_name +
            L" swapChain=" + PtrToString(swap_chain_ptr);
        Log(message.c_str());
    }

    auto* swap_chain = reinterpret_cast<IDXGISwapChain*>(swap_chain_ptr);
    if (!g_dxgi_hook_installed.load(std::memory_order_acquire)) {
        InstallSwapChainHooks(swap_chain, L"ScriptHookV present callback");
    }

    if (ShouldBypassSourcePresentDraw(source)) {
        return;
    }

    if (!TryEnterOverlayDrawRegion()) {
        const uint64_t reentries = g_present_reentry_skips.fetch_add(1, std::memory_order_relaxed) + 1;
        LogRateLimited(reentries, 600, L"[EVER2] Present draw skipped: draw region busy (reentry guard).");
        return;
    }

    const auto guard = std::unique_ptr<void, void(*)(void*)>(reinterpret_cast<void*>(1), [](void*) {
        LeaveOverlayDrawRegion();
    });

    if (!EnsureDeviceFromSwapChain(swap_chain)) {
        const uint64_t fail_count = g_present_device_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        wchar_t message[220] = {};
        swprintf_s(message, L"[EVER2] Present skip: EnsureDeviceFromSwapChain failed (count=%llu).", static_cast<unsigned long long>(fail_count));
        LogRateLimited(fail_count, 120, message);
        return;
    }

    UpdateTargetSizeFromSwapChain(swap_chain);

#if defined(_WIN64)
    if (IsConHostEventStageActive()) {
        const uint64_t bypasses = g_conhost_event_stage_present_bypasses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (bypasses == 1 || (bypasses % 600) == 0) {
            wchar_t line[220] = {};
            swprintf_s(line,
                       L"[EVER2] Present-stage draw bypassed while ConHost event-stage rendering is active. count=%llu",
                       static_cast<unsigned long long>(bypasses));
            Log(line);
        }
        return;
    }
#endif

    int previous_width = g_target_width.load(std::memory_order_relaxed);
    int previous_height = g_target_height.load(std::memory_order_relaxed);

    if (!UploadLatestFrame()) {
        const uint64_t fail_count = g_present_upload_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        wchar_t message[220] = {};
        swprintf_s(message, L"[EVER2] Present skip: UploadLatestFrame failed (count=%llu).", static_cast<unsigned long long>(fail_count));
        LogRateLimited(fail_count, 120, message);
        return;
    }

    if (!DrawOverlay(swap_chain)) {
        const uint64_t fail_count = g_present_draw_fail.fetch_add(1, std::memory_order_relaxed) + 1;
        wchar_t message[220] = {};
        swprintf_s(message, L"[EVER2] Present skip: DrawOverlay failed (count=%llu).", static_cast<unsigned long long>(fail_count));
        LogRateLimited(fail_count, 120, message);
        return;
    }

    const uint64_t draw_count = g_present_draw_success.fetch_add(1, std::memory_order_relaxed) + 1;
    if (draw_count == 1 || (draw_count % 600) == 0) {
        wchar_t message[220] = {};
        swprintf_s(message, L"[EVER2] Present draw success count=%llu.", static_cast<unsigned long long>(draw_count));
        Log(message);
    }

    const int next_width = g_target_width.load(std::memory_order_relaxed);
    const int next_height = g_target_height.load(std::memory_order_relaxed);
    if (next_width != previous_width || next_height != previous_height) {
        g_resize_requested.store(true, std::memory_order_release);
    }
}

}

namespace ever::browser {

using namespace native_overlay_internal;

void OnPresentNativeOverlay(void* swap_chain_ptr) {
    HandleOverlayPresentForSource(swap_chain_ptr, OverlayPresentSource::ScriptHook);
}

void OnPresentNativeOverlayFromInternalHook(void* swap_chain_ptr) {
    HandleOverlayPresentForSource(swap_chain_ptr, OverlayPresentSource::InternalDxgiHook);
}

bool IsNativeOverlayUsingDxgiHook() {
    return g_dxgi_hook_installed.load(std::memory_order_acquire);
}

}
