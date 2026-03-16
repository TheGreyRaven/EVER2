#pragma once

#include "ever/platform/debug_console.h"

#include <polyhook2/PE/EatHook.hpp>
#include <polyhook2/PE/IatHook.hpp>

#include <memory>
#include <string>

namespace ever::hooking {

template <typename FuncType>
HRESULT HookNamedImport(const std::string& dll_name,
                        const std::string& api_name,
                        void* hook_function,
                        FuncType* original_function,
                        std::shared_ptr<PLH::IatHook>& iat_hook) {
    if (iat_hook != nullptr) {
        return S_OK;
    }

    auto* new_hook = new PLH::IatHook(
        dll_name,
        api_name,
        reinterpret_cast<uint64_t>(hook_function),
        reinterpret_cast<uint64_t*>(original_function),
        L"");

    if (!new_hook->hook()) {
        *original_function = nullptr;
        delete new_hook;
        const std::wstring message =
            L"[EVER2] Failed IAT hook: " + std::wstring(dll_name.begin(), dll_name.end()) +
            L"!" + std::wstring(api_name.begin(), api_name.end());
        ever::platform::LogDebug(message.c_str());
        return E_FAIL;
    }

    iat_hook.reset(new_hook);
    return S_OK;
}

template <typename FuncType>
HRESULT HookNamedExport(const std::wstring& dll_name,
                        const std::string& api_name,
                        void* hook_function,
                        FuncType* original_function,
                        std::shared_ptr<PLH::EatHook>& eat_hook) {
    if (eat_hook != nullptr) {
        return S_OK;
    }

    auto* new_hook = new PLH::EatHook(
        api_name,
        dll_name,
        reinterpret_cast<uint64_t>(hook_function),
        reinterpret_cast<uint64_t*>(original_function));

    if (!new_hook->hook()) {
        *original_function = nullptr;
        delete new_hook;
        const std::wstring message =
            L"[EVER2] Failed EAT hook: " + std::wstring(api_name.begin(), api_name.end());
        ever::platform::LogDebug(message.c_str());
        return E_FAIL;
    }

    eat_hook.reset(new_hook);
    return S_OK;
}

}
