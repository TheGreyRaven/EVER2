#include "ever/hooking/pattern_scanner.h"

#include "ever/platform/debug_console.h"

#include <vector>

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

    if (module_base == 0 || module_size == 0 || pattern == nullptr || pattern[0] == '\0') {
        if (out_exception_code != nullptr) {
            *out_exception_code = ERROR_INVALID_PARAMETER;
        }
        return 0;
    }

    std::vector<int> bytes;
    bytes.reserve(64);

    const char* p = pattern;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }

        if (*p == '?') {
            ++p;
            if (*p == '?') {
                ++p;
            }
            bytes.push_back(-1);
            continue;
        }

        auto hex_to_nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return 10 + (c - 'a');
            }
            if (c >= 'A' && c <= 'F') {
                return 10 + (c - 'A');
            }
            return -1;
        };

        const int hi = hex_to_nibble(*p++);
        if (hi < 0 || *p == '\0') {
            if (out_exception_code != nullptr) {
                *out_exception_code = ERROR_BAD_FORMAT;
            }
            return 0;
        }
        const int lo = hex_to_nibble(*p++);
        if (lo < 0) {
            if (out_exception_code != nullptr) {
                *out_exception_code = ERROR_BAD_FORMAT;
            }
            return 0;
        }
        bytes.push_back((hi << 4) | lo);
    }

    if (bytes.empty() || bytes.size() > module_size) {
        return 0;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(module_base);
    const size_t last = module_size - bytes.size();
    for (size_t i = 0; i <= last; ++i) {
        bool match = true;
        for (size_t j = 0; j < bytes.size(); ++j) {
            const int expected = bytes[j];
            if (expected >= 0 && data[i + j] != static_cast<uint8_t>(expected)) {
                match = false;
                break;
            }
        }
        if (match) {
            return module_base + i;
        }
    }

    return 0;
}

bool ResolveExecutableSectionRange(
    HMODULE module,
    const uint8_t*& out_scan_base,
    uint32_t& out_scan_size) {
    out_scan_base = nullptr;
    out_scan_size = 0;

    if (module == nullptr) {
        return false;
    }

    const auto* module_bytes = reinterpret_cast<const uint8_t*>(module);
    const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module_bytes);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const auto* nt_header = reinterpret_cast<const IMAGE_NT_HEADERS*>(module_bytes + dos_header->e_lfanew);
    if (nt_header->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt_header);
    uint32_t min_rva = 0xFFFFFFFFu;
    uint32_t max_end_rva = 0;

    for (unsigned i = 0; i < nt_header->FileHeader.NumberOfSections; ++i, ++section) {
        const DWORD characteristics = section->Characteristics;
        const bool executable = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        const bool code = (characteristics & IMAGE_SCN_CNT_CODE) != 0;
        if (!executable && !code) {
            continue;
        }

        const uint32_t rva = section->VirtualAddress;
        uint32_t size = section->Misc.VirtualSize;
        if (size == 0) {
            size = section->SizeOfRawData;
        }
        if (size == 0) {
            continue;
        }

        if (rva < min_rva) {
            min_rva = rva;
        }

        const uint32_t end_rva = rva + size;
        if (end_rva > max_end_rva) {
            max_end_rva = end_rva;
        }
    }

    if (min_rva == 0xFFFFFFFFu || max_end_rva <= min_rva) {
        return false;
    }

    out_scan_base = module_bytes + min_rva;
    out_scan_size = max_end_rva - min_rva;
    return true;
}

}

namespace ever::hooking {

void PatternScanner::Initialize() {
    module_base_ = GetModuleHandleW(nullptr);
    module_size_ = 0;
    scan_base_ = nullptr;
    scan_size_ = 0;

    if (module_base_ == nullptr) {
        ever::platform::LogDebug(L"[EVER2] PatternScanner::Initialize failed to get module handle.");
        return;
    }

    module_size_ = GetModuleImageSize(module_base_);

    const bool resolved_scan_range = ResolveExecutableSectionRange(module_base_, scan_base_, scan_size_);
    if (!resolved_scan_range || scan_base_ == nullptr || scan_size_ == 0) {
        // Fallback to full module when section parsing fails.
        scan_base_ = reinterpret_cast<const uint8_t*>(module_base_);
        scan_size_ = module_size_;
    }

    const std::wstring message =
        L"[EVER2] PatternScanner initialized: base=" +
        std::to_wstring(reinterpret_cast<uintptr_t>(module_base_)) +
        L" size=" + std::to_wstring(module_size_) +
        L" scanBase=" + std::to_wstring(reinterpret_cast<uintptr_t>(scan_base_)) +
        L" scanSize=" + std::to_wstring(scan_size_);
    ever::platform::LogDebug(message.c_str());
}

void PatternScanner::PerformScan() {
    const uint8_t* const fallback_base = reinterpret_cast<const uint8_t*>(module_base_);
    const uint64_t effective_base = reinterpret_cast<uint64_t>(scan_base_ != nullptr ? scan_base_ : fallback_base);
    const size_t effective_size = static_cast<size_t>(scan_size_ != 0 ? scan_size_ : module_size_);

    for (const auto& [name, entry] : patterns_) {
        const ULONGLONG scan_begin = GetTickCount64();
        DWORD scan_exception = 0;
        const uint64_t address = FindPatternSafe(
            effective_base,
            effective_size,
            entry.pattern.c_str(),
            &scan_exception);
        const ULONGLONG scan_elapsed = GetTickCount64() - scan_begin;

        if (scan_exception != 0) {
            *entry.destination = 0;
            const std::wstring message =
                L"[EVER2] Pattern '" + std::wstring(name.begin(), name.end()) +
                L"' scan failed with SEH code=" + std::to_wstring(scan_exception) +
                L" elapsedMs=" + std::to_wstring(scan_elapsed);
            ever::platform::LogDebug(message.c_str());
            continue;
        }

        *entry.destination = address;

        const std::wstring message =
            L"[EVER2] Pattern '" + std::wstring(name.begin(), name.end()) +
            L"' " + (address != 0 ? L"found at " : L"not found, addr=") + std::to_wstring(address) +
            L" elapsedMs=" + std::to_wstring(scan_elapsed);
        ever::platform::LogDebug(message.c_str());
    }
}

void PatternScanner::AddPattern(const std::string& name, const std::string& pattern, uint64_t* destination) {
    patterns_[name] = PatternEntry{pattern, destination};
}

}
