#pragma once

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace ever::utils::replay_clip {

inline bool IsLikelyPrintable(char c) {
    return c >= 32 && c <= 126;
}

inline bool EndsWithInsensitive(const std::string& value, const char* suffix) {
    if (suffix == nullptr) {
        return false;
    }

    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len) {
        return false;
    }

    const size_t start = value.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[start + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) {
            return false;
        }
    }

    return true;
}

inline bool FileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline std::string ReplayUriToDiskPath(const std::string& path) {
    constexpr const char* kReplayPrefix = "replay:/";
    if (path.rfind(kReplayPrefix, 0) != 0) {
        return path;
    }

    char local_app_data[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return path;
    }

    std::string suffix = path.substr(std::strlen(kReplayPrefix));
    std::replace(suffix.begin(), suffix.end(), '/', '\\');

    std::string disk_path(local_app_data);
    disk_path += "\\Rockstar Games\\GTA V\\";
    disk_path += suffix;
    return disk_path;
}

inline std::string ReplaceExtension(const std::string& path, const char* old_ext, const char* new_ext) {
    if (old_ext == nullptr || new_ext == nullptr || path.empty()) {
        return std::string();
    }

    if (!EndsWithInsensitive(path, old_ext)) {
        return std::string();
    }

    const size_t old_len = std::strlen(old_ext);
    return path.substr(0, path.size() - old_len) + new_ext;
}

inline std::string GuessPreviewPath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    if (EndsWithInsensitive(path, ".vid") || EndsWithInsensitive(path, ".montage")) {
        return std::string();
    }

    std::string candidate = ReplaceExtension(path, ".clip", ".jpg");
    if (!candidate.empty()) {
        return candidate;
    }

    candidate = ReplaceExtension(path, ".montage", ".jpg");
    if (!candidate.empty()) {
        return candidate;
    }

    candidate = ReplaceExtension(path, ".xml", ".jpg");
    if (!candidate.empty()) {
        return candidate;
    }

    return path + ".jpg";
}

inline std::string BuildClipReplayPath(const std::string& clip_base_name) {
    if (clip_base_name.empty()) {
        return std::string();
    }

    return std::string("replay:/videos/clips/") + clip_base_name + ".clip";
}

inline std::vector<std::string> ExtractProjectClipBaseNames(const std::string& project_path) {
    const std::string disk_project_path = ReplayUriToDiskPath(project_path);
    std::ifstream file(disk_project_path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return {};
    }

    std::vector<std::string> result;
    std::string token;
    token.reserve(64);

    auto flush_token = [&]() {
        if (token.find("Clip-") != std::string::npos && token.size() >= 10) {
            if (std::find(result.begin(), result.end(), token) == result.end()) {
                result.push_back(token);
            }
        }
        token.clear();
    };

    for (const char raw : bytes) {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool is_valid =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == ' ';

        if (is_valid) {
            token.push_back(static_cast<char>(c));
            if (token.size() > 96) {
                flush_token();
            }
        } else {
            flush_token();
        }
    }

    flush_token();
    return result;
}

inline std::string GuessPreviewFromProjectClipNames(const std::string& project_path) {
    const auto clip_names = ExtractProjectClipBaseNames(project_path);
    for (const std::string& clip_name : clip_names) {
        const std::string replay_candidate = std::string("replay:/videos/clips/") + clip_name + ".jpg";
        const std::string disk_candidate = ReplayUriToDiskPath(replay_candidate);
        if (FileExists(disk_candidate)) {
            return replay_candidate;
        }
    }

    return std::string();
}

}
