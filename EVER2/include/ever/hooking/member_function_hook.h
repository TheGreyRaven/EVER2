#pragma once

#include "ever/hooking/hook_utility.h"
#include "ever/platform/debug_console.h"

#include <polyhook2/Virtuals/VFuncSwapHook.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace ever::hooking {

struct MemberHookInfo {
    explicit MemberHookInfo(uint16_t vtable_index_in) : vtable_index(vtable_index_in) {}

    uint16_t vtable_index;
    std::shared_ptr<PLH::VFuncSwapHook> hook;
};

template <typename ClassType, typename FuncType>
HRESULT HookMemberFunction(ClassType* instance,
                           FuncType hook_function,
                           FuncType* original_function,
                           MemberHookInfo& hook_info) {
    if (hook_info.hook != nullptr) {
        return S_OK;
    }

    const PLH::VFuncMap hook_map = {
        {hook_info.vtable_index, reinterpret_cast<uint64_t>(hook_function)},
    };
    PLH::VFuncMap original_functions;

    auto* new_hook = new PLH::VFuncSwapHook(
        reinterpret_cast<uint64_t>(instance),
        hook_map,
        &original_functions);

    if (!new_hook->hook()) {
        delete new_hook;
        const std::wstring message =
            L"[EVER2] Failed virtual member hook at vtable index=" +
            std::to_wstring(hook_info.vtable_index);
        ever::platform::LogDebug(message.c_str());
        return E_FAIL;
    }

    *original_function = ForceCast<FuncType, uint64_t>(original_functions[hook_info.vtable_index]);
    hook_info.hook.reset(new_hook);
    return S_OK;
}

}
