#include "ever/hooking/pattern_scanner.h"

#include "ever/platform/debug_console.h"

#include <polyhook2/Misc.hpp>

namespace {

uint32_t GetModuleImageSize(HMODULE module) {
    if (module == nullptr) {
        return 0;
    }

    const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    const auto* nt_header = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const unsigned char*>(module) + dos_header->e_lfanew);
    if (nt_header->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    return nt_header->OptionalHeader.SizeOfImage;
}

uint64_t FindPatternSafe(uint64_t module_base, size_t module_size, const char* pattern, DWORD* out_exception_code) {
    if (out_exception_code != nullptr) {
        *out_exception_code = 0;
    }

    __try {
        return PLH::findPattern(module_base, module_size, pattern);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (out_exception_code != nullptr) {
            *out_exception_code = GetExceptionCode();
        }
        return 0;
    }
}

}

namespace ever::hooking {

void PatternScanner::Initialize() {
    module_base_ = GetModuleHandleW(nullptr);
    module_size_ = 0;

    if (module_base_ == nullptr) {
        ever::platform::LogDebug(L"[EVER2] PatternScanner::Initialize failed to get module handle.");
        return;
    }

    module_size_ = GetModuleImageSize(module_base_);

    const std::wstring message =
        L"[EVER2] PatternScanner initialized: base=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(module_base_)) +
        L" size=" + std::to_wstring(module_size_);
    ever::platform::LogDebug(message.c_str());
}

void PatternScanner::PerformScan() {
    for (const auto& [name, entry] : patterns_) {
        DWORD scan_exception = 0;
        const uint64_t address = FindPatternSafe(
            reinterpret_cast<uint64_t>(module_base_),
            static_cast<size_t>(module_size_),
            entry.pattern.c_str(),
            &scan_exception);

        if (scan_exception != 0) {
            *entry.destination = 0;
            const std::wstring message =
                L"[EVER2] Pattern '" + std::wstring(name.begin(), name.end()) +
                L"' scan failed with SEH code=" + std::to_wstring(scan_exception);
            ever::platform::LogDebug(message.c_str());
            continue;
        }

        *entry.destination = address;

        const std::wstring message =
            L"[EVER2] Pattern '" + std::wstring(name.begin(), name.end()) +
            L"' " + (address != 0 ? L"found at " : L"not found, addr=") + std::to_wstring(address);
        ever::platform::LogDebug(message.c_str());
    }
}

void PatternScanner::AddPattern(const std::string& name, const std::string& pattern, uint64_t* destination) {
    patterns_[name] = PatternEntry{pattern, destination};
}

}
