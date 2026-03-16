#include "ever/browser/native_overlay_renderer_internal.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace ever::browser::native_overlay_internal {

bool EnsureDeviceFromSwapChain(IDXGISwapChain* swap_chain) {
    if (swap_chain == nullptr) {
        Log(L"[EVER2] EnsureDeviceFromSwapChain received null swap chain.");
        return false;
    }

    if (g_swap_chain.Get() == swap_chain && g_device != nullptr && g_context != nullptr) {
        return EnsureOverlayPipeline();
    }

    ResetD3DResources();
    g_swap_chain = swap_chain;

    HRESULT hr = swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(g_device.GetAddressOf()));
    if (FAILED(hr) || g_device == nullptr) {
        const std::wstring message = L"[EVER2] Failed to get D3D11 device from swap chain. hr=" + HrToString(hr) +
            L" swapChain=" + PtrToString(swap_chain);
        Log(message.c_str());
        ResetD3DResources();
        return false;
    }

    g_device->GetImmediateContext(g_context.GetAddressOf());
    if (g_context == nullptr) {
        Log(L"[EVER2] Failed to get D3D11 immediate context.");
        ResetD3DResources();
        return false;
    }

    Log((L"[EVER2] Bound new swap chain for native overlay. swapChain=" + PtrToString(swap_chain)).c_str());

    return EnsureOverlayPipeline();
}

bool EnsureOverlayTexture(int width, int height) {
    if (g_device == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    if (g_overlay_texture != nullptr && width == g_overlay_texture_width && height == g_overlay_texture_height) {
        return true;
    }

    g_overlay_srv.Reset();
    g_overlay_texture.Reset();
    g_overlay_texture_width = 0;
    g_overlay_texture_height = 0;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = static_cast<UINT>(width);
    tex_desc.Height = static_cast<UINT>(height);
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = g_device->CreateTexture2D(&tex_desc, nullptr, g_overlay_texture.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateTexture2D(overlay) failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = tex_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = g_device->CreateShaderResourceView(g_overlay_texture.Get(), &srv_desc, g_overlay_srv.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateShaderResourceView(overlay) failed. hr=" + HrToString(hr);
        Log(message.c_str());
        g_overlay_texture.Reset();
        return false;
    }

    g_overlay_texture_width = width;
    g_overlay_texture_height = height;
    wchar_t message[160] = {};
    swprintf_s(message, L"[EVER2] Recreated overlay texture %dx%d.", width, height);
    Log(message);
    return true;
}

void UpdateTargetSizeFromSwapChain(IDXGISwapChain* swap_chain) {
    if (swap_chain == nullptr) {
        return;
    }

    EnsureNativeInputHookFromSwapChain(swap_chain);

    ComPtr<ID3D11Texture2D> backbuffer;
    HRESULT hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backbuffer.GetAddressOf()));
    if (FAILED(hr) || backbuffer == nullptr) {
        return;
    }

    D3D11_TEXTURE2D_DESC bb_desc = {};
    backbuffer->GetDesc(&bb_desc);

    const int new_w = static_cast<int>(bb_desc.Width);
    const int new_h = static_cast<int>(bb_desc.Height);
    const int old_w = g_target_width.exchange(new_w, std::memory_order_relaxed);
    const int old_h = g_target_height.exchange(new_h, std::memory_order_relaxed);
    if (old_w != new_w || old_h != new_h) {
        g_resize_requested.store(true, std::memory_order_release);
    }
}

bool UploadLatestFrame() {
    if (g_context == nullptr) {
        return false;
    }

    if (g_cef_shared_texture_enabled.load(std::memory_order_acquire)) {
        HANDLE shared_handle = nullptr;
        bool refresh_required = false;
        uint64_t shared_sequence = 0;
        {
            std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
            shared_handle = g_cef_shared_texture_handle;
            refresh_required = g_cef_shared_texture_refresh_required;
            shared_sequence = g_cef_shared_texture_sequence;
        }

        if (shared_handle != nullptr) {
            const bool needs_reopen =
                refresh_required ||
                g_overlay_srv == nullptr ||
                g_overlay_texture == nullptr ||
                g_opened_shared_texture_handle != shared_handle ||
                g_uploaded_sequence != shared_sequence;

            if (needs_reopen) {
                ComPtr<ID3D11Texture2D> shared_texture;
                const HRESULT hr = g_device->OpenSharedResource(
                    shared_handle,
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(shared_texture.GetAddressOf()));
                if (SUCCEEDED(hr) && shared_texture != nullptr) {
                    D3D11_TEXTURE2D_DESC shared_desc = {};
                    shared_texture->GetDesc(&shared_desc);

                    ComPtr<ID3D11ShaderResourceView> shared_srv;
                    const HRESULT srv_hr = g_device->CreateShaderResourceView(shared_texture.Get(), nullptr, shared_srv.GetAddressOf());
                    if (SUCCEEDED(srv_hr) && shared_srv != nullptr) {
                        g_overlay_texture = shared_texture;
                        g_overlay_srv = shared_srv;
                        g_overlay_texture_width = static_cast<int>(shared_desc.Width);
                        g_overlay_texture_height = static_cast<int>(shared_desc.Height);
                        g_opened_shared_texture_handle = shared_handle;
                        g_uploaded_sequence = shared_sequence;
                        g_overlay_texture_from_shared_handle.store(true, std::memory_order_release);
                        g_cef_shared_texture_failed.store(false, std::memory_order_release);
                        {
                            std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
                            g_cef_shared_texture_refresh_required = false;
                        }
                        return true;
                    }

                    if (!g_cef_shared_texture_failed.exchange(true, std::memory_order_acq_rel)) {
                        const std::wstring message =
                            L"[EVER2] Shared CEF texture path unavailable (CreateShaderResourceView failed). hr=" + HrToString(srv_hr) +
                            L". Falling back to CPU OnPaint upload.";
                        Log(message.c_str());
                    }
                } else if (!g_cef_shared_texture_failed.exchange(true, std::memory_order_acq_rel)) {
                    const std::wstring message =
                        L"[EVER2] Shared CEF texture path unavailable (OpenSharedResource failed). hr=" + HrToString(hr) +
                        L". Falling back to CPU OnPaint upload.";
                    Log(message.c_str());
                }
            } else {
                return true;
            }
        }

        return false;
    }

    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
    std::vector<uint8_t> pixels;

    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        width = g_frame.width;
        height = g_frame.height;
        sequence = g_frame.sequence;
        if (width <= 0 || height <= 0 || g_frame.pixels.empty()) {
            if (!g_logged_onpaint_never_called.exchange(true, std::memory_order_acq_rel)) {
                Log(L"[EVER2] UploadLatestFrame has no frame data yet (OnPaint not received).");
            }
            return false;
        }

        if (sequence == g_uploaded_sequence && g_overlay_srv != nullptr) {
            return true;
        }

        pixels = g_frame.pixels;
    }

    const int target_w = g_target_width.load(std::memory_order_relaxed);
    const int target_h = g_target_height.load(std::memory_order_relaxed);
    if (target_w > 1 && target_h > 1 && (width != target_w || height != target_h)) {
        g_resize_requested.store(true, std::memory_order_release);
        const uint64_t skips = g_frame_size_wait_skips.fetch_add(1, std::memory_order_relaxed) + 1;
        wchar_t msg[260] = {};
        swprintf_s(msg,
                   L"[EVER2] Waiting for CEF frame resize: frame=%dx%d target=%dx%d skips=%llu",
                   width,
                   height,
                   target_w,
                   target_h,
                   static_cast<unsigned long long>(skips));
        LogRateLimited(skips, 120, msg);
        return false;
    }

    if (!g_logged_frame_alpha_stats.exchange(true, std::memory_order_acq_rel)) {
        size_t non_zero_alpha = 0;
        uint8_t min_alpha = 255;
        uint8_t max_alpha = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            const uint8_t a = pixels[i];
            min_alpha = (std::min)(min_alpha, a);
            max_alpha = (std::max)(max_alpha, a);
            if (a != 0) {
                ++non_zero_alpha;
            }
        }

        const size_t total_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        wchar_t alpha_msg[320] = {};
        swprintf_s(alpha_msg,
                   L"[EVER2] First CEF frame alpha stats: min=%u max=%u nonZero=%zu total=%zu.",
                   static_cast<unsigned int>(min_alpha),
                   static_cast<unsigned int>(max_alpha),
                   non_zero_alpha,
                   total_pixels);
        Log(alpha_msg);
    }

    bool all_zero_alpha = true;
    size_t non_zero_rgb = 0;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        const uint8_t b = pixels[i - 3];
        const uint8_t g = pixels[i - 2];
        const uint8_t r = pixels[i - 1];
        if ((r | g | b) != 0) {
            ++non_zero_rgb;
        }
        if (pixels[i] != 0) {
            all_zero_alpha = false;
        }
    }

    const bool blank_rgb_frame = (non_zero_rgb == 0);
    g_last_uploaded_frame_blank.store(blank_rgb_frame, std::memory_order_release);
    if (blank_rgb_frame && !g_logged_blank_frame_recovery.exchange(true, std::memory_order_acq_rel)) {
        Log(L"[EVER2] CEF frame content is fully black (RGB all zero); enabling debug diagnostic draw fallback.");
    }

    if (all_zero_alpha && !blank_rgb_frame) {
        size_t synthesized_alpha = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            const uint8_t b = pixels[i - 3];
            const uint8_t g = pixels[i - 2];
            const uint8_t r = pixels[i - 1];
            const uint8_t rgb_max = (std::max)((std::max)(r, g), b);
            if (rgb_max != 0) {
                pixels[i] = 255;
                ++synthesized_alpha;
            }
        }

        if (!g_logged_alpha_recovery.exchange(true, std::memory_order_acq_rel)) {
            wchar_t msg[280] = {};
            swprintf_s(msg,
                       L"[EVER2] CEF frame alpha recovery active: host returned all-zero alpha; synthesized alpha for %zu pixels.",
                       synthesized_alpha);
            Log(msg);
        }
    }

    if (!EnsureOverlayTexture(width, height)) {
        return false;
    }

    g_context->UpdateSubresource(g_overlay_texture.Get(), 0, nullptr, pixels.data(), static_cast<UINT>(width * 4), 0);
    g_overlay_texture_from_shared_handle.store(false, std::memory_order_release);
    g_uploaded_sequence = sequence;
    return true;
}

bool DrawOverlay(IDXGISwapChain* swap_chain) {
    if (swap_chain == nullptr || g_context == nullptr || g_overlay_srv == nullptr) {
        const uint64_t miss = g_draw_overlay_missing_prereq.fetch_add(1, std::memory_order_relaxed) + 1;
        wchar_t message[220] = {};
        swprintf_s(
            message,
            L"[EVER2] DrawOverlay skipped: missing prereq (swap=%d ctx=%d srv=%d) count=%llu.",
            swap_chain != nullptr ? 1 : 0,
            g_context != nullptr ? 1 : 0,
            g_overlay_srv != nullptr ? 1 : 0,
            static_cast<unsigned long long>(miss));
        LogRateLimited(miss, 120, message);
        return false;
    }

    ComPtr<ID3D11Texture2D> backbuffer;
    HRESULT hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backbuffer.GetAddressOf()));
    if (FAILED(hr) || backbuffer == nullptr) {
        const std::wstring message = L"[EVER2] DrawOverlay GetBuffer failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_TEXTURE2D_DESC bb_desc = {};
    backbuffer->GetDesc(&bb_desc);
    g_target_width.store(static_cast<int>(bb_desc.Width), std::memory_order_relaxed);
    g_target_height.store(static_cast<int>(bb_desc.Height), std::memory_order_relaxed);

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = g_device->CreateRenderTargetView(backbuffer.Get(), nullptr, rtv.GetAddressOf());
    if (FAILED(hr) || rtv == nullptr) {
        const std::wstring message = L"[EVER2] DrawOverlay CreateRenderTargetView failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11StateBackup backup;
    SaveState(&backup);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(bb_desc.Width);
    viewport.Height = static_cast<float>(bb_desc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    const UINT stride = sizeof(OverlayVertex);
    const UINT offset = 0;
    ID3D11Buffer* vertex_buffer = g_overlay_texture_from_shared_handle.load(std::memory_order_acquire)
                                     ? g_vertex_buffer_flipped.Get()
                                     : g_vertex_buffer.Get();
    ID3D11ShaderResourceView* shader_resource = g_overlay_srv.Get();
    ID3D11ShaderResourceView* null_resource = nullptr;
    const FLOAT blend_factor[4] = {0.f, 0.f, 0.f, 0.f};

    g_context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    g_context->OMSetBlendState(g_blend_state.Get(), blend_factor, 0xffffffffu);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->RSSetState(g_rasterizer_state.Get());
    g_context->RSSetViewports(1, &viewport);
    g_context->IASetInputLayout(g_input_layout.Get());
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    g_context->VSSetShader(g_vertex_shader.Get(), nullptr, 0);
    g_context->PSSetShader(g_pixel_shader.Get(), nullptr, 0);
    g_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());
    g_context->PSSetShaderResources(0, 1, &shader_resource);
    g_context->Draw(4, 0);
    g_context->PSSetShaderResources(0, 1, &null_resource);

    RestoreState(backup);
    return true;
}

}
