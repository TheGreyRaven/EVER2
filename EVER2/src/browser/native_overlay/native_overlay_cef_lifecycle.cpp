#include "ever/browser/native_overlay_renderer_internal.h"

#include "ever/platform/dll_runtime_loader.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace ever::browser::native_overlay_internal {

namespace {

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

std::wstring GetModulePath(HMODULE module) {
    if (module == nullptr) {
        return L"<null>";
    }

    wchar_t path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"<unavailable>";
    }

    return std::wstring(path, len);
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

class ScopedCurrentDirectory {
public:
    explicit ScopedCurrentDirectory(const std::filesystem::path& next) {
        wchar_t cwd[MAX_PATH] = {};
        if (GetCurrentDirectoryW(MAX_PATH, cwd) != 0) {
            previous_ = cwd;
            has_previous_ = true;
        }

        if (!next.empty()) {
            SetCurrentDirectoryW(next.c_str());
        }
    }

    ~ScopedCurrentDirectory() {
        if (has_previous_) {
            SetCurrentDirectoryW(previous_.c_str());
        }
    }

private:
    bool has_previous_ = false;
    std::wstring previous_;
};

} // namespace

bool InitializeCefInProcess() {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (g_cef_initialized.load(std::memory_order_acquire)) {
        Log(L"[EVER2] InitializeCefInProcess skipped: already initialized.");
        return true;
    }

    const std::filesystem::path module_dir = GetModuleDirectory();
    if (module_dir.empty()) {
        Log(L"[EVER2] Native CEF init failed: module directory unavailable.");
        return false;
    }

    const std::filesystem::path runtime_dir = module_dir / L"EVER2" / L"cef_runtime";
    const std::filesystem::path resources_dir = runtime_dir / L"cef_resources";
    const std::filesystem::path locales_dir = resources_dir / L"locales";
    const std::filesystem::path cef_log_file = module_dir / L"EVER2-native-cef.log";
    Log((L"[EVER2] Native CEF init paths: module=" + module_dir.wstring() +
         L" runtime=" + runtime_dir.wstring() +
         L" resources=" + resources_dir.wstring())
            .c_str());

    LogPathState(runtime_dir, L"runtime_dir");
    LogPathState(resources_dir, L"resources_dir");
    LogPathState(locales_dir, L"locales_dir");
    LogPathState(runtime_dir / L"libcef.dll", L"libcef");
    LogPathState(runtime_dir / L"chrome_elf.dll", L"chrome_elf");
    LogPathState(runtime_dir / L"icudtl.dat", L"icudtl");
    LogPathState(runtime_dir / L"resources.pak", L"resources_pak");
    LogPathState(runtime_dir / L"libEGL.dll", L"libEGL");
    LogPathState(runtime_dir / L"libGLESv2.dll", L"libGLESv2");
    LogPathState(runtime_dir / L"vk_swiftshader.dll", L"vk_swiftshader");
    LogPathState(runtime_dir / L"v8_context_snapshot.bin", L"v8_context_snapshot");
    LogPathState(runtime_dir / L"snapshot_blob.bin", L"snapshot_blob");
    LogPathState(resources_dir / L"locales" / L"en-US.pak", L"locale_en_us");
    Log((L"[EVER2] Native CEF log file target=" + cef_log_file.wstring()).c_str());

    if (!std::filesystem::exists(runtime_dir / L"libcef.dll")) {
        Log(L"[EVER2] Native CEF init failed: cef_runtime/libcef.dll missing.");
        return false;
    }

    const bool is_fivem_host = ::ever::platform::IsFiveMHostProcess();
    Log((L"[EVER2] Native CEF host mode detection: isFiveMHost=" + std::to_wstring(is_fivem_host ? 1 : 0)).c_str());

    HMODULE loaded_cef = nullptr;
    if (is_fivem_host) {
        loaded_cef = GetModuleHandleW(L"libcef.dll");
        if (loaded_cef == nullptr) {
            Log(L"[EVER2] Native CEF init failed: FiveM host detected but libcef.dll is not loaded yet.");
            return false;
        }

        Log((L"[EVER2] Native CEF using host-loaded FiveM libcef.dll module=" + PtrToString(loaded_cef) +
             L" path=" + GetModulePath(loaded_cef))
                .c_str());
    } else {
        loaded_cef = LoadLibraryExW((runtime_dir / L"libcef.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    if (loaded_cef == nullptr) {
        const DWORD err = GetLastError();
        const std::wstring message =
            L"[EVER2] Native CEF init failed: could not load libcef.dll (LoadLibraryExW). error=" +
            std::to_wstring(err) + L" text=" + GetLastErrorText(err);
        Log(message.c_str());
        return false;
    }

    Log((L"[EVER2] Native CEF loaded libcef.dll module=" + PtrToString(loaded_cef) +
         L" path=" + GetModulePath(loaded_cef))
            .c_str());

    ScopedCurrentDirectory scoped_cwd(runtime_dir);

    CefMainArgs main_args(GetModuleHandleW(nullptr));
    CefRefPtr<CefApp> app = CreateNativeOverlayCefApp();
    const int subprocess_exit_code = CefExecuteProcess(main_args, app, nullptr);
    if (subprocess_exit_code >= 0) {
        wchar_t message[192] = {};
        swprintf_s(message, L"[EVER2] CefExecuteProcess returned %d (child/subprocess path).", subprocess_exit_code);
        Log(message);
        return false;
    }

    Log(L"[EVER2] CefExecuteProcess indicates browser process path (code < 0). Continuing init.");

    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = is_fivem_host;
    settings.external_message_pump = false;
    CefString(&settings.log_file) = cef_log_file.wstring();
    settings.log_severity = LOGSEVERITY_DEFAULT;
    CefString(&settings.locales_dir_path) = (resources_dir / L"locales").wstring();
    CefString(&settings.resources_dir_path) = resources_dir.wstring();
    CefString(&settings.cache_path) = (runtime_dir / L"cache" / L"native").wstring();
    CefString(&settings.root_cache_path) = (runtime_dir / L"cache").wstring();

    g_cef_uses_multi_threaded_message_loop.store(is_fivem_host, std::memory_order_release);

    Log((L"[EVER2] Native CEF settings: logFile=" + cef_log_file.wstring() +
         L" logSeverity=DEFAULT multiThreadedMessageLoop=" + std::to_wstring(is_fivem_host ? 1 : 0) +
         L" localesPath=" + locales_dir.wstring() +
         L" resourcesPath=" + resources_dir.wstring() +
         L" cachePath=" + (runtime_dir / L"cache" / L"native").wstring() +
         L" rootCachePath=" + (runtime_dir / L"cache").wstring())
            .c_str());

    if (!CefInitialize(main_args, settings, app, nullptr)) {
        Log(L"[EVER2] Native CEF init failed: CefInitialize returned false.");
        return false;
    }

    Log(L"[EVER2] CefInitialize succeeded for native renderer.");

    const std::filesystem::path ui_root = module_dir / L"EVER2" / L"ui";
    if (!RegisterNativeOverlayUiSchemeHandler(ui_root)) {
        Log(L"[EVER2] WARNING: ever2-ui scheme handler registration failed; module-relative UI URLs may not load.");
    }

    if (!RegisterNativeOverlayAssetSchemeHandler()) {
        Log(L"[EVER2] WARNING: ever2-asset scheme handler registration failed; local image preview URLs may not load.");
    }

    g_cef_client = CreateNativeOverlayClient();

    CefWindowInfo window_info;
    window_info.SetAsWindowless(nullptr);
    window_info.shared_texture_enabled = is_fivem_host;
    g_cef_shared_texture_enabled.store(is_fivem_host, std::memory_order_release);
    g_cef_shared_texture_failed.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
        g_cef_shared_texture_handle = nullptr;
        g_cef_shared_texture_refresh_required = false;
        g_cef_shared_texture_sequence = 0;
    }

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = is_fivem_host ? 240 : 90;
    browser_settings.background_color = CefColorSetARGB(0, 0, 0, 0);

    const std::string html_document = BuildRootFrameManagerHtml();
    const std::string html = BuildHtmlDataUrl(html_document);

    if (!CefBrowserHost::CreateBrowser(window_info, g_cef_client, html, browser_settings, nullptr, nullptr)) {
        Log(L"[EVER2] Native CEF init failed: CreateBrowser returned false.");
        CefShutdown();
        g_cef_client = nullptr;
        return false;
    }

    g_cef_initialized.store(true, std::memory_order_release);
    Log(L"[EVER2] Native CEF initialized in-process and browser create was requested.");

    ExecuteOrQueueRootScript(
        "console.log('[EVER2] Root frame manager bootstrap ready.');",
        L"InitializeCefInProcess");

    return true;
#else
    Log(L"[EVER2] Native CEF init unavailable: EVER_NATIVE_ENABLE_CEF disabled for this build.");
    return false;
#endif
}

void ShutdownCefInProcess() {
#if defined(_WIN64) && EVER_NATIVE_ENABLE_CEF
    if (!g_cef_initialized.load(std::memory_order_acquire)) {
        return;
    }

    if (g_browser != nullptr) {
        CefRefPtr<CefBrowserHost> host = g_browser->GetHost();
        if (host != nullptr) {
            host->CloseBrowser(true);
        }
        g_browser = nullptr;
    }

    g_cef_client = nullptr;
    CefShutdown();
    g_cef_initialized.store(false, std::memory_order_release);
    g_cef_uses_multi_threaded_message_loop.store(false, std::memory_order_release);
    g_cef_shared_texture_enabled.store(false, std::memory_order_release);
    g_cef_shared_texture_failed.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_cef_shared_texture_mutex);
        g_cef_shared_texture_handle = nullptr;
        g_cef_shared_texture_refresh_required = false;
        g_cef_shared_texture_sequence = 0;
    }
    g_browser_ready.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_root_script_queue_mutex);
        g_root_script_queue.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_cef_message_queue_mutex);
        g_cef_message_queue.clear();
    }

    Log(L"[EVER2] Native CEF shutdown complete.");
#endif
}

}
