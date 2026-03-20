#include "ever/browser/native_overlay_renderer.h"
#include "ever/browser/native_overlay_renderer_internal.h"

#include <filesystem>

namespace {

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

std::filesystem::path NormalizeRootRelativePath(const std::filesystem::path& value) {
    std::filesystem::path out;
    for (const std::filesystem::path& part : value) {
        if (part.empty() || part == "/" || part == "\\" || part == ".") {
            continue;
        }
        out /= part;
    }
    return out;
}

std::string EncodeFileUrlPath(const std::string& path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() * 3);

    for (const unsigned char ch : path) {
        const bool unreserved =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/' || ch == ':';
        if (unreserved) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(kHex[(ch >> 4) & 0x0F]);
            out.push_back(kHex[ch & 0x0F]);
        }
    }

    return out;
}

std::string BuildFileUrlFromAbsolutePath(const std::filesystem::path& absolute_path) {
    std::string generic = absolute_path.generic_string();
    if (generic.empty()) {
        return std::string();
    }

    if (generic.rfind("//", 0) == 0) {
        return std::string("file:") + EncodeFileUrlPath(generic);
    }

    if (generic.front() != '/') {
        generic.insert(generic.begin(), '/');
    }

    return std::string("file://") + EncodeFileUrlPath(generic);
}

std::string BuildUiUrlFromRelativePath(const std::filesystem::path& relative_path) {
    std::string path = relative_path.generic_string();
    if (path.empty()) {
        return std::string();
    }

    if (path.front() == '/') {
        path.erase(path.begin());
    }

    return std::string("https://ever2-ui/") + EncodeFileUrlPath(path);
}

}

namespace ever::browser {

using namespace native_overlay_internal;

bool IsNativeOverlayRendererActive() {
    return g_initialized.load(std::memory_order_acquire);
}

void EnableCEFInteractions(bool enabled) {
    const bool previous = g_cef_interactions_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled) {
        return;
    }

    if (!enabled) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
        CefRefPtr<CefBrowser> browser = g_browser;
        if (browser != nullptr) {
            CefRefPtr<CefBrowserHost> host = browser->GetHost();
            if (host != nullptr) {
                host->SendCaptureLostEvent();
                host->SetFocus(false);
            }
        }
#endif
        g_cef_browser_focused.store(false, std::memory_order_release);
    }

    wchar_t line[160] = {};
    swprintf_s(line,
               L"[EVER2] CEF interactions %s.",
               enabled ? L"ENABLED" : L"DISABLED");
    Log(line);
}

bool AreCEFInteractionsEnabled() {
    return g_cef_interactions_enabled.load(std::memory_order_acquire);
}

void EnableCEFKeyboardInteractions(bool enabled) {
    const bool previous = g_cef_keyboard_interactions_enabled.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled) {
        return;
    }

    wchar_t line[170] = {};
    swprintf_s(line,
               L"[EVER2] CEF keyboard interactions %s.",
               enabled ? L"ENABLED" : L"DISABLED");
    Log(line);
}

bool AreCEFKeyboardInteractionsEnabled() {
    return g_cef_keyboard_interactions_enabled.load(std::memory_order_acquire);
}

bool ExecuteNativeOverlayRootScript(const std::string& javascript) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] ExecuteNativeOverlayRootScript rejected: renderer is inactive.");
        return false;
    }

    const bool accepted = ExecuteOrQueueRootScript(javascript, L"ExecuteNativeOverlayRootScript");
    if (!accepted) {
        Log(L"[EVER2] ExecuteNativeOverlayRootScript rejected: empty script.");
        return false;
    }

    return true;
#else
    (void)javascript;
    Log(L"[EVER2] ExecuteNativeOverlayRootScript unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool CreateNativeOverlayFrameFromUrl(const std::string& frame_name, const std::string& frame_url) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromUrl rejected: renderer is inactive.");
        return false;
    }

    if (frame_name.empty() || frame_url.empty()) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromUrl rejected: frame name or URL is empty.");
        return false;
    }

    const std::string script =
        std::string("window.everCreateFrameFromUrl(") + JsQuote(frame_name) + "," + JsQuote(frame_url) + ");";

    if (!ExecuteOrQueueRootScript(script, L"CreateNativeOverlayFrameFromUrl")) {
        return false;
    }

    const std::wstring message = L"[EVER2] Frame URL create/update requested: name=" +
        Utf8ToWide(frame_name) + L" url=" + Utf8ToWide(frame_url);
    Log(message.c_str());
    return true;
#else
    (void)frame_name;
    (void)frame_url;
    Log(L"[EVER2] CreateNativeOverlayFrameFromUrl unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool CreateNativeOverlayFrameFromHtml(const std::string& frame_name, const std::string& html_document) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromHtml rejected: renderer is inactive.");
        return false;
    }

    if (frame_name.empty()) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromHtml rejected: frame name is empty.");
        return false;
    }

    const std::string script =
        std::string("window.everCreateFrameFromHtml(") + JsQuote(frame_name) + "," + JsQuote(html_document) + ");";

    if (!ExecuteOrQueueRootScript(script, L"CreateNativeOverlayFrameFromHtml")) {
        return false;
    }

    const std::wstring message = L"[EVER2] Frame HTML create/update requested: name=" +
        Utf8ToWide(frame_name) + L" htmlBytes=" + std::to_wstring(static_cast<unsigned long long>(html_document.size()));
    Log(message.c_str());
    return true;
#else
    (void)frame_name;
    (void)html_document;
    Log(L"[EVER2] CreateNativeOverlayFrameFromHtml unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool CreateNativeOverlayFrameFromPath(const std::string& frame_name,
                                      const std::string& overlay_path,
                                               const std::string& entry_document_relative_path) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: renderer is inactive.");
        return false;
    }

    if (frame_name.empty() || overlay_path.empty()) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: frame name or overlay path is empty.");
        return false;
    }

    const std::string entry_path_value =
        entry_document_relative_path.empty() ? std::string("index.html") : entry_document_relative_path;

    std::error_code ec;
    std::filesystem::path base_path;
    const std::filesystem::path input_path = std::filesystem::path(Utf8ToWide(overlay_path));

    const bool force_module_relative =
        !overlay_path.empty() && (overlay_path.front() == '/' || overlay_path.front() == '\\');

    if (!force_module_relative && input_path.is_absolute()) {
        base_path = std::filesystem::weakly_canonical(input_path, ec);
        if (ec || base_path.empty()) {
            ec.clear();
            base_path = std::filesystem::absolute(input_path, ec);
        }
    } else {
        const std::filesystem::path module_dir = GetModuleDirectory();
        if (module_dir.empty()) {
            Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: module directory unavailable.");
            return false;
        }

        std::filesystem::path relative = input_path;
        if (force_module_relative) {
            relative = NormalizeRootRelativePath(relative);
        }

        const std::filesystem::path root_base = module_dir / L"EVER2" / L"ui";
        base_path = std::filesystem::weakly_canonical(root_base / relative, ec);
        if (ec || base_path.empty()) {
            ec.clear();
            base_path = std::filesystem::absolute(root_base / relative, ec);
        }

        if (ec || base_path.empty() || !IsPathUnderBase(base_path, root_base)) {
            Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: overlay path resolves outside module-relative EVER2/ui root.");
            return false;
        }
    }

    if (ec || base_path.empty()) {
        const std::wstring message = L"[EVER2] CreateNativeOverlayFrameFromPath rejected: overlay path is invalid. path=" +
            Utf8ToWide(overlay_path);
        Log(message.c_str());
        return false;
    }

    ec.clear();
    if (!std::filesystem::exists(base_path, ec) || !std::filesystem::is_directory(base_path, ec)) {
        const std::wstring message = L"[EVER2] CreateNativeOverlayFrameFromPath rejected: base folder does not exist. path=" +
            base_path.wstring();
        Log(message.c_str());
        return false;
    }

    const std::filesystem::path entry_relative = std::filesystem::path(Utf8ToWide(entry_path_value));
    if (entry_relative.empty() || entry_relative.is_absolute()) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: entry path must be a relative file path.");
        return false;
    }

    ec.clear();
    std::filesystem::path entry_path = std::filesystem::weakly_canonical(base_path / entry_relative, ec);
    if (ec || entry_path.empty()) {
        ec.clear();
        entry_path = std::filesystem::absolute(base_path / entry_relative, ec);
    }

    if (ec || entry_path.empty() || !IsPathUnderBase(entry_path, base_path)) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: entry path resolves outside the selected base folder.");
        return false;
    }

    ec.clear();
    if (!std::filesystem::exists(entry_path, ec) || !std::filesystem::is_regular_file(entry_path, ec)) {
        const std::wstring message = L"[EVER2] CreateNativeOverlayFrameFromPath rejected: entry file does not exist. path=" +
            entry_path.wstring();
        Log(message.c_str());
        return false;
    }

    std::string frame_url;
    const std::filesystem::path module_dir = GetModuleDirectory();
    if (!module_dir.empty()) {
        std::error_code root_ec;
        std::filesystem::path module_ui_root = std::filesystem::weakly_canonical(module_dir / L"EVER2" / L"ui", root_ec);
        if (root_ec || module_ui_root.empty()) {
            root_ec.clear();
            module_ui_root = std::filesystem::absolute(module_dir / L"EVER2" / L"ui", root_ec);
        }

        if (!root_ec && !module_ui_root.empty() && IsPathUnderBase(entry_path, module_ui_root)) {
            const std::filesystem::path relative_entry = entry_path.lexically_relative(module_ui_root);
            frame_url = BuildUiUrlFromRelativePath(relative_entry);
        }
    }

    if (frame_url.empty()) {
        frame_url = BuildFileUrlFromAbsolutePath(entry_path);
    }

    if (frame_url.empty()) {
        Log(L"[EVER2] CreateNativeOverlayFrameFromPath rejected: failed to build URL from entry path.");
        return false;
    }

    if (!CreateNativeOverlayFrameFromUrl(frame_name, frame_url)) {
        return false;
    }

    const std::wstring message = L"[EVER2] Frame path create/update requested: name=" +
        Utf8ToWide(frame_name) + L" base=" + base_path.wstring() + L" entry=" + entry_path.wstring();
    Log(message.c_str());
    return true;
#else
    (void)frame_name;
    (void)overlay_path;
    (void)entry_document_relative_path;
    Log(L"[EVER2] CreateNativeOverlayFrameFromPath unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool DestroyNativeOverlayFrame(const std::string& frame_name) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] DestroyNativeOverlayFrame rejected: renderer is inactive.");
        return false;
    }

    if (frame_name.empty()) {
        Log(L"[EVER2] DestroyNativeOverlayFrame rejected: frame name is empty.");
        return false;
    }

    const std::string script = std::string("window.everDestroyFrame(") + JsQuote(frame_name) + ");";
    if (!ExecuteOrQueueRootScript(script, L"DestroyNativeOverlayFrame")) {
        return false;
    }

    const std::wstring message = L"[EVER2] Frame destroy requested: name=" + Utf8ToWide(frame_name);
    Log(message.c_str());
    return true;
#else
    (void)frame_name;
    Log(L"[EVER2] DestroyNativeOverlayFrame unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool PostNativeOverlayFrameMessage(const std::string& frame_name, const std::string& json_payload) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] PostNativeOverlayFrameMessage rejected: renderer is inactive.");
        return false;
    }

    if (frame_name.empty()) {
        Log(L"[EVER2] PostNativeOverlayFrameMessage rejected: frame name is empty.");
        return false;
    }

    const std::string script =
        std::string("window.everPostFrameMessage(") + JsQuote(frame_name) + "," + JsQuote(json_payload) + ");";

    if (!ExecuteOrQueueRootScript(script, L"PostNativeOverlayFrameMessage")) {
        return false;
    }

    const std::wstring message = L"[EVER2] Frame message requested: name=" + Utf8ToWide(frame_name) +
        L" payloadBytes=" + std::to_wstring(static_cast<unsigned long long>(json_payload.size()));
    Log(message.c_str());
    return true;
#else
    (void)frame_name;
    (void)json_payload;
    Log(L"[EVER2] PostNativeOverlayFrameMessage unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool SendCefMessage(const std::string& json_payload) {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] SendCefMessage rejected: renderer is inactive.");
        return false;
    }

    if (json_payload.empty()) {
        Log(L"[EVER2] SendCefMessage rejected: payload is empty.");
        return false;
    }

    const std::string script = std::string("window.everBroadcastCefMessage(") + JsQuote(json_payload) + ");";
    if (!ExecuteOrQueueRootScript(script, L"SendCefMessage")) {
        return false;
    }

    return true;
#else
    (void)json_payload;
    Log(L"[EVER2] SendCefMessage unavailable: CEF disabled for this build.");
    return false;
#endif
}

bool PollCefMessage(std::string& out_json_payload) {
    out_json_payload.clear();

    std::lock_guard<std::mutex> lock(g_cef_message_queue_mutex);
    if (g_cef_message_queue.empty()) {
        return false;
    }

    out_json_payload = std::move(g_cef_message_queue.front());
    g_cef_message_queue.erase(g_cef_message_queue.begin());
    return true;
}

}
