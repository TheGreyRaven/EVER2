#include "ever/platform/debug_console.h"

#include <windows.h>

#include <filesystem>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

bool g_console_initialized = false;
std::filesystem::path g_log_path;

// TODO: Implement log rotation and overall reduce log noise & addlog level settings

std::filesystem::path GetModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
            &module)) {
        return {};
    }

    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        return {};
    }

    return std::filesystem::path(path).parent_path();
}

void AppendLogLine(const wchar_t* message) {
    if (message == nullptr) {
        return;
    }

    if (g_log_path.empty()) {
        const std::filesystem::path module_directory = GetModuleDirectory();
        if (!module_directory.empty()) {
            g_log_path = module_directory / L"EVER2-debug.log";
        }
    }

    if (g_log_path.empty()) {
        return;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, g_log_path.c_str(), L"a+, ccs=UTF-8") == 0 && file != nullptr) {
        fwprintf(file, L"%s\n", message);
        fclose(file);
    }
}

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

void AppendWin32Failure(const wchar_t* prefix, DWORD error_code) {
    const std::wstring message = std::wstring(prefix) +
        L" error=" + std::to_wstring(error_code) +
        L" (" + GetLastErrorText(error_code) + L")";
    AppendLogLine(message.c_str());
}

}

namespace ever::platform {

void InitializeDebugConsole() {
#ifdef _DEBUG
    if (g_log_path.empty()) {
        const std::filesystem::path module_directory = GetModuleDirectory();
        if (!module_directory.empty()) {
            g_log_path = module_directory / L"EVER2-debug.log";
        }
    }
    AppendLogLine(L"[EVER2] InitializeDebugConsole called.");
#endif

#ifdef _DEBUG
    if (g_console_initialized) {
        AppendLogLine(L"[EVER2] InitializeDebugConsole skipped: console already initialized.");
        return;
    }

    const bool allocated = AllocConsole() != FALSE;
    if (!allocated) {
        const DWORD alloc_error = GetLastError();
        AppendWin32Failure(L"[EVER2] AllocConsole failed.", alloc_error);
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            const DWORD attach_error = GetLastError();
            AppendWin32Failure(L"[EVER2] AttachConsole(ATTACH_PARENT_PROCESS) failed.", attach_error);
            AppendLogLine(L"[EVER2] Debug console unavailable; continuing with file-only logging.");
            return;
        }
        AppendLogLine(L"[EVER2] Attached to parent console after AllocConsole failure.");
    } else {
        AppendLogLine(L"[EVER2] AllocConsole succeeded.");
    }

    FILE* stream = nullptr;
    if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) {
        AppendLogLine(L"[EVER2] freopen stdout to CONOUT$ failed.");
    }
    if (freopen_s(&stream, "CONOUT$", "w", stderr) != 0) {
        AppendLogLine(L"[EVER2] freopen stderr to CONOUT$ failed.");
    }
    if (freopen_s(&stream, "CONIN$", "r", stdin) != 0) {
        AppendLogLine(L"[EVER2] freopen stdin to CONIN$ failed.");
    }

    SetConsoleTitleW(L"EVER2 Debug Console");
    g_console_initialized = true;
    LogDebug(L"[EVER2] Debug console initialized.");
#endif
}

void ShutdownDebugConsole() {
#ifdef _DEBUG
    if (!g_console_initialized) {
        return;
    }

    LogDebug(L"[EVER2] Debug console shutdown.");
    FreeConsole();
    g_console_initialized = false;
#endif
}

void LogDebug(const wchar_t* message) {
    if (message == nullptr) {
        return;
    }

    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");

    AppendLogLine(message);

#ifdef _DEBUG
    if (g_console_initialized) {
        wprintf(L"%s\n", message);
        fflush(stdout);
    }
#endif
}

}
