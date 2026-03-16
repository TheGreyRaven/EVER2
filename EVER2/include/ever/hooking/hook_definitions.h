#pragma once

#include "ever/hooking/import_export_hook.h"
#include "ever/hooking/member_function_hook.h"
#include "ever/hooking/x64_detour.h"

#include <memory>

#define EVER2_DEFINE_X64_HOOK(FUNCTION_NAME, RETURN_TYPE, ...)                                         \
    namespace GameHooks {                                                                               \
    namespace FUNCTION_NAME {                                                                           \
    RETURN_TYPE Implementation(__VA_ARGS__);                                                            \
    using Type = RETURN_TYPE (*)(__VA_ARGS__);                                                          \
    inline Type OriginalFunc = nullptr;                                                                 \
    inline std::shared_ptr<PLH::x64Detour> Hook;                                                        \
    }                                                                                                   \
    }

#define EVER2_INSTALL_X64_HOOK(FUNCTION_NAME, ADDRESS)                                                  \
    ::ever::hooking::HookX64Function(                                                                   \
        ADDRESS,                                                                                        \
        reinterpret_cast<void*>(GameHooks::FUNCTION_NAME::Implementation),                              \
        &GameHooks::FUNCTION_NAME::OriginalFunc,                                                        \
        GameHooks::FUNCTION_NAME::Hook)

#define EVER2_DEFINE_NAMED_IMPORT_HOOK(FUNCTION_NAME, RETURN_TYPE, ...)                                \
    namespace ImportHooks {                                                                             \
    namespace FUNCTION_NAME {                                                                           \
    RETURN_TYPE Implementation(__VA_ARGS__);                                                            \
    using Type = RETURN_TYPE (*)(__VA_ARGS__);                                                          \
    inline Type OriginalFunc = nullptr;                                                                 \
    inline std::shared_ptr<PLH::IatHook> Hook;                                                          \
    }                                                                                                   \
    }

#define EVER2_INSTALL_NAMED_IMPORT_HOOK(DLL_NAME, API_NAME, FUNCTION_NAME)                             \
    ::ever::hooking::HookNamedImport(                                                                   \
        DLL_NAME,                                                                                       \
        API_NAME,                                                                                       \
        reinterpret_cast<void*>(ImportHooks::FUNCTION_NAME::Implementation),                            \
        &ImportHooks::FUNCTION_NAME::OriginalFunc,                                                      \
        ImportHooks::FUNCTION_NAME::Hook)

#define EVER2_DEFINE_MEMBER_HOOK(BASE_CLASS, METHOD_NAME, VFUNC_INDEX, RETURN_TYPE, ...)              \
    namespace BASE_CLASS##Hooks {                                                                       \
    namespace METHOD_NAME {                                                                             \
    RETURN_TYPE Implementation(BASE_CLASS* p_this, __VA_ARGS__);                                        \
    using Type = RETURN_TYPE (*)(BASE_CLASS * p_this, __VA_ARGS__);                                     \
    inline Type OriginalFunc = nullptr;                                                                 \
    inline ::ever::hooking::MemberHookInfo Info(VFUNC_INDEX);                                           \
    }                                                                                                   \
    }

#define EVER2_INSTALL_MEMBER_HOOK(BASE_CLASS, METHOD_NAME, INSTANCE_PTR)                               \
    ::ever::hooking::HookMemberFunction(                                                                \
        INSTANCE_PTR,                                                                                   \
        BASE_CLASS##Hooks::METHOD_NAME::Implementation,                                                 \
        &BASE_CLASS##Hooks::METHOD_NAME::OriginalFunc,                                                  \
        BASE_CLASS##Hooks::METHOD_NAME::Info)
