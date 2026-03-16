#include "ever/features/commands/command_dispatcher.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/features/exit_rockstar_editor/exit_rockstar_editor_action.h"
#include "ever/features/quit_game/quit_game_action.h"
#include "ever/platform/debug_console.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace ever::features::commands {

namespace {

constexpr int kMaxMessagesPerPump = 16;

std::string EscapeJson(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
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

bool ExtractJsonStringField(const std::string& json, const char* key, std::string& out) {
    out.clear();

    const std::string needle = std::string("\"") + key + "\"";
    const size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }

    const size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    size_t value_pos = colon_pos + 1;
    while (value_pos < json.size() && (json[value_pos] == ' ' || json[value_pos] == '\t' || json[value_pos] == '\n' || json[value_pos] == '\r')) {
        ++value_pos;
    }

    if (value_pos >= json.size() || json[value_pos] != '"') {
        return false;
    }

    ++value_pos;
    bool escaped = false;
    for (size_t i = value_pos; i < json.size(); ++i) {
        const char c = json[i];
        if (!escaped && c == '\\') {
            escaped = true;
            continue;
        }
        if (!escaped && c == '"') {
            return true;
        }

        out.push_back(c);
        escaped = false;
    }

    out.clear();
    return false;
}

void SendCommandResponse(
    const std::string& action,
    const std::string& request_id,
    const char* status,
    const std::string& message) {
    std::string response =
        std::string("{\"event\":\"ever2_command_response\",\"action\":\"") + EscapeJson(action) +
        "\",\"status\":\"" + EscapeJson(status) + "\"";

    if (!request_id.empty()) {
        response += ",\"requestId\":\"" + EscapeJson(request_id) + "\"";
    }

    if (!message.empty()) {
        response += ",\"message\":\"" + EscapeJson(message) + "\"";
    }

    response += "}";
    ever::browser::SendCefMessage(response);
}

void DispatchMessage(const std::string& payload) {
    std::string action;
    std::string request_id;

    ExtractJsonStringField(payload, "action", action);
    ExtractJsonStringField(payload, "requestId", request_id);

    {
        const std::wstring message =
            L"[EVER2] Command dispatcher received payload. action='" +
            std::wstring(action.begin(), action.end()) +
            L"' requestId='" + std::wstring(request_id.begin(), request_id.end()) + L"'";
        ever::platform::LogDebug(message.c_str());
    }

    if (action.empty()) {
        SendCommandResponse("", request_id, "error", "Missing action field.");
        return;
    }

    if (action == "quit_game") {
        std::wstring error;
        const bool ok = ever::features::quit_game::Execute(error);
        if (ok) {
            ever::platform::LogDebug(L"[EVER2] Command 'quit_game' accepted.");
            SendCommandResponse(action, request_id, "accepted", "Quit flow started.");
        } else {
            const std::wstring message = L"[EVER2] Command 'quit_game' failed: " + error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(error));
        }
        return;
    }

    if (action == "exit_rockstar_editor") {
        std::wstring error;
        const bool ok = ever::features::exit_rockstar_editor::Execute(error);
        if (ok) {
            ever::platform::LogDebug(L"[EVER2] Command 'exit_rockstar_editor' accepted.");
            SendCommandResponse(action, request_id, "accepted", "Exit Rockstar Editor invoked.");
        } else {
            const std::wstring message = L"[EVER2] Command 'exit_rockstar_editor' failed: " + error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(error));
        }
        return;
    }

    SendCommandResponse(action, request_id, "error", "Unknown action.");
}

}

void PumpQueuedCommands() {
    int processed = 0;
    std::string payload;

    while (processed < kMaxMessagesPerPump && ever::browser::PollCefMessage(payload)) {
        DispatchMessage(payload);
        ++processed;
    }

    if (processed > 0) {
        const std::wstring message =
            L"[EVER2] Processed CEF command batch size=" + std::to_wstring(processed);
        ever::platform::LogDebug(message.c_str());
    }
}

}
