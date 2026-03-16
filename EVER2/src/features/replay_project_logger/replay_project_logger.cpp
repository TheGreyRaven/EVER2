#include "ever/features/replay_project_logger/replay_project_logger.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/hooking/x64_detour.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace ever::features::replay_project_logger {

namespace {

constexpr size_t kOffsetHasEnumeratedClips = 0x4FFA8;
constexpr size_t kOffsetHasEnumeratedMontages = 0x4FFA9;
constexpr size_t kOffsetIsEnumerating = 0x4FFAA;
constexpr size_t kOffsetMontageCount = 0x4FF40;
constexpr size_t kOffsetProjectCacheBase = 0x47F20;
constexpr size_t kProjectEntrySize = 0x148;
constexpr size_t kProjectClipArrayOffset = 0x138;
constexpr size_t kMaxProjectsInCache = 100;

struct ReplaySnapshot {
    bool has_enumerated_clips = false;
    bool has_enumerated_montages = false;
    bool is_enumerating = false;
    uint32_t montage_count = 0;
    uint64_t project_cache_base = 0;
    uint32_t project_entry_size = static_cast<uint32_t>(kProjectEntrySize);
};

using EnumerateFn = uint64_t(__fastcall*)(
    void* this_ptr,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7,
    uint64_t a8,
    uint64_t a9,
    uint64_t a10,
    uint64_t a11,
    uint64_t a12);

std::shared_ptr<PLH::x64Detour> g_detour;
EnumerateFn g_original = nullptr;
std::mutex g_install_mutex;
std::atomic<uint64_t> g_hook_hits{0};
std::atomic<uint32_t> g_install_attempt_count{0};
std::atomic<ULONGLONG> g_last_install_attempt_tick{0};
ReplaySnapshot g_last_snapshot{};
bool g_has_last_snapshot = false;
std::atomic<uint64_t> g_last_this_ptr{0};

using RtlLookupFunctionEntryFn = PRUNTIME_FUNCTION(NTAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);

bool IsReadableAddressRange(const void* address, size_t size) {
    if (address == nullptr || size == 0) {
        return false;
    }

    const auto start = reinterpret_cast<uintptr_t>(address);
    const auto end = start + size;
    if (end < start) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    const SIZE_T query_size = VirtualQuery(address, &mbi, sizeof(mbi));
    if (query_size != sizeof(mbi)) {
        return false;
    }

    if (mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
        return false;
    }

    const auto region_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const auto region_end = region_start + mbi.RegionSize;
    return start >= region_start && end <= region_end;
}

template <typename T>
bool ReadObjectAt(const uint8_t* base, size_t offset, T& out_value) {
    const uint8_t* address = base + offset;
    if (!IsReadableAddressRange(address, sizeof(T))) {
        return false;
    }

    std::memcpy(&out_value, address, sizeof(T));
    return true;
}

bool CaptureSnapshot(void* this_ptr, ReplaySnapshot& out_snapshot) {
    if (this_ptr == nullptr) {
        return false;
    }

    const auto* base = static_cast<const uint8_t*>(this_ptr);

    uint8_t clips = 0;
    uint8_t montages = 0;
    uint8_t enumerating = 0;
    uint32_t montage_count = 0;

    if (!ReadObjectAt(base, kOffsetHasEnumeratedClips, clips)) {
        return false;
    }
    if (!ReadObjectAt(base, kOffsetHasEnumeratedMontages, montages)) {
        return false;
    }
    if (!ReadObjectAt(base, kOffsetIsEnumerating, enumerating)) {
        return false;
    }
    if (!ReadObjectAt(base, kOffsetMontageCount, montage_count)) {
        return false;
    }
    out_snapshot.has_enumerated_clips = clips != 0;
    out_snapshot.has_enumerated_montages = montages != 0;
    out_snapshot.is_enumerating = enumerating != 0;
    out_snapshot.montage_count = montage_count;
    out_snapshot.project_cache_base = reinterpret_cast<uint64_t>(base + kOffsetProjectCacheBase);
    return true;
}

uint64_t ResolveFunctionStartFromUnwind(uint64_t hit_address, uint64_t module_base, uint64_t module_size) {
    if (hit_address == 0 || module_base == 0 || module_size == 0) {
        return 0;
    }

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return 0;
    }

    const auto rtl_lookup = reinterpret_cast<RtlLookupFunctionEntryFn>(
        GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
    if (rtl_lookup == nullptr) {
        return 0;
    }

    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION runtime_function = rtl_lookup(
        static_cast<DWORD64>(hit_address),
        &image_base,
        nullptr);
    if (runtime_function == nullptr || image_base == 0) {
        return 0;
    }

    const uint64_t function_start = static_cast<uint64_t>(image_base) + runtime_function->BeginAddress;
    if (function_start < module_base || function_start >= (module_base + module_size)) {
        return 0;
    }

    return function_start;
}

void LogSnapshot(const wchar_t* reason, const void* this_ptr, const ReplaySnapshot& snapshot) {
    const std::wstring message =
        L"[EVER2] Replay project enumerate " + std::wstring(reason) +
        L": this=" + std::to_wstring(reinterpret_cast<uintptr_t>(this_ptr)) +
        L" clipsDone=" + std::to_wstring(snapshot.has_enumerated_clips ? 1 : 0) +
        L" montagesDone=" + std::to_wstring(snapshot.has_enumerated_montages ? 1 : 0) +
        L" isEnumerating=" + std::to_wstring(snapshot.is_enumerating ? 1 : 0) +
        L" montageCount=" + std::to_wstring(snapshot.montage_count) +
        L" projectCacheBase=" + std::to_wstring(snapshot.project_cache_base) +
        L" projectEntrySize=" + std::to_wstring(snapshot.project_entry_size);
    ever::platform::LogDebug(message.c_str());
}

bool IsLikelyPrintable(char c) {
    return c >= 32 && c <= 126;
}

std::string GuessPathFromProjectEntry(const uint8_t* entry_base) {
    std::string best;

    for (size_t i = 0; i + 8 < kProjectEntrySize; ++i) {
        const char* candidate = reinterpret_cast<const char*>(entry_base + i);
        size_t len = 0;
        while ((i + len) < kProjectEntrySize && candidate[len] != '\0' && IsLikelyPrintable(candidate[len])) {
            ++len;
        }

        if (len < 8 || (i + len) >= kProjectEntrySize || candidate[len] != '\0') {
            continue;
        }

        const std::string text(candidate, len);
        if (text.find('\\') == std::string::npos && text.find('/') == std::string::npos) {
            continue;
        }
        if (text.find('.') == std::string::npos) {
            continue;
        }
        if (text.size() > best.size()) {
            best = text;
        }
    }

    return best;
}

void LogDecodedProjects(const ReplaySnapshot& snapshot) {
    if (snapshot.project_cache_base == 0) {
        ever::platform::LogDebug(L"[EVER2] Replay project decode: project cache base is null.");
        return;
    }

    uint32_t total = snapshot.montage_count;
    if (total > kMaxProjectsInCache) {
        total = kMaxProjectsInCache;
    }

    const std::wstring header =
        L"[EVER2] Replay project decode begin: count=" + std::to_wstring(total) +
        L" base=" + std::to_wstring(snapshot.project_cache_base) +
        L" stride=" + std::to_wstring(snapshot.project_entry_size);
    ever::platform::LogDebug(header.c_str());

    for (uint32_t i = 0; i < total; ++i) {
        const uint64_t entry_addr = snapshot.project_cache_base + (static_cast<uint64_t>(i) * snapshot.project_entry_size);
        const auto* entry = reinterpret_cast<const uint8_t*>(entry_addr);
        if (!IsReadableAddressRange(entry, kProjectEntrySize)) {
            const std::wstring message =
                L"[EVER2] Replay project[" + std::to_wstring(i) +
                L"] unreadable at addr=" + std::to_wstring(entry_addr);
            ever::platform::LogDebug(message.c_str());
            continue;
        }

        uint32_t duration_ms = 0;
        uint64_t size_bytes = 0;
        uint64_t last_write = 0;
        uint64_t user_id = 0;
        uint64_t clip_array_ptr = 0;
        uint16_t clip_count16 = 0;
        uint16_t clip_capacity16 = 0;
        uint32_t clip_count32 = 0;
        uint32_t clip_capacity32 = 0;

        ReadObjectAt(entry, 0x08, duration_ms);
        ReadObjectAt(entry, 0x10, size_bytes);
        ReadObjectAt(entry, 0x18, last_write);
        ReadObjectAt(entry, 0x20, user_id);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x00, clip_array_ptr);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x08, clip_count16);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x0A, clip_capacity16);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x08, clip_count32);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x0C, clip_capacity32);

        const std::string path_guess = GuessPathFromProjectEntry(entry);
        std::wstring path_w = path_guess.empty() ? L"<unknown>" : std::wstring(path_guess.begin(), path_guess.end());

        std::wstring message =
            L"[EVER2] Replay project[" + std::to_wstring(i) +
            L"] durationMs=" + std::to_wstring(duration_ms) +
            L" sizeBytes=" + std::to_wstring(size_bytes) +
            L" userId=" + std::to_wstring(user_id) +
            L" lastWrite=" + std::to_wstring(last_write) +
            L" path=" + path_w +
            L" clipArrayPtr=" + std::to_wstring(clip_array_ptr) +
            L" clipCount16=" + std::to_wstring(clip_count16) +
            L" clipCap16=" + std::to_wstring(clip_capacity16) +
            L" clipCount32=" + std::to_wstring(clip_count32) +
            L" clipCap32=" + std::to_wstring(clip_capacity32);

        if (clip_array_ptr != 0 && clip_count16 > 0 && clip_count16 < 256) {
            const auto* clip_base = reinterpret_cast<const uint8_t*>(clip_array_ptr);
            if (IsReadableAddressRange(clip_base, static_cast<size_t>(clip_count16) * 0x10)) {
                std::wstringstream uid_stream;
                const uint16_t uid_dump_count = std::min<uint16_t>(clip_count16, 12);
                for (uint16_t uid_index = 0; uid_index < uid_dump_count; ++uid_index) {
                    uint32_t uid = 0;
                    ReadObjectAt(clip_base, static_cast<size_t>(uid_index) * 0x10 + 0x08, uid);
                    if (uid_index > 0) {
                        uid_stream << L",";
                    }
                    uid_stream << uid;
                }
                message += L" clipUIDs=" + uid_stream.str();
            }
        }

        ever::platform::LogDebug(message.c_str());
    }

    ever::platform::LogDebug(L"[EVER2] Replay project decode end.");
}

bool ShouldLogTransition(const ReplaySnapshot& before, const ReplaySnapshot& after) {
    if (before.is_enumerating && !after.is_enumerating) {
        return true;
    }

    if (!before.has_enumerated_montages && after.has_enumerated_montages) {
        return true;
    }

    if (before.montage_count != after.montage_count) {
        return true;
    }

    return false;
}

uint64_t __fastcall HookedEnumerate(
    void* this_ptr,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7,
    uint64_t a8,
    uint64_t a9,
    uint64_t a10,
    uint64_t a11,
    uint64_t a12) {
    thread_local bool reentrant = false;
    if (reentrant || g_original == nullptr) {
        if (g_original != nullptr) {
            return g_original(this_ptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        }
        return 0;
    }

    reentrant = true;

    ReplaySnapshot before{};
    const bool before_ok = CaptureSnapshot(this_ptr, before);
    g_last_this_ptr.store(reinterpret_cast<uint64_t>(this_ptr), std::memory_order_release);

    const uint64_t result = g_original(this_ptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);

    ReplaySnapshot after{};
    const bool after_ok = CaptureSnapshot(this_ptr, after);

    const uint64_t hit_count = g_hook_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hit_count == 1 || (hit_count % 100) == 0) {
        const std::wstring message =
            L"[EVER2] Replay project enumerate hook invoked. hits=" + std::to_wstring(hit_count);
        ever::platform::LogDebug(message.c_str());
    }

    if (after_ok) {
        if (!g_has_last_snapshot) {
            g_last_snapshot = after;
            g_has_last_snapshot = true;
            LogSnapshot(L"initial state", this_ptr, after);
        } else if (before_ok && ShouldLogTransition(before, after)) {
            LogSnapshot(L"transition", this_ptr, after);
            g_last_snapshot = after;
        }
    }

    reentrant = false;
    return result;
}

void InstallHookNoThrow() {
    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const char* const* candidates =
        ever::hooking::GetGameFunctionPatternCandidates(ever::hooking::GameFunctionPatternId::ReplayEnumerateProjects);
    if (candidates == nullptr) {
        ever::platform::LogDebug(L"[EVER2] Replay project logger: no pattern candidates registered.");
        return;
    }

    uint64_t hit_address = 0;
    int matched_candidate = -1;

    for (int i = 0; candidates[i] != nullptr; ++i) {
        uint64_t candidate_hit = 0;
        {
            const std::wstring message =
                L"[EVER2] Replay project logger: scanning candidate index=" + std::to_wstring(i) +
                L" (exact).";
            ever::platform::LogDebug(message.c_str());
        }

        scanner.AddPattern("ReplayEnumerateProjectsCandidate", candidates[i], &candidate_hit);
        scanner.PerformScan();

        if (candidate_hit != 0) {
            hit_address = candidate_hit;
            matched_candidate = i;
            break;
        }
    }

    if (hit_address == 0) {
        const std::wstring message =
            L"[EVER2] Replay project logger: none of the confirmed patterns matched. attempts=" +
            std::to_wstring(g_install_attempt_count.load(std::memory_order_relaxed));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const uint64_t module_base = scanner.GetModuleBase();
    const uint64_t module_size = scanner.GetModuleSize();
    const uint64_t function_start = ResolveFunctionStartFromUnwind(hit_address, module_base, module_size);

    if (function_start == 0) {
        const std::wstring message =
            L"[EVER2] Replay project logger: pattern matched at " + std::to_wstring(hit_address) +
            L" but function start could not be resolved through unwind metadata.";
        ever::platform::LogDebug(message.c_str());
        return;
    }

    HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        reinterpret_cast<void*>(&HookedEnumerate),
        &g_original,
        g_detour);
    if (FAILED(hr) || g_detour == nullptr || g_original == nullptr) {
        const std::wstring message =
            L"[EVER2] Replay project logger: failed to install detour. hr=" + std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    const std::wstring message =
        L"[EVER2] Replay project logger hook installed. candidateIndex=" + std::to_wstring(matched_candidate) +
        L" match=" + std::to_wstring(hit_address) +
        L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}

void EnsureHookInstalled() {
    if (g_detour != nullptr) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last_attempt = g_last_install_attempt_tick.load(std::memory_order_acquire);
    if (last_attempt != 0 && (now - last_attempt) < 1500) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_install_mutex);
    if (g_detour != nullptr) {
        return;
    }

    const ULONGLONG lock_now = GetTickCount64();
    const ULONGLONG lock_last_attempt = g_last_install_attempt_tick.load(std::memory_order_acquire);
    if (lock_last_attempt != 0 && (lock_now - lock_last_attempt) < 1500) {
        return;
    }

    g_last_install_attempt_tick.store(lock_now, std::memory_order_release);
    const uint32_t attempt = g_install_attempt_count.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::wstring attempt_message =
        L"[EVER2] Replay project logger: install attempt=" + std::to_wstring(attempt);
    ever::platform::LogDebug(attempt_message.c_str());

    InstallHookNoThrow();
}

void LogSnapshotForUiTrigger() {
    EnsureHookInstalled();

    if (g_detour == nullptr) {
        const std::wstring message =
            L"[EVER2] Load project UI trigger: replay enumeration hook is not installed (pattern mismatch or install failure). attempts=" +
            std::to_wstring(g_install_attempt_count.load(std::memory_order_relaxed));
        ever::platform::LogDebug(message.c_str());
        return;
    }

    if (!g_has_last_snapshot) {
        const std::wstring message =
            L"[EVER2] Load project UI trigger: hook is installed but no enumerate snapshot captured yet. hookHits=" +
            std::to_wstring(g_hook_hits.load(std::memory_order_relaxed)) +
            L". Open the in-game load flow once to populate it.";
        ever::platform::LogDebug(message.c_str());
        return;
    }

    LogSnapshot(L"ui-trigger latest", nullptr, g_last_snapshot);
    LogDecodedProjects(g_last_snapshot);
}

}
