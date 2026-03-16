#include "ever/browser/native_overlay_renderer_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "include/cef_parser.h"
#include "include/cef_v8.h"

namespace ever::browser::native_overlay_internal {

#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
CefRefPtr<CefBrowser> g_browser;

void LogPathState(const std::filesystem::path& path, const wchar_t* label) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    const bool is_dir = exists ? std::filesystem::is_directory(path, ec) : false;
    const uintmax_t size = (!is_dir && exists) ? std::filesystem::file_size(path, ec) : 0;

    std::wstring message = L"[EVER2] Native CEF path check: ";
    message += label;
    message += L" path=";
    message += path.wstring();
    message += L" exists=";
    message += exists ? L"1" : L"0";
    message += L" isDir=";
    message += is_dir ? L"1" : L"0";
    if (exists && !is_dir) {
        message += L" size=";
        message += std::to_wstring(static_cast<unsigned long long>(size));
    }
    if (ec) {
        message += L" ec=";
        message += std::to_wstring(ec.value());
    }
    Log(message.c_str());
}

std::wstring ToWide(const CefString& value) {
    return value.ToWString();
}

constexpr char kCefBridgeProcessMessageName[] = "ever.cef.message";
constexpr char kCefBridgeConsoleFallbackPrefix[] = "__EVER2_CEF_BRIDGE__:";

bool IsPrimaryOverlayBrowser(const CefRefPtr<CefBrowser>& browser) {
    return browser != nullptr && !browser->IsPopup();
}

bool TryExtractConsoleBridgePayload(const CefString& message, std::string& out_payload) {
    const std::string msg_utf8 = message.ToString();
    if (msg_utf8.rfind(kCefBridgeConsoleFallbackPrefix, 0) != 0) {
        return false;
    }

    out_payload = msg_utf8.substr(std::strlen(kCefBridgeConsoleFallbackPrefix));
    return !out_payload.empty();
}

bool IsNoisyDevToolsConsoleMessage(cef_log_severity_t severity, const CefString& source, const CefString& message) {
    if (severity != LOGSEVERITY_DEFAULT && severity != LOGSEVERITY_INFO) {
        return false;
    }

    const std::string source_utf8 = source.ToString();
    if (source_utf8.rfind("devtools://", 0) != 0) {
        return false;
    }

    const std::string message_utf8 = message.ToString();
    return message_utf8.rfind("Main._", 0) == 0;
}

std::filesystem::path GetModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
            &module)) {
        return {};
    }

    wchar_t module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(module, module_path, MAX_PATH) == 0) {
        return {};
    }

    return std::filesystem::path(module_path).parent_path();
}

bool IsPathUnderBase(const std::filesystem::path& candidate, const std::filesystem::path& base) {
    const std::filesystem::path relative = candidate.lexically_relative(base);
    if (relative.empty()) {
        return false;
    }

    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return false;
        }
    }

    return true;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string GuessMimeType(const std::filesystem::path& path) {
    const std::string ext = ToLowerCopy(path.extension().string());
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".js" || ext == ".mjs") return "application/javascript";
    if (ext == ".css") return "text/css";
    if (ext == ".json") return "application/json";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".webp") return "image/webp";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".map") return "application/json";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

class Ever2UiResourceHandler final : public CefResourceHandler {
public:
    explicit Ever2UiResourceHandler(std::filesystem::path root)
        : root_(std::move(root)) {}

    bool ProcessRequest(CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) override {
        status_code_ = 404;
        response_data_.clear();
        response_offset_ = 0;
        mime_type_ = "text/plain";

        if (request == nullptr) {
            callback->Continue();
            return true;
        }

        CefURLParts parts;
        if (!CefParseURL(request->GetURL(), parts)) {
            callback->Continue();
            return true;
        }

        std::string path_utf8 = CefString(&parts.path).ToString();
        if (path_utf8.empty() || path_utf8 == "/") {
            path_utf8 = "/index.html";
        }

        std::filesystem::path relative;
        for (const std::filesystem::path& part : std::filesystem::path(Utf8ToWide(path_utf8))) {
            if (part.empty() || part == "/" || part == "\\" || part == ".") {
                continue;
            }
            relative /= part;
        }

        if (relative.empty()) {
            relative = L"index.html";
        }

        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::weakly_canonical(root_ / relative, ec);
        if (ec || absolute.empty()) {
            ec.clear();
            absolute = std::filesystem::absolute(root_ / relative, ec);
        }

        if (ec || absolute.empty() || !IsPathUnderBase(absolute, root_)) {
            callback->Continue();
            return true;
        }

        ec.clear();
        if (!std::filesystem::exists(absolute, ec) || !std::filesystem::is_regular_file(absolute, ec)) {
            callback->Continue();
            return true;
        }

        std::ifstream file(absolute, std::ios::binary);
        if (!file) {
            callback->Continue();
            return true;
        }

        file.seekg(0, std::ios::end);
        const std::streamsize length = file.tellg();
        file.seekg(0, std::ios::beg);
        if (length <= 0) {
            callback->Continue();
            return true;
        }

        response_data_.resize(static_cast<size_t>(length));
        if (!file.read(response_data_.data(), length)) {
            response_data_.clear();
            callback->Continue();
            return true;
        }

        status_code_ = 200;
        mime_type_ = GuessMimeType(absolute);
        callback->Continue();
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64& response_length, CefString&) override {
        response->SetStatus(status_code_);
        response->SetMimeType(mime_type_);

        CefResponse::HeaderMap headers;
        headers.emplace("access-control-allow-origin", "*");
        headers.emplace("access-control-allow-methods", "GET, OPTIONS");
        headers.emplace("cache-control", "no-cache, must-revalidate");
        response->SetHeaderMap(headers);

        response_length = (status_code_ == 200) ? static_cast<int64>(response_data_.size()) : 0;
    }

    bool ReadResponse(void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefCallback>) override {
        bytes_read = 0;
        if (status_code_ != 200 || data_out == nullptr || bytes_to_read <= 0) {
            return false;
        }

        const size_t remaining = response_data_.size() - response_offset_;
        if (remaining == 0) {
            return false;
        }

        const size_t chunk = (std::min)(remaining, static_cast<size_t>(bytes_to_read));
        memcpy(data_out, response_data_.data() + response_offset_, chunk);
        response_offset_ += chunk;
        bytes_read = static_cast<int>(chunk);
        // CEF expects true when bytes_read > 0; returning false here can drop final chunks.
        return true;
    }

    void Cancel() override {
        response_data_.clear();
        response_offset_ = 0;
    }

private:
    std::filesystem::path root_;
    int status_code_ = 404;
    std::string mime_type_ = "text/plain";
    std::vector<char> response_data_;
    size_t response_offset_ = 0;

    IMPLEMENT_REFCOUNTING(Ever2UiResourceHandler);
};

class Ever2UiSchemeHandlerFactory final : public CefSchemeHandlerFactory {
public:
    explicit Ever2UiSchemeHandlerFactory(std::filesystem::path root)
        : root_(std::move(root)) {}

    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>,
                                         CefRefPtr<CefFrame>,
                                         const CefString&,
                                         CefRefPtr<CefRequest>) override {
        return new Ever2UiResourceHandler(root_);
    }

private:
    std::filesystem::path root_;

    IMPLEMENT_REFCOUNTING(Ever2UiSchemeHandlerFactory);
};

std::wstring CompactConsoleSource(const CefString& source) {
    const std::string source_utf8 = source.ToString();
    if (source_utf8.rfind("data:text/html", 0) == 0) {
        return L"data:text/html(...truncated...)";
    }

    std::wstring out = source.ToWString();
    constexpr size_t kMaxSourceChars = 180;
    if (out.size() > kMaxSourceChars) {
        out.resize(kMaxSourceChars);
        out += L"...";
    }
    return out;
}

std::wstring CompactConsoleMessage(const CefString& message) {
    std::wstring out = message.ToWString();
    constexpr size_t kMaxMessageChars = 260;
    if (out.size() > kMaxMessageChars) {
        out.resize(kMaxMessageChars);
        out += L"...";
    }
    return out;
}

class NativeOverlayCefBridgeV8Handler final : public CefV8Handler {
public:
    bool Execute(const CefString& name,
                 CefRefPtr<CefV8Value> object,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override {
        (void)object;

        if (name != "everNativeSendCefMessage") {
            return false;
        }

        if (arguments.empty() || !arguments[0]->IsString()) {
            exception = "everNativeSendCefMessage expects a JSON string payload.";
            retval = CefV8Value::CreateBool(false);
            return true;
        }

        CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
        if (context == nullptr) {
            retval = CefV8Value::CreateBool(false);
            return true;
        }

        CefRefPtr<CefFrame> frame = context->GetFrame();
        if (frame == nullptr) {
            retval = CefV8Value::CreateBool(false);
            return true;
        }

        const std::string payload = arguments[0]->GetStringValue();
        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kCefBridgeProcessMessageName);
        message->GetArgumentList()->SetString(0, payload);

        frame->SendProcessMessage(PID_BROWSER, message);
        retval = CefV8Value::CreateBool(true);
        return true;
    }

    IMPLEMENT_REFCOUNTING(NativeOverlayCefBridgeV8Handler);
};

class NativeOverlayCefApp final : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
public:
    void OnBeforeCommandLineProcessing(
        const CefString&,
        CefRefPtr<CefCommandLine> command_line) override {
        Log(L"[EVER2] Native CEF app OnBeforeCommandLineProcessing.");
        command_line->AppendSwitch("autoplay-policy=no-user-gesture-required");
        command_line->AppendSwitch("disable-features=PaintHolding");
        command_line->AppendSwitch("allow-file-access-from-files");
        command_line->AppendSwitch("allow-universal-access-from-files");
        command_line->AppendSwitch("disable-web-security");
        Log(L"[EVER2] Native CEF command-line switches applied (host-compatible OSR).");
    }

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        Log(L"[EVER2] Native CEF GetBrowserProcessHandler called.");
        return this;
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        Log(L"[EVER2] Native CEF GetRenderProcessHandler called.");
        return this;
    }

    void OnContextInitialized() override {
        Log(L"[EVER2] Native CEF browser process context initialized.");
    }

    void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override {
        const std::wstring cmd = command_line != nullptr ? ToWide(command_line->GetCommandLineString()) : L"<null>";
        Log((L"[EVER2] Native CEF OnBeforeChildProcessLaunch cmd=" + cmd).c_str());
    }

    void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context> context) override {
        if (context == nullptr) {
            return;
        }

        CefRefPtr<CefV8Value> global = context->GetGlobal();
        if (global == nullptr) {
            return;
        }

        CefRefPtr<CefV8Value> func = CefV8Value::CreateFunction(
            "everNativeSendCefMessage",
            new NativeOverlayCefBridgeV8Handler());
        global->SetValue("everNativeSendCefMessage", func, V8_PROPERTY_ATTRIBUTE_NONE);
    }

    IMPLEMENT_REFCOUNTING(NativeOverlayCefApp);
};

CefRefPtr<CefApp> CreateNativeOverlayCefApp() {
    return new NativeOverlayCefApp();
}

class NativeOverlayRenderHandler final : public CefRenderHandler {
public:
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
        const int width = g_target_width.load(std::memory_order_relaxed);
        const int height = g_target_height.load(std::memory_order_relaxed);
        rect = CefRect(0, 0, (std::max)(width, 1), (std::max)(height, 1));
    }

    void OnPaint(
        CefRefPtr<CefBrowser>,
        PaintElementType,
        const RectList&,
        const void* buffer,
        int width,
        int height) override {
        if (buffer == nullptr || width <= 0 || height <= 0) {
            return;
        }

        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4ULL;
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        g_frame.width = width;
        g_frame.height = height;
        g_frame.sequence++;
        g_frame.pixels.resize(bytes);
        memcpy(g_frame.pixels.data(), buffer, bytes);

        const uint64_t paint_calls = g_onpaint_calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (paint_calls == 1 || (paint_calls % 120) == 0) {
            wchar_t msg[256] = {};
            swprintf_s(
                msg,
                L"[EVER2] Native CEF OnPaint call=%llu frameSeq=%llu size=%dx%d bytes=%zu",
                static_cast<unsigned long long>(paint_calls),
                static_cast<unsigned long long>(g_frame.sequence),
                width,
                height,
                bytes);
            Log(msg);
        }
    }

    void OnAcceleratedPaint(CefRefPtr<CefBrowser>,
                            PaintElementType,
                            const RectList&,
                            void* shared_handle) override {
        if (!g_cef_shared_texture_enabled.load(std::memory_order_acquire) || shared_handle == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
        g_cef_shared_texture_handle = shared_handle;
        g_cef_shared_texture_refresh_required = true;
        ++g_cef_shared_texture_sequence;
    }

    void OnAcceleratedPaint2(CefRefPtr<CefBrowser>,
                             PaintElementType,
                             const RectList&,
                             void* shared_handle,
                             bool new_texture) override {
        if (!g_cef_shared_texture_enabled.load(std::memory_order_acquire) || shared_handle == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
        g_cef_shared_texture_handle = shared_handle;
        g_cef_shared_texture_refresh_required = g_cef_shared_texture_refresh_required || new_texture;
        ++g_cef_shared_texture_sequence;
    }

    IMPLEMENT_REFCOUNTING(NativeOverlayRenderHandler);
};

class NativeOverlayClient final : public CefClient,
                                 public CefLifeSpanHandler,
                                 public CefLoadHandler,
                                 public CefDisplayHandler {
public:
    NativeOverlayClient()
        : render_handler_(new NativeOverlayRenderHandler()) {}

    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return render_handler_;
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return this;
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser>,
                                  CefRefPtr<CefFrame>,
                                  CefProcessId,
                                  CefRefPtr<CefProcessMessage> message) override {
        if (message == nullptr || message->GetName() != kCefBridgeProcessMessageName) {
            return false;
        }

        CefRefPtr<CefListValue> args = message->GetArgumentList();
        if (args == nullptr || args->GetSize() < 1 || args->GetType(0) != VTYPE_STRING) {
            return true;
        }

        QueueCefMessageFromJs(args->GetString(0));
        return true;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        if (IsPrimaryOverlayBrowser(browser)) {
            g_browser = browser;
            g_browser_ready.store(true, std::memory_order_release);
            g_resize_requested.store(true, std::memory_order_release);
            primary_browser_id_ = browser->GetIdentifier();
            Log(L"[EVER2] Native CEF primary overlay browser created.");
        } else {
            Log(L"[EVER2] Native CEF popup browser created.");
        }

#if defined(_DEBUG)
        if (!devtools_opened_ && IsPrimaryOverlayBrowser(browser)) {
            CefRefPtr<CefBrowserHost> host = browser->GetHost();
            if (host != nullptr) {
                // Set the guard before ShowDevTools in case browser creation is re-entrant.
                devtools_opened_ = true;
                CefWindowInfo devtools_window_info;
                devtools_window_info.SetAsPopup(nullptr, "EVER2 Native CEF DevTools");
                CefBrowserSettings devtools_settings;
                host->ShowDevTools(devtools_window_info, this, devtools_settings, CefPoint());
                Log(L"[EVER2] Native CEF DevTools opened (Debug build).");
            } else {
                Log(L"[EVER2] Native CEF DevTools skipped: browser host unavailable.");
            }
        }
#endif
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        const int closing_id = browser != nullptr ? browser->GetIdentifier() : -1;
        if (closing_id == primary_browser_id_) {
            g_browser_ready.store(false, std::memory_order_release);
            g_browser = nullptr;
            primary_browser_id_ = -1;
            Log(L"[EVER2] Native CEF primary overlay browser closed.");
        } else {
            Log(L"[EVER2] Native CEF popup browser closed.");
        }
    }

    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                              bool is_loading,
                              bool can_go_back,
                              bool can_go_forward) override {
        if (!IsPrimaryBrowserInstance(browser)) {
            return;
        }

        wchar_t msg[220] = {};
        swprintf_s(msg,
                   L"[EVER2] Native CEF loading state: isLoading=%d canGoBack=%d canGoForward=%d",
                   is_loading ? 1 : 0,
                   can_go_back ? 1 : 0,
                   can_go_forward ? 1 : 0);
        Log(msg);
    }

    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) override {
        if (!IsPrimaryBrowserInstance(browser) || frame == nullptr || !frame->IsMain()) {
            return;
        }
        const std::wstring url = ToWide(frame->GetURL());
        Log((L"[EVER2] Native CEF main frame load start: " + url).c_str());
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int http_status_code) override {
        if (!IsPrimaryBrowserInstance(browser) || frame == nullptr || !frame->IsMain()) {
            return;
        }
        const std::wstring url = ToWide(frame->GetURL());
        wchar_t msg[512] = {};
        _snwprintf_s(msg,
                     _countof(msg),
                     _TRUNCATE,
                     L"[EVER2] Native CEF main frame load end: status=%d url=%s",
                     http_status_code,
                     url.c_str());
        Log(msg);

        constexpr ULONGLONG kEnableDelayMs = 5000;
        const ULONGLONG deadline = GetTickCount64() + kEnableDelayMs;
        g_enable_interactions_after_load_deadline_tick.store(deadline, std::memory_order_release);
        g_enable_interactions_after_load_pending.store(true, std::memory_order_release);
        Log(L"[EVER2] CEF interactions auto-enable timer armed for 5000ms after main-frame load end.");
    }

    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode error_code,
                     const CefString& error_text,
                     const CefString& failed_url) override {
        if (!IsPrimaryBrowserInstance(browser) || frame == nullptr || !frame->IsMain()) {
            return;
        }
        const std::wstring err = ToWide(error_text);
        const std::wstring url = ToWide(failed_url);
        wchar_t msg[768] = {};
        _snwprintf_s(msg,
                     _countof(msg),
                     _TRUNCATE,
                     L"[EVER2] Native CEF main frame load error: code=%d text=%s url=%s",
                     static_cast<int>(error_code),
                     err.c_str(),
                     url.c_str());
        Log(msg);
    }

    bool OnConsoleMessage(CefRefPtr<CefBrowser>,
                          cef_log_severity_t severity,
                          const CefString& message,
                          const CefString& source,
                          int line) override {
        std::string payload;
        if (TryExtractConsoleBridgePayload(message, payload)) {
            QueueCefMessageFromJs(payload);
            return true;
        }

        if (IsNoisyDevToolsConsoleMessage(severity, source, message)) {
            return false;
        }

        const std::wstring msg = CompactConsoleMessage(message);
        const std::wstring src = CompactConsoleSource(source);
        wchar_t buffer[1024] = {};
        _snwprintf_s(buffer,
                     _countof(buffer),
                     _TRUNCATE,
                     L"[EVER2] Native CEF JS console: %s (source=%s line=%d)",
                     msg.c_str(),
                     src.c_str(),
                     line);
        Log(buffer);
        return false;
    }

private:
    bool IsPrimaryBrowserInstance(const CefRefPtr<CefBrowser>& browser) const {
        return browser != nullptr && browser->GetIdentifier() == primary_browser_id_;
    }

    CefRefPtr<NativeOverlayRenderHandler> render_handler_;
    bool devtools_opened_ = false;
    int primary_browser_id_ = -1;

    IMPLEMENT_REFCOUNTING(NativeOverlayClient);
};

CefRefPtr<CefClient> CreateNativeOverlayClient() {
    return new NativeOverlayClient();
}

bool RegisterNativeOverlayUiSchemeHandler(const std::filesystem::path& ui_root) {
    std::error_code ec;
    std::filesystem::path root = std::filesystem::weakly_canonical(ui_root, ec);
    if (ec || root.empty()) {
        ec.clear();
        root = std::filesystem::absolute(ui_root, ec);
    }

    if (ec || root.empty()) {
        Log(L"[EVER2] UI scheme registration failed: invalid UI root path.");
        return false;
    }

    if (!CefRegisterSchemeHandlerFactory("https", "ever2-ui", new Ever2UiSchemeHandlerFactory(root))) {
        Log(L"[EVER2] UI scheme registration failed: CefRegisterSchemeHandlerFactory returned false.");
        return false;
    }

    const std::wstring message = L"[EVER2] UI scheme registered: https://ever2-ui/* root=" + root.wstring();
    Log(message.c_str());
    return true;
}

CefRefPtr<CefClient> g_cef_client;
#endif

}
