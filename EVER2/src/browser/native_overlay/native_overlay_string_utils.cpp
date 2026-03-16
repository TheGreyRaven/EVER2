#include "ever/browser/native_overlay_renderer_internal.h"

namespace ever::browser::native_overlay_internal {

std::string UrlEncodeDataPayload(const std::string& input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (const unsigned char ch : input) {
        const bool unreserved =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(kHex[(ch >> 4) & 0x0F]);
            out.push_back(kHex[ch & 0x0F]);
        }
    }
    return out;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }

    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return std::string();
    }

    std::string out(static_cast<size_t>(needed - 1), '\0');
    const int converted = WideCharToMultiByte(CP_UTF8,
                                              0,
                                              value.c_str(),
                                              -1,
                                              out.data(),
                                              needed,
                                              nullptr,
                                              nullptr);
    if (converted <= 1) {
        return std::string();
    }

    return out;
}

std::string BuildHtmlDataUrl(const std::string& html_utf8) {
    return std::string("data:text/html;charset=utf-8,") + UrlEncodeDataPayload(html_utf8);
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }

    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (needed <= 1) {
        return std::wstring();
    }

    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), needed);
    if (converted <= 1) {
        return std::wstring();
    }

    return out;
}

std::string JsQuote(const std::string& input) {
    static constexpr char kHex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(input.size() + 16);
    out.push_back('\'');

    for (const unsigned char ch : input) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '\'':
            out += "\\\'";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                out += "\\x";
                out.push_back(kHex[(ch >> 4) & 0x0F]);
                out.push_back(kHex[ch & 0x0F]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    out.push_back('\'');
    return out;
}

}
