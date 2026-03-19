#include "ever/browser/native_overlay_renderer.h"
#include "ever/browser/native_overlay_renderer_internal.h"

#include "ever/platform/debug_console.h"
#include "ever/platform/dll_runtime_loader.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#endif

namespace ever::browser {
void OnPresentNativeOverlay(void* swap_chain_ptr);
void OnPresentNativeOverlayFromInternalHook(void* swap_chain_ptr);
}

namespace ever::browser::native_overlay_internal {

using Microsoft::WRL::ComPtr;


std::atomic<bool> g_initialized{false};
std::atomic<bool> g_cef_initialized{false};
std::atomic<bool> g_browser_ready{false};
std::atomic<bool> g_enable_interactions_after_load_pending{false};
std::atomic<ULONGLONG> g_enable_interactions_after_load_deadline_tick{0};
std::atomic<int> g_target_width{1280};
std::atomic<int> g_target_height{720};
std::atomic<bool> g_resize_requested{false};
std::atomic<bool> g_logged_present_before_init{false};
std::atomic<bool> g_logged_present_null_swapchain{false};
std::atomic<bool> g_logged_onpaint_never_called{false};
std::atomic<uint64_t> g_onpaint_calls{0};
std::atomic<uint64_t> g_present_calls{0};
std::atomic<uint64_t> g_present_device_fail{0};
std::atomic<uint64_t> g_present_upload_fail{0};
std::atomic<uint64_t> g_present_draw_fail{0};
std::atomic<uint64_t> g_present_draw_success{0};
std::atomic<uint64_t> g_tick_calls{0};
std::atomic<uint64_t> g_message_loop_calls{0};
std::atomic<bool> g_cef_uses_multi_threaded_message_loop{false};
std::atomic<uint64_t> g_resize_dispatches{0};
std::atomic<uint64_t> g_invalidate_dispatches{0};
std::atomic<ULONGLONG> g_last_heartbeat_tick{0};
std::atomic<bool> g_dxgi_hook_installed{false};
std::atomic<bool> g_dxgi_hook_failed{false};
std::atomic<ULONGLONG> g_last_dxgi_hook_attempt_tick{0};
std::atomic<uint64_t> g_hooked_present_calls{0};
std::atomic<bool> g_logged_frame_alpha_stats{false};
std::atomic<bool> g_logged_alpha_recovery{false};
std::atomic<bool> g_last_uploaded_frame_blank{false};
std::atomic<bool> g_logged_blank_frame_recovery{false};
std::atomic<uint64_t> g_frame_size_wait_skips{0};
std::atomic<uint64_t> g_console_stage_draw_success{0};
std::atomic_flag g_overlay_draw_in_progress = ATOMIC_FLAG_INIT;
std::atomic<uint64_t> g_console_stage_reentry_skips{0};
std::atomic<uint64_t> g_console_stage_missing_swapchain{0};
std::atomic<uint64_t> g_console_stage_device_fail{0};
std::atomic<uint64_t> g_console_stage_upload_fail{0};
std::atomic<uint64_t> g_console_stage_draw_fail{0};
std::atomic<bool> g_cef_shared_texture_enabled{false};
std::atomic<bool> g_cef_shared_texture_failed{false};
std::mutex g_cef_shared_texture_mutex;
HANDLE g_cef_shared_texture_handle = nullptr;
bool g_cef_shared_texture_refresh_required = false;
uint64_t g_cef_shared_texture_sequence = 0;

std::mutex g_root_script_queue_mutex;
std::vector<std::string> g_root_script_queue;
std::atomic<uint64_t> g_root_script_queue_enqueued{0};
std::atomic<uint64_t> g_root_script_queue_executed{0};
std::atomic<uint64_t> g_root_script_queue_dropped{0};

std::mutex g_cef_message_queue_mutex;
std::vector<std::string> g_cef_message_queue;
std::atomic<uint64_t> g_cef_message_received{0};
std::atomic<uint64_t> g_cef_message_dropped{0};
std::atomic<ULONGLONG> g_fivem_deferred_dxgi_bootstrap_tick{0};
std::atomic<bool> g_fivem_deferred_dxgi_wait_logged{false};
std::atomic<bool> g_fivem_deferred_dxgi_attempt_logged{false};

void QueueCefMessageFromJs(const std::string& message) {
    constexpr size_t kMaxQueuedMessages = 256;
    if (message.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_cef_message_queue_mutex);
    if (g_cef_message_queue.size() >= kMaxQueuedMessages) {
        g_cef_message_queue.erase(g_cef_message_queue.begin());
        g_cef_message_dropped.fetch_add(1, std::memory_order_relaxed);
    }

    g_cef_message_queue.push_back(message);
    const uint64_t received = g_cef_message_received.fetch_add(1, std::memory_order_relaxed) + 1;

    std::wstring preview = Utf8ToWide(message);
    if (preview.size() > 220) {
        preview.resize(220);
        preview += L"...";
    }

    wchar_t msg[420] = {};
    swprintf_s(msg,
               L"[EVER2] CEF->C++ message received: count=%llu payload=%s",
               static_cast<unsigned long long>(received),
               preview.c_str());
    Log(msg);
}

std::mutex g_frame_mutex;
FrameBuffer g_frame;

bool TryExecuteRootScriptNow(const std::string& script) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_cef_initialized.load(std::memory_order_acquire)) {
        return false;
    }

    CefRefPtr<CefBrowser> browser = g_browser;
    if (browser == nullptr) {
        return false;
    }

    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (frame == nullptr) {
        return false;
    }

    frame->ExecuteJavaScript(script, "ever2://overlay-root", 1);
    return true;
#else
    (void)script;
    return false;
#endif
}

bool ExecuteOrQueueRootScript(const std::string& script, const wchar_t* source_tag) {
    if (script.empty()) {
        return false;
    }

    if (TryExecuteRootScriptNow(script)) {
        const uint64_t executed = g_root_script_queue_executed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (executed == 1 || (executed % 100) == 0) {
            wchar_t msg[220] = {};
            swprintf_s(msg,
                       L"[EVER2] Root script executed immediately: source=%s executedCount=%llu",
                       source_tag != nullptr ? source_tag : L"unknown",
                       static_cast<unsigned long long>(executed));
            Log(msg);
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_root_script_queue_mutex);
        constexpr size_t kMaxQueuedScripts = 256;
        if (g_root_script_queue.size() >= kMaxQueuedScripts) {
            g_root_script_queue.erase(g_root_script_queue.begin());
            const uint64_t dropped = g_root_script_queue_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
            if (dropped == 1 || (dropped % 20) == 0) {
                wchar_t msg[220] = {};
                swprintf_s(msg,
                           L"[EVER2] Root script queue overflow: dropped=%llu source=%s",
                           static_cast<unsigned long long>(dropped),
                           source_tag != nullptr ? source_tag : L"unknown");
                Log(msg);
            }
        }

        g_root_script_queue.push_back(script);
        const uint64_t enqueued = g_root_script_queue_enqueued.fetch_add(1, std::memory_order_relaxed) + 1;
        if (enqueued == 1 || (enqueued % 100) == 0) {
            wchar_t msg[220] = {};
            swprintf_s(msg,
                       L"[EVER2] Root script queued: source=%s queuedCount=%llu",
                       source_tag != nullptr ? source_tag : L"unknown",
                       static_cast<unsigned long long>(enqueued));
            Log(msg);
        }
    }

    return true;
}

void FlushQueuedRootScripts() {
    std::vector<std::string> pending;

    {
        std::lock_guard<std::mutex> lock(g_root_script_queue_mutex);
        if (g_root_script_queue.empty()) {
            return;
        }
        pending.swap(g_root_script_queue);
    }

    size_t executed_now = 0;
    size_t first_failed_index = pending.size();

    for (size_t i = 0; i < pending.size(); ++i) {
        if (!TryExecuteRootScriptNow(pending[i])) {
            first_failed_index = i;
            break;
        }
        ++executed_now;
    }

    if (executed_now > 0) {
        const uint64_t total_executed =
            g_root_script_queue_executed.fetch_add(static_cast<uint64_t>(executed_now), std::memory_order_relaxed) +
            static_cast<uint64_t>(executed_now);
        wchar_t msg[220] = {};
        swprintf_s(msg,
                   L"[EVER2] Flushed queued root scripts: executedNow=%llu totalExecuted=%llu",
                   static_cast<unsigned long long>(executed_now),
                   static_cast<unsigned long long>(total_executed));
        Log(msg);
    }

    if (first_failed_index < pending.size()) {
        std::lock_guard<std::mutex> lock(g_root_script_queue_mutex);
        g_root_script_queue.insert(g_root_script_queue.begin(),
                                   pending.begin() + static_cast<std::ptrdiff_t>(first_failed_index),
                                   pending.end());
    }
}

bool EnsureDeviceFromSwapChain(IDXGISwapChain* swap_chain);
void UpdateTargetSizeFromSwapChain(IDXGISwapChain* swap_chain);
bool UploadLatestFrame();
bool DrawOverlay(IDXGISwapChain* swap_chain);
void RenderOverlayAtConsoleStage();
extern ComPtr<IDXGISwapChain> g_swap_chain;

ComPtr<IDXGISwapChain> g_swap_chain;
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;

ComPtr<ID3D11VertexShader> g_vertex_shader;
ComPtr<ID3D11PixelShader> g_pixel_shader;
ComPtr<ID3D11InputLayout> g_input_layout;
ComPtr<ID3D11Buffer> g_vertex_buffer;
ComPtr<ID3D11Buffer> g_vertex_buffer_flipped;
ComPtr<ID3D11SamplerState> g_sampler;
ComPtr<ID3D11BlendState> g_blend_state;
ComPtr<ID3D11RasterizerState> g_rasterizer_state;

ComPtr<ID3D11Texture2D> g_overlay_texture;
ComPtr<ID3D11ShaderResourceView> g_overlay_srv;
int g_overlay_texture_width = 0;
int g_overlay_texture_height = 0;
uint64_t g_uploaded_sequence = 0;
std::atomic<uint64_t> g_draw_overlay_missing_prereq{0};
HANDLE g_opened_shared_texture_handle = nullptr;
std::atomic<bool> g_overlay_texture_from_shared_handle{false};

void SaveState(D3D11StateBackup* state) {
    if (state == nullptr || g_context == nullptr) {
        return;
    }

    g_context->OMGetRenderTargets(1, state->render_target.GetAddressOf(), state->depth_stencil.GetAddressOf());
    g_context->OMGetBlendState(state->blend_state.GetAddressOf(), state->blend_factor, &state->sample_mask);
    g_context->OMGetDepthStencilState(state->depth_state.GetAddressOf(), &state->stencil_ref);
    g_context->RSGetState(state->rasterizer_state.GetAddressOf());
    state->viewport_count = 1;
    g_context->RSGetViewports(&state->viewport_count, &state->viewport);
    g_context->IAGetInputLayout(state->input_layout.GetAddressOf());
    g_context->IAGetVertexBuffers(0, 1, state->vertex_buffer.GetAddressOf(), &state->vertex_stride, &state->vertex_offset);
    g_context->IAGetPrimitiveTopology(&state->topology);
    g_context->VSGetShader(state->vertex_shader.GetAddressOf(), nullptr, nullptr);
    g_context->PSGetShader(state->pixel_shader.GetAddressOf(), nullptr, nullptr);
    g_context->PSGetSamplers(0, 1, state->pixel_sampler.GetAddressOf());
    g_context->PSGetShaderResources(0, 1, state->pixel_srv.GetAddressOf());
}

void RestoreState(const D3D11StateBackup& state) {
    if (g_context == nullptr) {
        return;
    }

    g_context->OMSetRenderTargets(1, state.render_target.GetAddressOf(), state.depth_stencil.Get());
    g_context->OMSetBlendState(state.blend_state.Get(), state.blend_factor, state.sample_mask);
    g_context->OMSetDepthStencilState(state.depth_state.Get(), state.stencil_ref);
    g_context->RSSetState(state.rasterizer_state.Get());
    if (state.viewport_count > 0) {
        g_context->RSSetViewports(state.viewport_count, &state.viewport);
    }
    g_context->IASetInputLayout(state.input_layout.Get());
    g_context->IASetVertexBuffers(0, 1, state.vertex_buffer.GetAddressOf(), &state.vertex_stride, &state.vertex_offset);
    g_context->IASetPrimitiveTopology(state.topology);
    g_context->VSSetShader(state.vertex_shader.Get(), nullptr, 0);
    g_context->PSSetShader(state.pixel_shader.Get(), nullptr, 0);
    g_context->PSSetSamplers(0, 1, state.pixel_sampler.GetAddressOf());
    g_context->PSSetShaderResources(0, 1, state.pixel_srv.GetAddressOf());
}

void ResetD3DResources() {
    g_overlay_srv.Reset();
    g_overlay_texture.Reset();
    g_overlay_texture_width = 0;
    g_overlay_texture_height = 0;
    g_uploaded_sequence = 0;
    g_opened_shared_texture_handle = nullptr;
    g_overlay_texture_from_shared_handle.store(false, std::memory_order_release);
    g_vertex_buffer_flipped.Reset();
    g_vertex_buffer.Reset();
    g_input_layout.Reset();
    g_vertex_shader.Reset();
    g_pixel_shader.Reset();
    g_sampler.Reset();
    g_blend_state.Reset();
    g_rasterizer_state.Reset();
    g_context.Reset();
    g_device.Reset();
    g_swap_chain.Reset();
}

bool EnsureOverlayPipeline() {
    if (g_device == nullptr || g_context == nullptr) {
        Log(L"[EVER2] EnsureOverlayPipeline failed: device/context unavailable.");
        return false;
    }

    if (g_vertex_shader != nullptr && g_pixel_shader != nullptr && g_input_layout != nullptr && g_vertex_buffer != nullptr &&
        g_vertex_buffer_flipped != nullptr &&
        g_sampler != nullptr && g_blend_state != nullptr && g_rasterizer_state != nullptr) {
        return true;
    }

    static const char* kVertexShader = R"(
struct VSInput {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = input.pos;
    output.uv = input.uv;
    return output;
}
)";

    static const char* kPixelShader = R"(
Texture2D overlayTex : register(t0);
SamplerState overlaySampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    return overlayTex.Sample(overlaySampler, uv);
}
)";

    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> ps_blob;
    ComPtr<ID3DBlob> error_blob;

    HRESULT hr = D3DCompile(kVertexShader, strlen(kVertexShader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0,
                            vs_blob.GetAddressOf(), error_blob.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] Failed to compile native overlay vertex shader. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    error_blob.Reset();
    hr = D3DCompile(kPixelShader, strlen(kPixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0,
                    ps_blob.GetAddressOf(), error_blob.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] Failed to compile native overlay pixel shader. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    hr = g_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
                                      g_vertex_shader.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateVertexShader failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    hr = g_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
                                     g_pixel_shader.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreatePixelShader failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    hr = g_device->CreateInputLayout(input_layout, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                     g_input_layout.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateInputLayout failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    const OverlayVertex vertices[4] = {
        {{-1.f, +1.f, 0.f, 1.f}, {0.f, 0.f}},
        {{+1.f, +1.f, 0.f, 1.f}, {1.f, 0.f}},
        {{-1.f, -1.f, 0.f, 1.f}, {0.f, 1.f}},
        {{+1.f, -1.f, 0.f, 1.f}, {1.f, 1.f}},
    };

    const OverlayVertex flipped_vertices[4] = {
        {{-1.f, +1.f, 0.f, 1.f}, {0.f, 1.f}},
        {{+1.f, +1.f, 0.f, 1.f}, {1.f, 1.f}},
        {{-1.f, -1.f, 0.f, 1.f}, {0.f, 0.f}},
        {{+1.f, -1.f, 0.f, 1.f}, {1.f, 0.f}},
    };

    D3D11_BUFFER_DESC vb_desc = {};
    vb_desc.ByteWidth = static_cast<UINT>(sizeof(vertices));
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = {};
    vb_data.pSysMem = vertices;
    hr = g_device->CreateBuffer(&vb_desc, &vb_data, g_vertex_buffer.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateBuffer(vertex) failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_SUBRESOURCE_DATA vb_flipped_data = {};
    vb_flipped_data.pSysMem = flipped_vertices;
    hr = g_device->CreateBuffer(&vb_desc, &vb_flipped_data, g_vertex_buffer_flipped.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateBuffer(vertex-flipped) failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_device->CreateSamplerState(&sampler_desc, g_sampler.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateSamplerState failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    // CEF OSR frames use premultiplied alpha.
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_device->CreateBlendState(&blend_desc, g_blend_state.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateBlendState failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = FALSE;
    hr = g_device->CreateRasterizerState(&rasterizer_desc, g_rasterizer_state.GetAddressOf());
    if (FAILED(hr)) {
        const std::wstring message = L"[EVER2] CreateRasterizerState failed. hr=" + HrToString(hr);
        Log(message.c_str());
        return false;
    }

    Log(L"[EVER2] Native D3D overlay pipeline initialized.");

    return true;
}

} // namespace ever::browser::native_overlay_internal

namespace ever::browser {

using namespace native_overlay_internal;

bool InitializeNativeOverlayRenderer() {
    if (g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] InitializeNativeOverlayRenderer called while already active.");
        return true;
    }

    Log(L"[EVER2] InitializeNativeOverlayRenderer begin.");

    std::lock_guard<std::mutex> lock(g_frame_mutex);
    g_frame = {};

    if (!InitializeCefInProcess()) {
        Log(L"[EVER2] Native overlay renderer disabled because in-process CEF initialization failed.");
        return false;
    }

    const bool is_fivem_host = ::ever::platform::IsFiveMHostProcess();
    if (!is_fivem_host) {
        EnsureDxgiHookInstalled();
    } else {
        constexpr ULONGLONG kFiveMDeferredDxgiBootstrapDelayMs = 25000;
        const ULONGLONG bootstrap_tick = GetTickCount64() + kFiveMDeferredDxgiBootstrapDelayMs;
        g_fivem_deferred_dxgi_bootstrap_tick.store(bootstrap_tick, std::memory_order_release);
        g_fivem_deferred_dxgi_wait_logged.store(false, std::memory_order_release);
        g_fivem_deferred_dxgi_attempt_logged.store(false, std::memory_order_release);
        const std::wstring message =
            L"[EVER2] FiveM host mode: deferring internal DXGI hook bootstrap until process stabilizes (delayMs=" +
            std::to_wstring(kFiveMDeferredDxgiBootstrapDelayMs) + L").";
        Log(message.c_str());
    }
#if defined(_WIN64)
    EnsureConHostEventBridgeInstalled();
#endif

    g_initialized.store(true, std::memory_order_release);

    const std::string legacy_demo_url = BuildHtmlDataUrl(BuildLegacyGreetingHtmlDocument());
    if (!CreateNativeOverlayFrameFromPath("legacy-demo", "/re-editor/dist/")) {
        Log(L"[EVER2] Legacy demo frame request failed during initialization.");
    }

    Log(L"[EVER2] InitializeNativeOverlayRenderer success.");
    return true;
}

void TickNativeOverlayRenderer() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }

    const bool is_fivem_host = ::ever::platform::IsFiveMHostProcess();
    if (!is_fivem_host) {
        EnsureDxgiHookInstalled();
    } else if (!g_dxgi_hook_installed.load(std::memory_order_acquire)) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG deferred_tick = g_fivem_deferred_dxgi_bootstrap_tick.load(std::memory_order_acquire);
        if (deferred_tick != 0 && now >= deferred_tick) {
            if (!g_fivem_deferred_dxgi_attempt_logged.exchange(true, std::memory_order_acq_rel)) {
                Log(L"[EVER2] FiveM host mode: attempting deferred internal DXGI hook bootstrap.");
            }
            EnsureDxgiHookInstalled();
        } else if (!g_fivem_deferred_dxgi_wait_logged.exchange(true, std::memory_order_acq_rel)) {
            Log(L"[EVER2] FiveM host mode: waiting before deferred DXGI hook bootstrap.");
        }
    }
#if defined(_WIN64)
    EnsureConHostEventBridgeInstalled();
#endif

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    const uint64_t tick_call = g_tick_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    uint64_t loop_calls = g_message_loop_calls.load(std::memory_order_relaxed);
    if (!g_cef_uses_multi_threaded_message_loop.load(std::memory_order_acquire)) {
        loop_calls = g_message_loop_calls.fetch_add(1, std::memory_order_relaxed) + 1;
        CefDoMessageLoopWork();

        const ULONGLONG loop_now = GetTickCount64();
        const ULONGLONG last_input_tick = g_last_cef_input_activity_tick.load(std::memory_order_relaxed);
        const bool recently_interacting =
            last_input_tick != 0 && (loop_now - last_input_tick) <= 250 &&
            (g_cef_interactions_enabled.load(std::memory_order_acquire) ||
             g_cef_keyboard_interactions_enabled.load(std::memory_order_acquire));
        if (recently_interacting) {
            CefDoMessageLoopWork();
        }
    }

    FlushQueuedRootScripts();
    PumpPolledMouseButtonsToCef();

    if (tick_call == 1 || (tick_call % 1200) == 0) {
        wchar_t msg[220] = {};
        swprintf_s(msg,
                   L"[EVER2] TickNativeOverlayRenderer activity: tickCalls=%llu messageLoopCalls=%llu",
                   static_cast<unsigned long long>(tick_call),
                   static_cast<unsigned long long>(loop_calls));
        Log(msg);
    }

    if (g_resize_requested.exchange(false, std::memory_order_acq_rel) && g_browser != nullptr) {
        CefRefPtr<CefBrowserHost> host = g_browser->GetHost();
        if (host != nullptr) {
            host->WasResized();
            const uint64_t resized = g_resize_dispatches.fetch_add(1, std::memory_order_relaxed) + 1;
            host->Invalidate(PET_VIEW);
            const uint64_t invalidates = g_invalidate_dispatches.fetch_add(1, std::memory_order_relaxed) + 1;
            wchar_t msg[240] = {};
            swprintf_s(msg,
                       L"[EVER2] Native CEF resize/invalidate dispatched: resized=%llu invalidates=%llu",
                       static_cast<unsigned long long>(resized),
                       static_cast<unsigned long long>(invalidates));
            Log(msg);
        }
    }
#endif

    const ULONGLONG now = GetTickCount64();
    if (g_enable_interactions_after_load_pending.load(std::memory_order_acquire)) {
        const ULONGLONG deadline = g_enable_interactions_after_load_deadline_tick.load(std::memory_order_acquire);
        if (deadline != 0 && now >= deadline) {
            g_enable_interactions_after_load_pending.store(false, std::memory_order_release);
            g_enable_interactions_after_load_deadline_tick.store(0, std::memory_order_release);
            EnableCEFInteractions(true);
            Log(L"[EVER2] Auto-enabled CEF interactions after delayed post-load timer.");
        }
    }

    const ULONGLONG last = g_last_heartbeat_tick.load(std::memory_order_relaxed);
    if (last == 0 || (now - last) >= 5000) {
        g_last_heartbeat_tick.store(now, std::memory_order_relaxed);
        ValidateOverlayRuntimeInvariants();
        // TODO: This should be removed in the future
        const uint64_t presents = g_present_calls.load(std::memory_order_relaxed);
        const uint64_t draws = g_present_draw_success.load(std::memory_order_relaxed);
        const uint64_t device_fails = g_present_device_fail.load(std::memory_order_relaxed);
        const uint64_t upload_fails = g_present_upload_fail.load(std::memory_order_relaxed);
        const uint64_t draw_fails = g_present_draw_fail.load(std::memory_order_relaxed);
        const uint64_t paints = g_onpaint_calls.load(std::memory_order_relaxed);
        const uint64_t ticks = g_tick_calls.load(std::memory_order_relaxed);
        const uint64_t loops = g_message_loop_calls.load(std::memory_order_relaxed);
        const uint64_t resized = g_resize_dispatches.load(std::memory_order_relaxed);
        const uint64_t invalidates = g_invalidate_dispatches.load(std::memory_order_relaxed);
        const bool hook_installed = g_dxgi_hook_installed.load(std::memory_order_acquire);
        const uint64_t hooked_presents = g_hooked_present_calls.load(std::memory_order_relaxed);
        const bool conhost_event_installed = g_conhost_event_bridge_installed.load(std::memory_order_acquire);
        const bool conhost_event_failed = g_conhost_event_bridge_failed.load(std::memory_order_acquire);
        const uint64_t conhost_event_attempts = g_conhost_event_bridge_attempts.load(std::memory_order_relaxed);
        const uint64_t conhost_on_draw_calls = g_conhost_on_draw_callbacks.load(std::memory_order_relaxed);
        const uint64_t conhost_on_should_calls = g_conhost_on_should_callbacks.load(std::memory_order_relaxed);
        const uint64_t console_stage_success = g_console_stage_draw_success.load(std::memory_order_relaxed);
        const uint64_t console_stage_reentry = g_console_stage_reentry_skips.load(std::memory_order_relaxed);
        const uint64_t console_stage_no_swap = g_console_stage_missing_swapchain.load(std::memory_order_relaxed);
        const uint64_t console_stage_dev_fail = g_console_stage_device_fail.load(std::memory_order_relaxed);
        const uint64_t console_stage_upload_fail = g_console_stage_upload_fail.load(std::memory_order_relaxed);
        const uint64_t console_stage_draw_fail = g_console_stage_draw_fail.load(std::memory_order_relaxed);
        const uint64_t script_bypasses = g_script_present_bypasses.load(std::memory_order_relaxed);
        const uint64_t event_stage_bypasses = g_conhost_event_stage_present_bypasses.load(std::memory_order_relaxed);
        const uint64_t present_reentries = g_present_reentry_skips.load(std::memory_order_relaxed);
        const uint64_t internal_source_calls = g_present_internal_source_calls.load(std::memory_order_relaxed);
        const uint64_t invariant_anomalies = g_runtime_invariant_anomalies.load(std::memory_order_relaxed);
        const uint64_t input_conhost = g_input_mouse_consumed_conhost.load(std::memory_order_relaxed);
        const uint64_t input_forwarded_cef = g_input_mouse_forwarded_cef.load(std::memory_order_relaxed);
        const uint64_t input_consumed_cef = g_input_mouse_consumed_cef.load(std::memory_order_relaxed);
        const uint64_t input_passthrough = g_input_mouse_passthrough.load(std::memory_order_relaxed);
        const uint64_t input_kbd_conhost = g_input_keyboard_consumed_conhost.load(std::memory_order_relaxed);
        const uint64_t input_kbd_forwarded_cef = g_input_keyboard_forwarded_cef.load(std::memory_order_relaxed);
        const uint64_t input_kbd_consumed_cef = g_input_keyboard_consumed_cef.load(std::memory_order_relaxed);
        const uint64_t input_kbd_passthrough = g_input_keyboard_passthrough.load(std::memory_order_relaxed);
        const bool cef_interactions_enabled = g_cef_interactions_enabled.load(std::memory_order_acquire);
        const bool cef_keyboard_interactions_enabled = g_cef_keyboard_interactions_enabled.load(std::memory_order_acquire);
        const int target_w = g_target_width.load(std::memory_order_relaxed);
        const int target_h = g_target_height.load(std::memory_order_relaxed);
        const bool browser_ready = g_browser_ready.load(std::memory_order_acquire);
        wchar_t message[1560] = {};
        swprintf_s(
            message,
            L"[EVER2] Native overlay heartbeat: browserReady=%d hookInstalled=%d hookedPresentCalls=%llu internalSourceCalls=%llu presents=%llu draws=%llu paintCalls=%llu tickCalls=%llu loopCalls=%llu resized=%llu invalidates=%llu deviceFail=%llu uploadFail=%llu drawFail=%llu presentReentry=%llu target=%dx%d | conhostEventInstalled=%d conhostEventFailed=%d conhostEventAttempts=%llu conhostEventDrawCalls=%llu conhostEventShouldCalls=%llu consoleStageDraws=%llu consoleStageReentry=%llu consoleStageNoSwap=%llu consoleStageDeviceFail=%llu consoleStageUploadFail=%llu consoleStageDrawFail=%llu scriptBypasses=%llu eventStageBypasses=%llu invariantAnomalies=%llu | cefMouseInteractions=%d cefKeyboardInteractions=%d mouseConhost=%llu mouseForwardedCef=%llu mouseConsumedCef=%llu mousePassthrough=%llu kbdConhost=%llu kbdForwardedCef=%llu kbdConsumedCef=%llu kbdPassthrough=%llu",
            browser_ready ? 1 : 0,
            hook_installed ? 1 : 0,
            static_cast<unsigned long long>(hooked_presents),
            static_cast<unsigned long long>(internal_source_calls),
            static_cast<unsigned long long>(presents),
            static_cast<unsigned long long>(draws),
            static_cast<unsigned long long>(paints),
            static_cast<unsigned long long>(ticks),
            static_cast<unsigned long long>(loops),
            static_cast<unsigned long long>(resized),
            static_cast<unsigned long long>(invalidates),
            static_cast<unsigned long long>(device_fails),
            static_cast<unsigned long long>(upload_fails),
            static_cast<unsigned long long>(draw_fails),
            static_cast<unsigned long long>(present_reentries),
            target_w,
            target_h,
            conhost_event_installed ? 1 : 0,
            conhost_event_failed ? 1 : 0,
            static_cast<unsigned long long>(conhost_event_attempts),
            static_cast<unsigned long long>(conhost_on_draw_calls),
            static_cast<unsigned long long>(conhost_on_should_calls),
            static_cast<unsigned long long>(console_stage_success),
            static_cast<unsigned long long>(console_stage_reentry),
            static_cast<unsigned long long>(console_stage_no_swap),
            static_cast<unsigned long long>(console_stage_dev_fail),
            static_cast<unsigned long long>(console_stage_upload_fail),
            static_cast<unsigned long long>(console_stage_draw_fail),
            static_cast<unsigned long long>(script_bypasses),
            static_cast<unsigned long long>(event_stage_bypasses),
            static_cast<unsigned long long>(invariant_anomalies),
            cef_interactions_enabled ? 1 : 0,
            cef_keyboard_interactions_enabled ? 1 : 0,
            static_cast<unsigned long long>(input_conhost),
            static_cast<unsigned long long>(input_forwarded_cef),
            static_cast<unsigned long long>(input_consumed_cef),
            static_cast<unsigned long long>(input_passthrough),
            static_cast<unsigned long long>(input_kbd_conhost),
            static_cast<unsigned long long>(input_kbd_forwarded_cef),
            static_cast<unsigned long long>(input_kbd_consumed_cef),
            static_cast<unsigned long long>(input_kbd_passthrough));
        Log(message);
    }
}

void ShutdownNativeOverlayRenderer() {
    if (!g_initialized.exchange(false, std::memory_order_acq_rel)) {
        Log(L"[EVER2] ShutdownNativeOverlayRenderer called while inactive.");
        return;
    }

    Log(L"[EVER2] ShutdownNativeOverlayRenderer begin.");

    RemoveNativeInputHook();
    RemoveInstalledVTableHooks();

#if defined(_WIN64)
    RemoveConHostEventBridge();
#endif

    ShutdownCefInProcess();
    ResetD3DResources();
    g_fivem_deferred_dxgi_bootstrap_tick.store(0, std::memory_order_release);
    g_fivem_deferred_dxgi_wait_logged.store(false, std::memory_order_release);
    g_fivem_deferred_dxgi_attempt_logged.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(g_frame_mutex);
    g_frame = {};
    Log(L"[EVER2] ShutdownNativeOverlayRenderer complete.");
}

}