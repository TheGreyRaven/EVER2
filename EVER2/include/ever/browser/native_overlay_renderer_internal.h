#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#endif

namespace ever::browser::native_overlay_internal {

using Microsoft::WRL::ComPtr;

void Log(const wchar_t* message);
void LogRateLimited(uint64_t counter, uint64_t period, const wchar_t* message);
std::wstring PtrToString(const void* value);
std::wstring HrToString(HRESULT hr);
std::string UrlEncodeDataPayload(const std::string& input);
std::string WideToUtf8(const std::wstring& value);
std::string BuildHtmlDataUrl(const std::string& html_utf8);
std::wstring Utf8ToWide(const std::string& value);
std::string JsQuote(const std::string& input);

struct FrameBuffer {
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
    std::vector<uint8_t> pixels;
};

extern std::atomic<bool> g_initialized;
extern std::atomic<bool> g_cef_initialized;
extern std::atomic<bool> g_browser_ready;
extern std::atomic<bool> g_enable_interactions_after_load_pending;
extern std::atomic<ULONGLONG> g_enable_interactions_after_load_deadline_tick;
extern std::atomic<int> g_target_width;
extern std::atomic<int> g_target_height;
extern std::atomic<bool> g_resize_requested;
extern std::atomic<bool> g_logged_present_before_init;
extern std::atomic<bool> g_logged_present_null_swapchain;
extern std::atomic<bool> g_logged_onpaint_never_called;
extern std::atomic<uint64_t> g_onpaint_calls;
extern std::atomic<uint64_t> g_present_calls;
extern std::atomic<uint64_t> g_present_device_fail;
extern std::atomic<uint64_t> g_present_upload_fail;
extern std::atomic<uint64_t> g_present_draw_fail;
extern std::atomic<uint64_t> g_present_draw_success;
extern std::atomic<uint64_t> g_tick_calls;
extern std::atomic<uint64_t> g_message_loop_calls;
extern std::atomic<bool> g_cef_uses_multi_threaded_message_loop;
extern std::atomic<uint64_t> g_resize_dispatches;
extern std::atomic<uint64_t> g_invalidate_dispatches;
extern std::atomic<ULONGLONG> g_last_heartbeat_tick;
extern std::atomic<bool> g_dxgi_hook_installed;
extern std::atomic<bool> g_dxgi_hook_failed;
extern std::atomic<ULONGLONG> g_last_dxgi_hook_attempt_tick;
extern std::atomic<uint64_t> g_hooked_present_calls;
extern std::atomic<bool> g_logged_frame_alpha_stats;
extern std::atomic<bool> g_logged_alpha_recovery;
extern std::atomic<bool> g_last_uploaded_frame_blank;
extern std::atomic<bool> g_logged_blank_frame_recovery;
extern std::atomic<uint64_t> g_frame_size_wait_skips;
extern std::atomic<uint64_t> g_console_stage_draw_success;
extern std::atomic_flag g_overlay_draw_in_progress;
extern std::atomic<uint64_t> g_console_stage_reentry_skips;
extern std::atomic<uint64_t> g_console_stage_missing_swapchain;
extern std::atomic<uint64_t> g_console_stage_device_fail;
extern std::atomic<uint64_t> g_console_stage_upload_fail;
extern std::atomic<uint64_t> g_console_stage_draw_fail;
extern std::atomic<uint64_t> g_present_reentry_skips;
extern std::atomic<uint64_t> g_present_internal_source_calls;
extern std::atomic<uint64_t> g_runtime_invariant_anomalies;
extern std::atomic<bool> g_logged_internal_source_without_hook;
extern std::atomic<bool> g_logged_browser_ready_without_cef;
extern std::atomic<bool> g_logged_script_bypass_without_hook;
extern std::atomic<bool> g_logged_conhost_bypass_without_bridge;
extern std::atomic<bool> g_cef_shared_texture_enabled;
extern std::atomic<bool> g_cef_shared_texture_failed;
extern std::atomic<uint64_t> g_input_mouse_consumed_conhost;
extern std::atomic<uint64_t> g_input_mouse_forwarded_cef;
extern std::atomic<uint64_t> g_input_mouse_consumed_cef;
extern std::atomic<uint64_t> g_input_mouse_passthrough;
extern std::atomic<uint64_t> g_input_keyboard_consumed_conhost;
extern std::atomic<uint64_t> g_input_keyboard_forwarded_cef;
extern std::atomic<uint64_t> g_input_keyboard_consumed_cef;
extern std::atomic<uint64_t> g_input_keyboard_passthrough;
extern std::atomic<bool> g_cef_interactions_enabled;
extern std::atomic<bool> g_cef_keyboard_interactions_enabled;
extern std::atomic<ULONGLONG> g_last_cef_input_activity_tick;
extern std::mutex g_input_hook_mutex;
extern HWND g_input_hook_window;
extern std::atomic<bool> g_cef_tracking_mouse_leave;

extern std::mutex g_cef_shared_texture_mutex;
extern HANDLE g_cef_shared_texture_handle;
extern bool g_cef_shared_texture_refresh_required;
extern uint64_t g_cef_shared_texture_sequence;

extern std::mutex g_frame_mutex;
extern FrameBuffer g_frame;

extern std::mutex g_root_script_queue_mutex;
extern std::vector<std::string> g_root_script_queue;

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
extern CefRefPtr<CefBrowser> g_browser;
extern CefRefPtr<CefClient> g_cef_client;
void LogPathState(const std::filesystem::path& path, const wchar_t* label);
CefRefPtr<CefApp> CreateNativeOverlayCefApp();
CefRefPtr<CefClient> CreateNativeOverlayClient();
bool RegisterNativeOverlayUiSchemeHandler(const std::filesystem::path& ui_root);
#endif

extern ComPtr<IDXGISwapChain> g_swap_chain;
extern ComPtr<ID3D11Device> g_device;
extern ComPtr<ID3D11DeviceContext> g_context;

extern ComPtr<ID3D11VertexShader> g_vertex_shader;
extern ComPtr<ID3D11PixelShader> g_pixel_shader;
extern ComPtr<ID3D11InputLayout> g_input_layout;
extern ComPtr<ID3D11Buffer> g_vertex_buffer;
extern ComPtr<ID3D11Buffer> g_vertex_buffer_flipped;
extern ComPtr<ID3D11SamplerState> g_sampler;
extern ComPtr<ID3D11BlendState> g_blend_state;
extern ComPtr<ID3D11RasterizerState> g_rasterizer_state;

extern ComPtr<ID3D11Texture2D> g_overlay_texture;
extern ComPtr<ID3D11ShaderResourceView> g_overlay_srv;
extern int g_overlay_texture_width;
extern int g_overlay_texture_height;
extern uint64_t g_uploaded_sequence;
extern std::atomic<uint64_t> g_draw_overlay_missing_prereq;
extern HANDLE g_opened_shared_texture_handle;
extern std::atomic<bool> g_overlay_texture_from_shared_handle;

struct OverlayVertex {
    float position[4];
    float uv[2];
};

struct D3D11StateBackup {
    ComPtr<ID3D11RenderTargetView> render_target;
    ComPtr<ID3D11DepthStencilView> depth_stencil;
    ComPtr<ID3D11BlendState> blend_state;
    FLOAT blend_factor[4] = {0.f, 0.f, 0.f, 0.f};
    UINT sample_mask = 0xffffffffu;
    ComPtr<ID3D11DepthStencilState> depth_state;
    UINT stencil_ref = 0;
    ComPtr<ID3D11RasterizerState> rasterizer_state;
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    ComPtr<ID3D11InputLayout> input_layout;
    ComPtr<ID3D11Buffer> vertex_buffer;
    UINT vertex_stride = 0;
    UINT vertex_offset = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11SamplerState> pixel_sampler;
    ComPtr<ID3D11ShaderResourceView> pixel_srv;
};

void SaveState(D3D11StateBackup* state);
void RestoreState(const D3D11StateBackup& state);
void ResetD3DResources();
bool EnsureOverlayPipeline();
bool ExecuteOrQueueRootScript(const std::string& script, const wchar_t* source_tag);
void RemoveInstalledVTableHooks();
void EnsureDxgiHookInstalled();
void ValidateOverlayRuntimeInvariants();
bool InitializeCefInProcess();
void ShutdownCefInProcess();
std::string BuildRootFrameManagerHtml();
std::string BuildLegacyGreetingHtmlDocument();

bool EnsureDeviceFromSwapChain(IDXGISwapChain* swap_chain);
void UpdateTargetSizeFromSwapChain(IDXGISwapChain* swap_chain);
void EnsureNativeInputHookFromSwapChain(IDXGISwapChain* swap_chain);
void RemoveNativeInputHook();
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
void MarkCefInputActivity();
bool IsMouseMessageForRouting(UINT msg);
bool IsKeyboardMessageForRouting(UINT msg);
bool ForwardMouseMessageToCef(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
bool ForwardKeyboardMessageToCef(UINT msg, WPARAM wparam, LPARAM lparam);
#endif
void PumpPolledMouseButtonsToCef();
bool UploadLatestFrame();
bool DrawOverlay(IDXGISwapChain* swap_chain);
void RenderOverlayAtConsoleStage();

enum class OverlayPresentSource : uint8_t {
    ScriptHook = 0,
    InternalDxgiHook = 1,
};

extern std::atomic<uint64_t> g_script_present_bypasses;
bool ShouldBypassSourcePresentDraw(OverlayPresentSource source);

extern std::atomic<bool> g_conhost_event_bridge_installed;
extern std::atomic<bool> g_conhost_event_bridge_failed;
extern std::atomic<uint64_t> g_conhost_event_bridge_attempts;
extern std::atomic<ULONGLONG> g_conhost_event_bridge_last_try_tick;
extern std::atomic<uint64_t> g_conhost_on_draw_callbacks;
extern std::atomic<uint64_t> g_conhost_on_should_callbacks;
extern std::atomic<bool> g_conhost_console_open;
extern std::atomic<ULONGLONG> g_conhost_event_stage_draw_tick;
extern std::atomic<uint64_t> g_conhost_event_stage_present_bypasses;

extern std::mutex g_cef_message_queue_mutex;
extern std::vector<std::string> g_cef_message_queue;
extern std::atomic<uint64_t> g_cef_message_received;
extern std::atomic<uint64_t> g_cef_message_dropped;

void QueueCefMessageFromJs(const std::string& message);

bool EnsureConHostEventBridgeInstalled();
void RemoveConHostEventBridge();
bool IsConHostEventStageActive();
bool TryEnterOverlayDrawRegion();
void LeaveOverlayDrawRegion();

}
