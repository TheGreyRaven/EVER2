#include "ever/features/quit_game/quit_game_action.h"

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace ever::features::quit_game {

namespace {

using StartShutdownTasksFn = void(__fastcall*)(void*);

std::atomic<bool> g_resolve_attempted{false};
std::atomic<uint64_t> g_start_shutdown_addr{0};
std::atomic<uint64_t> g_network_exit_flow_instance{0};
std::atomic<bool> g_quit_in_flight{false};

// TODO: This is a hacky attempt to find the NetworkExitFlow instance, this should be replaced with a more robust solution in the future.
uint64_t ResolveNetworkExitFlowInstanceFromCallsites(uint64_t function_addr) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return 0;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    const unsigned char* base = reinterpret_cast<const unsigned char*>(module);
    const size_t size = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);

    for (size_t i = 0; i + 5 < size; ++i) {
        if (base[i] != 0xE8) {
            continue;
        }

        const int32_t rel = *reinterpret_cast<const int32_t*>(base + i + 1);
        const uint64_t target = reinterpret_cast<uint64_t>(base + i + 5 + rel);
        if (target != function_addr) {
            continue;
        }

        const size_t start = (i > 24) ? (i - 24) : 0;
        for (size_t j = start; j + 7 < i; ++j) {
            if (base[j] == 0x48 && base[j + 1] == 0x8D && base[j + 2] == 0x0D) {
                const int32_t disp = *reinterpret_cast<const int32_t*>(base + j + 3);
                const uint64_t candidate_addr = reinterpret_cast<uint64_t>(base + j + 7 + disp);
                if (candidate_addr != 0) {
                    return candidate_addr;
                }
            }

            if (base[j] == 0x48 && base[j + 1] == 0x8B && base[j + 2] == 0x0D) {
                const int32_t disp = *reinterpret_cast<const int32_t*>(base + j + 3);
                const uint64_t ptr_addr = reinterpret_cast<uint64_t>(base + j + 7 + disp);
                if (ptr_addr != 0) {
                    const uint64_t candidate = *reinterpret_cast<const uint64_t*>(ptr_addr);
                    if (candidate != 0) {
                        return candidate;
                    }
                }
            }
        }
    }

    return 0;
}

bool ResolveStartShutdownAddressAndContext() {
    if (g_resolve_attempted.exchange(true, std::memory_order_acq_rel)) {
        ever::platform::LogDebug(L"[EVER2] QuitGame resolver reused cached result.");
        return g_start_shutdown_addr.load(std::memory_order_acquire) != 0 &&
               g_network_exit_flow_instance.load(std::memory_order_acquire) != 0;
    }

    ever::platform::LogDebug(L"[EVER2] QuitGame resolver started.");

    ever::hooking::PatternScanner scanner;
    scanner.Initialize();

    const char* const* candidates =
        ever::hooking::GetGameFunctionPatternCandidates(ever::hooking::GameFunctionPatternId::StartShutdownTasks);
    if (candidates == nullptr) {
        ever::platform::LogDebug(L"[EVER2] QuitGame resolve failed: no StartShutdownTasks patterns.");
        return false;
    }

    uint64_t start_shutdown = 0;
    for (size_t i = 0; candidates[i] != nullptr; ++i) {
        start_shutdown = 0;
        const std::string pattern_name = std::string("start_shutdown_tasks_") + std::to_string(i);
        {
            const std::wstring message =
                L"[EVER2] QuitGame scanning candidate index=" + std::to_wstring(i);
            ever::platform::LogDebug(message.c_str());
        }
        scanner.AddPattern(pattern_name, candidates[i], &start_shutdown);
        scanner.PerformScan();

        if (start_shutdown != 0) {
            const std::wstring message =
                L"[EVER2] QuitGame candidate matched index=" + std::to_wstring(i) +
                L" addr=" + std::to_wstring(start_shutdown);
            ever::platform::LogDebug(message.c_str());
            break;
        }
    }

    if (start_shutdown == 0) {
        ever::platform::LogDebug(L"[EVER2] Failed to resolve NetworkExitFlow::StartShutdownTasks pattern.");
        return false;
    }

    const uint64_t exit_flow_this = ResolveNetworkExitFlowInstanceFromCallsites(start_shutdown);
    if (exit_flow_this == 0) {
        ever::platform::LogDebug(L"[EVER2] Failed to recover NetworkExitFlow instance from StartShutdownTasks callsites.");
        return false;
    }

    g_start_shutdown_addr.store(start_shutdown, std::memory_order_release);
    g_network_exit_flow_instance.store(exit_flow_this, std::memory_order_release);

    const std::wstring message =
        L"[EVER2] Resolved StartShutdownTasks=" + std::to_wstring(start_shutdown) +
        L" NetworkExitFlowInstance=" + std::to_wstring(exit_flow_this);
    ever::platform::LogDebug(message.c_str());
    return true;
}

}

bool Execute(std::wstring& out_error) {
    out_error.clear();
    ever::platform::LogDebug(L"[EVER2] QuitGame Execute called.");

    if (g_quit_in_flight.exchange(true, std::memory_order_acq_rel)) {
        out_error = L"Quit action is already in progress.";
        return false;
    }

    const auto release_guard = std::unique_ptr<void, void(*)(void*)>(
        reinterpret_cast<void*>(1),
        [](void*) {
            g_quit_in_flight.store(false, std::memory_order_release);
        });

    if (!ResolveStartShutdownAddressAndContext()) {
        out_error = L"Unable to resolve quit path (StartShutdownTasks/context).";
        return false;
    }

    const uint64_t addr = g_start_shutdown_addr.load(std::memory_order_acquire);
    const uint64_t this_ptr = g_network_exit_flow_instance.load(std::memory_order_acquire);
    if (addr == 0 || this_ptr == 0) {
        out_error = L"Quit path resolved to invalid address/context.";
        return false;
    }

    const auto fn = reinterpret_cast<StartShutdownTasksFn>(addr);
    fn(reinterpret_cast<void*>(this_ptr));
    ever::platform::LogDebug(L"[EVER2] Executed Quit Game action via NetworkExitFlow::StartShutdownTasks.");
    return true;
}

}
