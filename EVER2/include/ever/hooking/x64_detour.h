#pragma once

#include "ever/hooking/hook_utility.h"
#include "ever/platform/debug_console.h"

#include <polyhook2/Detour/x64Detour.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace ever::hooking {

template <typename FuncType>
HRESULT HookX64Function(uint64_t target_address,
                        void* hook_function,
                        FuncType* original_function,
                        std::shared_ptr<PLH::x64Detour>& detour,
                        PLH::x64Detour::detour_scheme_t scheme = PLH::x64Detour::detour_scheme_t::RECOMMENDED) {
    if (detour != nullptr) {
        return S_OK;
    }

    auto* new_hook = new PLH::x64Detour(
        target_address,
        reinterpret_cast<uint64_t>(hook_function),
        reinterpret_cast<uint64_t*>(original_function));
    new_hook->setDetourScheme(scheme);

    bool hooked = false;
    SehTranslatorGuard translator_guard(&SehTranslator);

    try {
        hooked = new_hook->hook();
    } catch (const SehException& ex) {
        const std::wstring message =
            L"[EVER2] SEH while installing x64 detour at " + std::to_wstring(target_address) +
            L", code=" + std::to_wstring(ex.code());
        ever::platform::LogDebug(message.c_str());
    } catch (const std::exception& ex) {
        const std::wstring message =
            L"[EVER2] Exception while installing x64 detour at " + std::to_wstring(target_address) +
            L": " + std::wstring(ex.what(), ex.what() + std::char_traits<char>::length(ex.what()));
        ever::platform::LogDebug(message.c_str());
    }

    if (!hooked) {
        *original_function = nullptr;
        delete new_hook;
        const std::wstring message =
            L"[EVER2] Failed to install x64 detour at " + std::to_wstring(target_address);
        ever::platform::LogDebug(message.c_str());
        return E_FAIL;
    }

    detour.reset(new_hook);
    return S_OK;
}

}
