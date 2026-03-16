#pragma once

#include <windows.h>

#include <eh.h>
#include <exception>
#include <type_traits>

namespace ever::hooking {

class SehException final : public std::exception {
public:
    explicit SehException(unsigned int code) noexcept : code_(code) {}

    const char* what() const noexcept override {
        return "SEH exception occurred during hook operation";
    }

    unsigned int code() const noexcept {
        return code_;
    }

private:
    unsigned int code_;
};

inline void __cdecl SehTranslator(unsigned int code, EXCEPTION_POINTERS*) {
    throw SehException(code);
}

class SehTranslatorGuard final {
public:
    explicit SehTranslatorGuard(_se_translator_function translator) noexcept
        : previous_(_set_se_translator(translator)) {}

    ~SehTranslatorGuard() {
        _set_se_translator(previous_);
    }

    SehTranslatorGuard(const SehTranslatorGuard&) = delete;
    SehTranslatorGuard& operator=(const SehTranslatorGuard&) = delete;

private:
    _se_translator_function previous_;
};

template <typename To, typename From>
inline To ForceCast(From value) {
    static_assert(sizeof(To) == sizeof(From), "ForceCast requires equal size types.");
    return reinterpret_cast<To>(value);
}

}
