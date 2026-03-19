#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>

namespace ever::hooking {

class PatternScanner {
public:
    PatternScanner() = default;
    ~PatternScanner() = default;

    PatternScanner(const PatternScanner&) = delete;
    PatternScanner& operator=(const PatternScanner&) = delete;

    void Initialize();
    void PerformScan();
    void AddPattern(const std::string& name, const std::string& pattern, uint64_t* destination);

    uint64_t GetModuleBase() const {
        return reinterpret_cast<uint64_t>(module_base_);
    }

    size_t GetModuleSize() const {
        return static_cast<size_t>(module_size_);
    }

private:
    struct PatternEntry {
        std::string pattern;
        uint64_t* destination;
    };

    std::map<std::string, PatternEntry> patterns_;
    HMODULE module_base_ = nullptr;
    uint32_t module_size_ = 0;
    const uint8_t* scan_base_ = nullptr;
    uint32_t scan_size_ = 0;
};

}
