#include "ever/browser/native_overlay_renderer_internal.h"

#include "ever/platform/debug_console.h"

#include <cwchar>

namespace ever::browser::native_overlay_internal {

void Log(const wchar_t* message) {
    ::ever::platform::LogDebug(message);
}

std::wstring PtrToString(const void* value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"0x%p", value);
    return buffer;
}

std::wstring HrToString(HRESULT hr) {
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

void LogRateLimited(uint64_t counter, uint64_t period, const wchar_t* message) {
    if (counter == 1 || (period != 0 && (counter % period) == 0)) {
        Log(message);
    }
}

}
