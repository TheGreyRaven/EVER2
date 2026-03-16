#include "ever/platform/dll_runtime_loader.h"
#include "ever/platform/debug_console.h"

#include <filesystem>
#include <string>

namespace {

void Log(const wchar_t* message) {
    ever::platform::LogDebug(message);
}

std::filesystem::path GetModuleDirectory(HMODULE module_handle) {
    wchar_t module_path[MAX_PATH] = {};
    if (GetModuleFileNameW(module_handle, module_path, MAX_PATH) == 0) {
        return {};
    }

    std::filesystem::path path(module_path);
    return path.parent_path();
}

bool DetectFiveMHost() {
    wchar_t exe_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        return false;
    }

    const std::wstring exe_name = std::filesystem::path(exe_path).filename().wstring();
    return exe_name.find(L"FiveM") != std::wstring::npos || exe_name.find(L"fivem") != std::wstring::npos;
}

std::filesystem::path GetRuntimeRoot(const HMODULE module_handle) {
    const std::filesystem::path module_directory = GetModuleDirectory(module_handle);
    if (module_directory.empty()) {
        return {};
    }

    return module_directory / L"EVER2";
}

bool LoadAllDllsFrom(const std::filesystem::path& directory) {
    const std::wstring pattern = (directory / L"*.dll").wstring();

    WIN32_FIND_DATAW file_data = {};
    HANDLE find_handle = FindFirstFileW(pattern.c_str(), &file_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return true;
    }

    do {
        if ((file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        const std::filesystem::path dll_path = directory / file_data.cFileName;
        HMODULE loaded = LoadLibraryW(dll_path.c_str());
        if (loaded == nullptr) {
            const std::wstring message = L"[EVER2] Failed to load DLL: " + dll_path.wstring();
            Log(message.c_str());
            FindClose(find_handle);
            return false;
        }
    } while (FindNextFileW(find_handle, &file_data));

    FindClose(find_handle);
    return true;
}

}

namespace ever::platform {

bool ConfigureRuntimeDllDirectory(HMODULE module_handle) {
    const std::filesystem::path runtime_root = GetRuntimeRoot(module_handle);
    if (runtime_root.empty()) {
        Log(L"[EVER2] Could not resolve runtime root directory.");
        return false;
    }

    const std::filesystem::path dll_directory = runtime_root / L"dlls";
    if (!std::filesystem::exists(dll_directory)) {
        const std::wstring message = L"[EVER2] Runtime DLL directory is missing: " + dll_directory.wstring();
        Log(message.c_str());
        return false;
    }

    if (SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        if (AddDllDirectory(dll_directory.c_str()) == nullptr) {
            Log(L"[EVER2] AddDllDirectory failed.");
            return false;
        }
    } else {
        if (!SetDllDirectoryW(dll_directory.c_str())) {
            Log(L"[EVER2] SetDllDirectoryW failed.");
            return false;
        }
    }

    if (!LoadAllDllsFrom(dll_directory)) {
        Log(L"[EVER2] Failed while preloading DLLs from runtime dll directory.");
        return false;
    }

    const std::wstring message = L"[EVER2] Runtime DLL directory configured: " + dll_directory.wstring();
    Log(message.c_str());
    return true;
}

bool IsFiveMHostProcess() {
    return DetectFiveMHost();
}

}
