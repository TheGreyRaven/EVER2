#include "ever/features/replay_project_logger/replay_project_logger.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/hooking/x64_detour.h"
#include "ever/platform/debug_console.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace ever::features::replay_project_logger {

namespace {

using Json = nlohmann::json;

constexpr size_t kOffsetHasEnumeratedClips = 0x4FFA8;
constexpr size_t kOffsetHasEnumeratedMontages = 0x4FFA9;
constexpr size_t kOffsetIsEnumerating = 0x4FFAA;
constexpr size_t kOffsetMontageCount = 0x4FF40;
constexpr size_t kOffsetProjectCacheBase = 0x47F20;
constexpr size_t kProjectEntrySize = 0x148;
constexpr size_t kProjectClipArrayOffset = 0x138;
constexpr size_t kMaxProjectsInCache = 100;
constexpr size_t kProjectPathOffset = 0x31;
constexpr size_t kProjectPathMaxBytes = 260;

constexpr size_t kReplayInfoMontagePtrOffset = 0xAD8;
constexpr size_t kReplayInfoPathPtrOffset = 0xAF0;
constexpr size_t kReplayInfoPathLenOffset = 0xAF8;
constexpr size_t kReplayInfoFilenamePtrOffset = 0xB00;
constexpr size_t kReplayInfoFilenameLenOffset = 0xB08;
constexpr int kEnumerateKickPollIterations = 40;
constexpr DWORD kEnumerateKickPollSleepMs = 10;

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

using LoadMontageFn = uint32_t(__fastcall*)(void* this_ptr, void* replay_info, uint64_t* out_extended_result);
using ReplayFileManagerStartEnumerateProjectFilesFn = bool(__fastcall*)(void* file_list, const char* filter);
using ReplayFileManagerCheckEnumerateProjectFilesFn = bool(__fastcall*)(bool* result);

std::shared_ptr<PLH::x64Detour> g_enumerate_detour;
EnumerateFn g_enumerate_original = nullptr;
std::shared_ptr<PLH::x64Detour> g_load_montage_detour;
LoadMontageFn g_load_montage_original = nullptr;
std::shared_ptr<PLH::x64Detour> g_replay_file_manager_start_enum_projects_detour;
ReplayFileManagerStartEnumerateProjectFilesFn g_replay_file_manager_start_enum_projects_original = nullptr;
std::shared_ptr<PLH::x64Detour> g_replay_file_manager_check_enum_projects_detour;
ReplayFileManagerCheckEnumerateProjectFilesFn g_replay_file_manager_check_enum_projects_original = nullptr;
std::mutex g_install_mutex;
std::atomic<uint64_t> g_hook_hits{0};
std::atomic<uint64_t> g_load_montage_hook_hits{0};
std::atomic<uint64_t> g_start_enum_projects_hook_hits{0};
std::atomic<uint64_t> g_check_enum_projects_hook_hits{0};
std::atomic<bool> g_last_enumerate_projects_completed{false};
std::atomic<bool> g_last_enumerate_projects_result{false};
std::atomic<uintptr_t> g_last_enum_projects_file_list_ptr{0};
std::atomic<bool> g_enumerate_session_active{false};
std::atomic<int> g_last_check_logged_state{-1};
std::atomic<uint32_t> g_install_attempt_count{0};
std::atomic<ULONGLONG> g_last_install_attempt_tick{0};
ReplaySnapshot g_last_snapshot{};
bool g_has_last_snapshot = false;

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

bool IsLikelyPrintableAscii(char c) {
    return c >= 32 && c <= 126;
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

uint64_t ResolvePatternToFunctionStart(
    ever::hooking::PatternScanner& scanner,
    ever::hooking::GameFunctionPatternId pattern_id,
    const wchar_t* log_prefix,
    int* out_candidate_index,
    int max_candidates = -1) {
    const char* const* candidates = ever::hooking::GetGameFunctionPatternCandidates(pattern_id);
    if (candidates == nullptr) {
        const std::wstring message = std::wstring(log_prefix) + L": no pattern candidates registered.";
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    uint64_t hit_address = 0;
    int matched_candidate = -1;
    for (int i = 0; candidates[i] != nullptr; ++i) {
        if (max_candidates > 0 && i >= max_candidates) {
            break;
        }

        uint64_t candidate_hit = 0;
        const std::wstring message =
            std::wstring(log_prefix) + L": scanning candidate index=" + std::to_wstring(i) + L" (exact).";
        ever::platform::LogDebug(message.c_str());

        const std::string pattern_key = "ReplayProjectLoggerCandidate_" + std::to_string(i);
        scanner.AddPattern(pattern_key, candidates[i], &candidate_hit);
        scanner.PerformScan();
        if (candidate_hit != 0) {
            hit_address = candidate_hit;
            matched_candidate = i;
            break;
        }
    }

    if (out_candidate_index != nullptr) {
        *out_candidate_index = matched_candidate;
    }

    if (hit_address == 0) {
        const std::wstring message =
            std::wstring(log_prefix) + L": none of the patterns matched. attempts=" +
            std::to_wstring(g_install_attempt_count.load(std::memory_order_relaxed));
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    const uint64_t function_start = ResolveFunctionStartFromUnwind(
        hit_address,
        scanner.GetModuleBase(),
        scanner.GetModuleSize());
    if (function_start == 0) {
        const std::wstring message =
            std::wstring(log_prefix) + L": pattern matched at " + std::to_wstring(hit_address) +
            L" but function start could not be resolved through unwind metadata.";
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    const std::wstring message =
        std::wstring(log_prefix) + L": resolved candidateIndex=" + std::to_wstring(matched_candidate) +
        L" match=" + std::to_wstring(hit_address) +
        L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
    return function_start;
}

uint64_t ResolvePatternToAddress(
    ever::hooking::PatternScanner& scanner,
    ever::hooking::GameFunctionPatternId pattern_id,
    const wchar_t* log_prefix,
    int* out_candidate_index,
    int max_candidates = -1) {
    const char* const* candidates = ever::hooking::GetGameFunctionPatternCandidates(pattern_id);
    if (candidates == nullptr) {
        const std::wstring message = std::wstring(log_prefix) + L": no pattern candidates registered.";
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    uint64_t hit_address = 0;
    int matched_candidate = -1;
    for (int i = 0; candidates[i] != nullptr; ++i) {
        if (max_candidates > 0 && i >= max_candidates) {
            break;
        }

        uint64_t candidate_hit = 0;
        const std::wstring message =
            std::wstring(log_prefix) + L": scanning candidate index=" + std::to_wstring(i) + L" (exact address).";
        ever::platform::LogDebug(message.c_str());

        const std::string pattern_key = "ReplayProjectLoggerAddressCandidate_" + std::to_string(i);
        scanner.AddPattern(pattern_key, candidates[i], &candidate_hit);
        scanner.PerformScan();
        if (candidate_hit != 0) {
            hit_address = candidate_hit;
            matched_candidate = i;
            break;
        }
    }

    if (out_candidate_index != nullptr) {
        *out_candidate_index = matched_candidate;
    }

    if (hit_address == 0) {
        const std::wstring message =
            std::wstring(log_prefix) + L": none of the patterns matched. attempts=" +
            std::to_wstring(g_install_attempt_count.load(std::memory_order_relaxed));
        ever::platform::LogDebug(message.c_str());
        return 0;
    }

    const std::wstring message =
        std::wstring(log_prefix) + L": resolved candidateIndex=" + std::to_wstring(matched_candidate) +
        L" match=" + std::to_wstring(hit_address) +
        L" functionStart=" + std::to_wstring(hit_address) +
        L" (direct-match)";
    ever::platform::LogDebug(message.c_str());
    return hit_address;
}

uint64_t ResolveCheckEnumerateHookAddress(uint64_t matched_address) {
    if (matched_address == 0) {
        return 0;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(matched_address);
    if (!IsReadableAddressRange(bytes, 3)) {
        return matched_address;
    }

    if (bytes[0] == 0x90 && bytes[1] == 0x8B && bytes[2] == 0x05) {
        return matched_address + 1;
    }

    return matched_address;
}

void BeginEnumerateSession() {
    g_last_enumerate_projects_completed.store(false, std::memory_order_release);
    g_last_enumerate_projects_result.store(false, std::memory_order_release);
    g_last_check_logged_state.store(-1, std::memory_order_release);
    g_enumerate_session_active.store(true, std::memory_order_release);
}

void EndEnumerateSession() {
    g_enumerate_session_active.store(false, std::memory_order_release);
}

bool AreCoreHooksInstalled() {
    return g_enumerate_detour != nullptr &&
           g_load_montage_detour != nullptr &&
           g_replay_file_manager_start_enum_projects_detour != nullptr &&
           g_replay_file_manager_check_enum_projects_detour != nullptr;
}

template <typename OriginalFn>
bool InstallDetourForResolvedAddress(
    uint64_t function_start,
    void* detour_handler,
    OriginalFn* out_original,
    std::shared_ptr<PLH::x64Detour>& out_detour,
    const wchar_t* failure_message_prefix) {
    const HRESULT hr = ever::hooking::HookX64Function(
        function_start,
        detour_handler,
        out_original,
        out_detour);
    if (FAILED(hr) || out_detour == nullptr || *out_original == nullptr) {
        const std::wstring message =
            std::wstring(failure_message_prefix) + std::to_wstring(static_cast<long>(hr));
        ever::platform::LogDebug(message.c_str());
        return false;
    }
    return true;
}

bool EnsureEnumerateHookInstalledForUi() {
    PrimeHookInstallationAsync();
    if (g_enumerate_detour == nullptr) {
        EnsureHookInstalled();
    }
    return g_enumerate_detour != nullptr;
}

std::string ReadAsciiCStringAt(const char* address, size_t max_len) {
    if (!IsReadableAddressRange(address, 1) || max_len == 0) {
        return std::string();
    }

    std::string out;
    out.reserve(max_len);
    for (size_t i = 0; i < max_len; ++i) {
        if (!IsReadableAddressRange(address + i, 1)) {
            break;
        }
        const char c = address[i];
        if (c == '\0') {
            break;
        }
        if (!IsLikelyPrintableAscii(c)) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::string ReadReplayInfoString(const uint8_t* info_base, size_t ptr_offset, size_t len_offset) {
    if (info_base == nullptr) {
        return std::string();
    }

    uint16_t len16 = 0;
    if (!ReadObjectAt(info_base, len_offset, len16)) {
        return std::string();
    }

    uint64_t ptr = 0;
    if (!ReadObjectAt(info_base, ptr_offset, ptr)) {
        return std::string();
    }

    if (ptr == 0) {
        return std::string();
    }

    const size_t max_len = std::min<size_t>(len16 > 0 ? len16 : 260, 260);
    return ReadAsciiCStringAt(reinterpret_cast<const char*>(ptr), max_len);
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

std::wstring ToWide(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return std::string();
    }

    std::string out(static_cast<size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        out.data(),
        required,
        nullptr,
        nullptr);
    if (written <= 0) {
        return std::string();
    }

    return out;
}

std::string GetFilenameNoExtension(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    size_t file_start = path.find_last_of("\\/");
    if (file_start == std::string::npos) {
        file_start = 0;
    } else {
        ++file_start;
    }

    std::string filename = path.substr(file_start);
    const size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) {
        filename.erase(dot);
    }
    return filename;
}

std::wstring FormatLastWriteTimestamp(uint64_t raw_time) {
    if (raw_time == 0) {
        return L"<unknown>";
    }

    uint64_t unix_seconds = raw_time;

    constexpr uint64_t kFileTimeUnixOffsetSeconds = 11644473600ULL;
    constexpr uint64_t kFileTimeTicksPerSecond = 10000000ULL;
    if (raw_time > (kFileTimeUnixOffsetSeconds * kFileTimeTicksPerSecond)) {
        unix_seconds = (raw_time / kFileTimeTicksPerSecond) - kFileTimeUnixOffsetSeconds;
    } else if (raw_time > 1000000000000ULL) {
        unix_seconds = raw_time / 1000ULL;
    }

    const time_t tt = static_cast<time_t>(unix_seconds);
    tm local_tm{};
    if (localtime_s(&local_tm, &tt) != 0) {
        return L"<invalid>";
    }

    wchar_t buffer[64]{};
    if (wcsftime(buffer, _countof(buffer), L"%Y-%m-%d %H:%M:%S", &local_tm) == 0) {
        return L"<invalid>";
    }

    return std::wstring(buffer);
}

std::string GuessPathFromProjectEntry(const uint8_t* entry_base) {
    std::string best;

    for (size_t i = 0; i + 8 < kProjectEntrySize; ++i) {
        const char* candidate = reinterpret_cast<const char*>(entry_base + i);
        size_t len = 0;
        while ((i + len) < kProjectEntrySize && candidate[len] != '\0' && IsLikelyPrintableAscii(candidate[len])) {
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
        uint8_t is_corrupt = 0;
        uint32_t file_hash = 0;
        uint64_t size_bytes = 0;
        uint64_t last_write = 0;
        uint64_t user_id = 0;
        uint8_t mark_delete = 0;
        uint64_t clip_array_ptr = 0;
        uint16_t clip_count16 = 0;
        uint16_t clip_capacity16 = 0;

        ReadObjectAt(entry, 0x08, duration_ms);
        ReadObjectAt(entry, 0x0C, is_corrupt);
        ReadObjectAt(entry, 0x10, file_hash);
        ReadObjectAt(entry, 0x18, size_bytes);
        ReadObjectAt(entry, 0x20, last_write);
        ReadObjectAt(entry, 0x28, user_id);
        ReadObjectAt(entry, 0x30, mark_delete);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x00, clip_array_ptr);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x08, clip_count16);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x0A, clip_capacity16);

        std::string path_guess = ReadAsciiCStringAt(
            reinterpret_cast<const char*>(entry + kProjectPathOffset),
            kProjectPathMaxBytes);
        if (path_guess.empty()) {
            path_guess = GuessPathFromProjectEntry(entry);
        }
        const std::wstring path_w = path_guess.empty() ? L"<unknown>" : ToWide(path_guess);
        const std::string project_name = GetFilenameNoExtension(path_guess);
        const std::wstring project_name_w = project_name.empty() ? L"<unknown>" : ToWide(project_name);
        const std::wstring last_write_local = FormatLastWriteTimestamp(last_write);
        std::wstring message =
            L"[EVER2] Replay project[" + std::to_wstring(i) +
            L"] projectName=" + project_name_w +
            L"] durationMs=" + std::to_wstring(duration_ms) +
            L" corrupt=" + std::to_wstring(is_corrupt != 0 ? 1 : 0) +
            L" fileHash=" + std::to_wstring(file_hash) +
            L" sizeBytes=" + std::to_wstring(size_bytes) +
            L" userId=" + std::to_wstring(user_id) +
            L" lastWriteRaw=" + std::to_wstring(last_write) +
            L" lastWriteLocal=" + last_write_local +
            L" markDelete=" + std::to_wstring(mark_delete != 0 ? 1 : 0) +
            L" path=" + path_w +
            L" clipArrayPtr=" + std::to_wstring(clip_array_ptr) +
            L" clipCount16=" + std::to_wstring(clip_count16) +
            L" clipCap16=" + std::to_wstring(clip_capacity16);

        std::vector<uint32_t> clip_uids;

        if (clip_array_ptr != 0 && clip_count16 > 0 && clip_count16 < 256) {
            const auto* clip_base = reinterpret_cast<const uint8_t*>(clip_array_ptr);
            if (IsReadableAddressRange(clip_base, static_cast<size_t>(clip_count16) * 0x10)) {
                std::wstringstream uid_stream;
                const uint16_t uid_dump_count = std::min<uint16_t>(clip_count16, 12);
                clip_uids.reserve(uid_dump_count);
                for (uint16_t uid_index = 0; uid_index < uid_dump_count; ++uid_index) {
                    uint32_t uid = 0;
                    ReadObjectAt(clip_base, static_cast<size_t>(uid_index) * 0x10 + 0x08, uid);
                    clip_uids.push_back(uid);
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

bool BuildProjectsJsonFromSnapshot(const ReplaySnapshot& snapshot, std::string& out_json, std::wstring& out_error) {
    out_json.clear();
    out_error.clear();

    if (snapshot.project_cache_base == 0) {
        out_error = L"Replay project cache is not available yet.";
        return false;
    }

    uint32_t total = snapshot.montage_count;
    if (total > kMaxProjectsInCache) {
        total = kMaxProjectsInCache;
    }

    Json payload;
    payload["event"] = "ever2_load_project_data";
    payload["status"] = "ready";
    payload["projects"] = Json::array();

    for (uint32_t i = 0; i < total; ++i) {
        const uint64_t entry_addr = snapshot.project_cache_base + (static_cast<uint64_t>(i) * snapshot.project_entry_size);
        const auto* entry = reinterpret_cast<const uint8_t*>(entry_addr);
        if (!IsReadableAddressRange(entry, kProjectEntrySize)) {
            continue;
        }

        uint32_t duration_ms = 0;
        uint8_t is_corrupt = 0;
        uint32_t file_hash = 0;
        uint64_t size_bytes = 0;
        uint64_t last_write = 0;
        uint64_t user_id = 0;
        uint8_t mark_delete = 0;
        uint64_t clip_array_ptr = 0;
        uint16_t clip_count16 = 0;
        uint16_t clip_capacity16 = 0;

        ReadObjectAt(entry, 0x08, duration_ms);
        ReadObjectAt(entry, 0x0C, is_corrupt);
        ReadObjectAt(entry, 0x10, file_hash);
        ReadObjectAt(entry, 0x18, size_bytes);
        ReadObjectAt(entry, 0x20, last_write);
        ReadObjectAt(entry, 0x28, user_id);
        ReadObjectAt(entry, 0x30, mark_delete);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x00, clip_array_ptr);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x08, clip_count16);
        ReadObjectAt(entry, kProjectClipArrayOffset + 0x0A, clip_capacity16);

        std::string path_guess = ReadAsciiCStringAt(
            reinterpret_cast<const char*>(entry + kProjectPathOffset),
            kProjectPathMaxBytes);
        if (path_guess.empty()) {
            path_guess = GuessPathFromProjectEntry(entry);
        }

        const std::string project_name = GetFilenameNoExtension(path_guess);
        const std::wstring last_write_local_w = FormatLastWriteTimestamp(last_write);
        const std::string last_write_local = WideToUtf8(last_write_local_w);
        std::vector<uint32_t> clip_uids;
        if (clip_array_ptr != 0 && clip_count16 > 0 && clip_count16 < 256) {
            const auto* clip_base = reinterpret_cast<const uint8_t*>(clip_array_ptr);
            if (IsReadableAddressRange(clip_base, static_cast<size_t>(clip_count16) * 0x10)) {
                const uint16_t uid_dump_count = std::min<uint16_t>(clip_count16, 256);
                clip_uids.reserve(uid_dump_count);
                for (uint16_t uid_index = 0; uid_index < uid_dump_count; ++uid_index) {
                    uint32_t uid = 0;
                    ReadObjectAt(clip_base, static_cast<size_t>(uid_index) * 0x10 + 0x08, uid);
                    clip_uids.push_back(uid);
                }
            }
        }

        Json project;
        project["index"] = i;
        project["projectName"] = project_name;
        project["path"] = path_guess;
        project["durationMs"] = duration_ms;
        project["corrupt"] = (is_corrupt != 0);
        project["fileHash"] = file_hash;
        project["sizeBytes"] = size_bytes;
        project["userId"] = user_id;
        project["lastWriteRaw"] = last_write;
        project["lastWriteLocal"] = last_write_local;
        project["markDelete"] = (mark_delete != 0);
        project["previewCandidate"] = "";
        project["previewDiskPath"] = "";
        project["previewExists"] = false;
        project["clipArrayPtr"] = clip_array_ptr;
        project["clipCount16"] = clip_count16;
        project["clipCap16"] = clip_capacity16;
        project["clipBaseNameCount"] = 0;

        Json clips = Json::array();
        const size_t max_clip_rows = clip_uids.size();
        for (size_t clip_index = 0; clip_index < max_clip_rows; ++clip_index) {
            Json clip;
            clip["index"] = clip_index;

            clip["baseName"] = "";
            clip["path"] = "";
            clip["diskPath"] = "";
            clip["exists"] = false;
            clip["previewPath"] = "";
            clip["previewDiskPath"] = "";
            clip["previewExists"] = false;

            if (clip_index < clip_uids.size()) {
                clip["uid"] = clip_uids[clip_index];
            }

            clips.push_back(std::move(clip));
        }

        project["clips"] = std::move(clips);
        project["clipCount"] = project["clips"].size();
        payload["projects"].push_back(std::move(project));
    }

    payload["projectCount"] = payload["projects"].size();
    out_json = payload.dump();
    return true;
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
    if (reentrant || g_enumerate_original == nullptr) {
        if (g_enumerate_original != nullptr) {
            return g_enumerate_original(this_ptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
        }
        return 0;
    }

    reentrant = true;

    ReplaySnapshot before{};
    const bool before_ok = CaptureSnapshot(this_ptr, before);
    const uint64_t result = g_enumerate_original(this_ptr, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);

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

uint32_t __fastcall HookedLoadMontage(void* this_ptr, void* replay_info, uint64_t* out_extended_result) {
    thread_local bool reentrant = false;
    if (reentrant || g_load_montage_original == nullptr) {
        if (g_load_montage_original != nullptr) {
            return g_load_montage_original(this_ptr, replay_info, out_extended_result);
        }
        return 1;
    }

    reentrant = true;

    const auto* info_base = static_cast<const uint8_t*>(replay_info);
    uint64_t montage_ptr = 0;
    if (info_base != nullptr) {
        ReadObjectAt(info_base, kReplayInfoMontagePtrOffset, montage_ptr);
    }
    const std::string file_path = ReadReplayInfoString(info_base, kReplayInfoPathPtrOffset, kReplayInfoPathLenOffset);
    const std::string filename = ReadReplayInfoString(info_base, kReplayInfoFilenamePtrOffset, kReplayInfoFilenameLenOffset);
    const std::string project_name = !filename.empty() ? GetFilenameNoExtension(filename) : GetFilenameNoExtension(file_path);
    const uint32_t ret_code = g_load_montage_original(this_ptr, replay_info, out_extended_result);
    uint64_t extended_result = 0;
    if (out_extended_result != nullptr && IsReadableAddressRange(out_extended_result, sizeof(uint64_t))) {
        extended_result = *out_extended_result;
    }

    const uint64_t hit_count = g_load_montage_hook_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::wstring message =
        L"[EVER2] Replay LoadMontage hook: hits=" + std::to_wstring(hit_count) +
        L" this=" + std::to_wstring(reinterpret_cast<uintptr_t>(this_ptr)) +
        L" replayInfo=" + std::to_wstring(reinterpret_cast<uintptr_t>(replay_info)) +
        L" montagePtr=" + std::to_wstring(montage_ptr) +
        L" retCode=" + std::to_wstring(ret_code) +
        L" extendedResult=" + std::to_wstring(extended_result) +
        L" projectName=" + ToWide(project_name) +
        L" path=" + std::wstring(file_path.begin(), file_path.end()) +
        L" filename=" + std::wstring(filename.begin(), filename.end());
    ever::platform::LogDebug(message.c_str());

    ReplaySnapshot snapshot{};
    if (CaptureSnapshot(this_ptr, snapshot)) {
        g_last_snapshot = snapshot;
        g_has_last_snapshot = true;
        LogSnapshot(L"load-montage", this_ptr, snapshot);
        LogDecodedProjects(snapshot);
    }

    reentrant = false;
    return ret_code;
}

bool __fastcall HookedReplayFileManagerStartEnumerateProjectFiles(void* file_list, const char* filter) {
    if (g_replay_file_manager_start_enum_projects_original == nullptr) {
        return false;
    }

    const bool ok = g_replay_file_manager_start_enum_projects_original(file_list, filter);
    g_last_enum_projects_file_list_ptr.store(reinterpret_cast<uintptr_t>(file_list), std::memory_order_release);
    g_last_enumerate_projects_completed.store(false, std::memory_order_release);
    const std::string filter_text = (filter != nullptr) ? filter : "";

    const uint64_t hit_count = g_start_enum_projects_hook_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::wstring message =
        L"[EVER2] ReplayFileManager::StartEnumerateProjectFiles hook: hits=" + std::to_wstring(hit_count) +
        L" fileList=" + std::to_wstring(reinterpret_cast<uintptr_t>(file_list)) +
        L" filter='" + ToWide(filter_text) + L"'" +
        L" returned=" + std::to_wstring(ok ? 1 : 0);
    ever::platform::LogDebug(message.c_str());
    return ok;
}

bool __fastcall HookedReplayFileManagerCheckEnumerateProjectFiles(bool* result) {
    if (g_replay_file_manager_check_enum_projects_original == nullptr) {
        return false;
    }

    const bool completed = g_replay_file_manager_check_enum_projects_original(result);
    const bool op_result = (result != nullptr) ? (*result) : false;

    const uint64_t hit_count = g_check_enum_projects_hook_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool session_active = g_enumerate_session_active.load(std::memory_order_acquire);
    if (session_active && completed) {
        g_last_enumerate_projects_completed.store(true, std::memory_order_release);
        g_last_enumerate_projects_result.store(op_result, std::memory_order_release);
    }

    const int state = (completed ? 2 : 0) | (op_result ? 1 : 0);
    const int previous_state = g_last_check_logged_state.exchange(state, std::memory_order_acq_rel);
    const bool state_changed = state != previous_state;
    const bool should_log =
        session_active ? (state_changed || hit_count == 1 || (hit_count % 200) == 0)
                       : (hit_count == 1 || (hit_count % 1000) == 0);

    if (should_log) {
        const std::wstring message =
            L"[EVER2] ReplayFileManager::CheckEnumerateProjectFiles hook: hits=" + std::to_wstring(hit_count) +
            L" sessionActive=" + std::to_wstring(session_active ? 1 : 0) +
            L" completed=" + std::to_wstring(completed ? 1 : 0) +
            L" result=" + std::to_wstring(op_result ? 1 : 0);
        ever::platform::LogDebug(message.c_str());
    }

    return completed;
}

void TryKickNativeProjectEnumeration() {
    if (g_replay_file_manager_start_enum_projects_original == nullptr ||
        g_replay_file_manager_check_enum_projects_original == nullptr) {
        return;
    }

    uintptr_t file_list_ptr = g_last_enum_projects_file_list_ptr.load(std::memory_order_acquire);
    if (file_list_ptr != 0 && !IsReadableAddressRange(reinterpret_cast<const void*>(file_list_ptr), sizeof(void*))) {
        file_list_ptr = 0;
    }

    BeginEnumerateSession();

    if (file_list_ptr == 0) {
        ever::platform::LogDebug(
            L"[EVER2] Replay enumerate kick: no captured file list pointer; trying StartEnumerate with nullptr context.");
    }

    const bool started = g_replay_file_manager_start_enum_projects_original(
        reinterpret_cast<void*>(file_list_ptr),
        ".vid");

    const std::wstring start_message =
        L"[EVER2] Replay enumerate kick: started=" + std::to_wstring(started ? 1 : 0) +
        L" fileList=" + std::to_wstring(file_list_ptr);
    ever::platform::LogDebug(start_message.c_str());

    if (!started) {
        EndEnumerateSession();
        return;
    }

    for (int i = 0; i < kEnumerateKickPollIterations; ++i) {
        bool op_result = false;
        const bool completed = g_replay_file_manager_check_enum_projects_original(&op_result);

        if (completed) {
            g_last_enumerate_projects_completed.store(true, std::memory_order_release);
            g_last_enumerate_projects_result.store(op_result, std::memory_order_release);
            EndEnumerateSession();
            const std::wstring done_message =
                L"[EVER2] Replay enumerate kick: completed result=" + std::to_wstring(op_result ? 1 : 0) +
                L" polls=" + std::to_wstring(i + 1);
            ever::platform::LogDebug(done_message.c_str());
            return;
        }
        Sleep(kEnumerateKickPollSleepMs);
    }

    EndEnumerateSession();
    ever::platform::LogDebug(L"[EVER2] Replay enumerate kick: timed out waiting for completion.");
}

void InstallHookNoThrow() {
    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    int matched_candidate = -1;
    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::ReplayEnumerateProjects,
        L"[EVER2] Replay project logger",
        &matched_candidate);
    if (function_start == 0) {
        return;
    }

    if (!InstallDetourForResolvedAddress(
        function_start,
        reinterpret_cast<void*>(&HookedEnumerate),
        &g_enumerate_original,
        g_enumerate_detour,
        L"[EVER2] Replay project logger: failed to install detour. hr=")) {
        return;
    }

    const std::wstring message =
        L"[EVER2] Replay project logger hook installed. candidateIndex=" + std::to_wstring(matched_candidate) +
        L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

void InstallLoadMontageHookNoThrow() {
    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    int matched_candidate = -1;
    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::ReplayLoadMontage,
        L"[EVER2] Replay load montage logger",
        &matched_candidate);
    if (function_start == 0) {
        return;
    }

    if (!InstallDetourForResolvedAddress(
        function_start,
        reinterpret_cast<void*>(&HookedLoadMontage),
        &g_load_montage_original,
        g_load_montage_detour,
        L"[EVER2] Replay load montage logger: failed to install detour. hr=")) {
        return;
    }

    const std::wstring message =
        L"[EVER2] Replay load montage hook installed. candidateIndex=" + std::to_wstring(matched_candidate) +
        L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

void InstallReplayFileManagerStartEnumerateProjectFilesHookNoThrow() {
    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    int matched_candidate = -1;
    const uint64_t function_start = ResolvePatternToFunctionStart(
        scanner,
        ever::hooking::GameFunctionPatternId::ReplayFileManagerStartEnumerateProjectFiles,
        L"[EVER2] ReplayFileManager::StartEnumerateProjectFiles",
        &matched_candidate);
    if (function_start == 0) {
        return;
    }

    if (!InstallDetourForResolvedAddress(
        function_start,
        reinterpret_cast<void*>(&HookedReplayFileManagerStartEnumerateProjectFiles),
        &g_replay_file_manager_start_enum_projects_original,
        g_replay_file_manager_start_enum_projects_detour,
        L"[EVER2] ReplayFileManager::StartEnumerateProjectFiles hook install failed. hr=")) {
        return;
    }

    const std::wstring message =
        L"[EVER2] ReplayFileManager::StartEnumerateProjectFiles hook installed. candidateIndex=" +
        std::to_wstring(matched_candidate) + L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

void InstallReplayFileManagerCheckEnumerateProjectFilesHookNoThrow() {
    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    int matched_candidate = -1;
    const uint64_t matched_address = ResolvePatternToAddress(
        scanner,
        ever::hooking::GameFunctionPatternId::ReplayFileManagerCheckEnumerateProjectFiles,
        L"[EVER2] ReplayFileManager::CheckEnumerateProjectFiles",
        &matched_candidate,
        1);
    const uint64_t function_start = ResolveCheckEnumerateHookAddress(matched_address);
    if (function_start == 0) {
        return;
    }

    const std::wstring address_message =
        L"[EVER2] ReplayFileManager::CheckEnumerateProjectFiles: matchedAddress=" +
        std::to_wstring(matched_address) + L" hookAddress=" + std::to_wstring(function_start);
    ever::platform::LogDebug(address_message.c_str());

    if (!InstallDetourForResolvedAddress(
        function_start,
        reinterpret_cast<void*>(&HookedReplayFileManagerCheckEnumerateProjectFiles),
        &g_replay_file_manager_check_enum_projects_original,
        g_replay_file_manager_check_enum_projects_detour,
        L"[EVER2] ReplayFileManager::CheckEnumerateProjectFiles hook install failed. hr=")) {
        return;
    }

    const std::wstring message =
        L"[EVER2] ReplayFileManager::CheckEnumerateProjectFiles hook installed. candidateIndex=" +
        std::to_wstring(matched_candidate) + L" functionStart=" + std::to_wstring(function_start);
    ever::platform::LogDebug(message.c_str());
}

}

void EnsureHookInstalled() {
    if (AreCoreHooksInstalled()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last_attempt = g_last_install_attempt_tick.load(std::memory_order_acquire);
    if (last_attempt != 0 && (now - last_attempt) < 1500) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_install_mutex);
    if (AreCoreHooksInstalled()) {
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

    if (g_enumerate_detour == nullptr) {
        InstallHookNoThrow();
    }
    if (g_load_montage_detour == nullptr) {
        InstallLoadMontageHookNoThrow();
    }
    if (g_replay_file_manager_start_enum_projects_detour == nullptr) {
        InstallReplayFileManagerStartEnumerateProjectFilesHookNoThrow();
    }
    if (g_replay_file_manager_check_enum_projects_detour == nullptr) {
        InstallReplayFileManagerCheckEnumerateProjectFilesHookNoThrow();
    }
}

void PrimeHookInstallationAsync() {
    // TODO: Fix this
    EnsureHookInstalled();
}

bool IsHookInstalled() {
    return AreCoreHooksInstalled();
}

bool HasSnapshotReady() {
    return g_has_last_snapshot;
}

void LogSnapshotForUiTrigger() {
    if (!EnsureEnumerateHookInstalledForUi()) {
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
            L" loadMontageHits=" + std::to_wstring(g_load_montage_hook_hits.load(std::memory_order_relaxed)) +
            L". Open the in-game load flow once to populate it.";
        ever::platform::LogDebug(message.c_str());
        return;
    }

    LogSnapshot(L"ui-trigger latest", nullptr, g_last_snapshot);
    LogDecodedProjects(g_last_snapshot);
}

bool TryBuildProjectsJsonForUiTrigger(std::string& out_json, std::wstring& out_error) {
    out_json.clear();
    out_error.clear();

    if (!EnsureEnumerateHookInstalledForUi()) {
        out_error =
            L"Replay enumeration hook is not installed yet. Open Rockstar Editor load flow first and retry.";
        return false;
    }

    if (!g_has_last_snapshot) {
        TryKickNativeProjectEnumeration();
    }

    if (!g_has_last_snapshot) {
        out_error =
            L"Replay snapshot is not ready yet. Open Rockstar Editor load flow once, then click Load project again.";
        return false;
    }

    if (g_last_enumerate_projects_completed.load(std::memory_order_acquire) &&
        !g_last_enumerate_projects_result.load(std::memory_order_acquire)) {
        out_error = L"Project enumeration completed but returned failure in native ReplayFileManager.";
        return false;
    }

    return BuildProjectsJsonFromSnapshot(g_last_snapshot, out_json, out_error);
}

}
