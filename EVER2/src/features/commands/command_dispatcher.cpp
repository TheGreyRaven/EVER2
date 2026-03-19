#include "ever/features/commands/command_dispatcher.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/features/editor_projects/editor_projects_bridge.h"
#include "ever/features/exit_rockstar_editor/exit_rockstar_editor_action.h"
#include "ever/features/quit_game/quit_game_action.h"
#include "ever/features/replay_project_logger/replay_project_logger.h"
#include "ever/platform/debug_console.h"
#include "ever/utils/command_json_utils.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <string>

namespace ever::features::commands {

namespace {

using Json = nlohmann::json;

constexpr int kMaxMessagesPerPump = 16;

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

void SendCommandResponse(
    const std::string& action,
    const std::string& request_id,
    const char* status,
    const std::string& message,
    const Json& extra = Json::object()) {
    Json response;
    response["event"] = "ever2_command_response";
    response["action"] = action;
    response["status"] = status;
    if (!request_id.empty()) {
        response["requestId"] = request_id;
    }
    if (!message.empty()) {
        response["message"] = message;
    }

    if (extra.is_object()) {
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            response[it.key()] = it.value();
        }
    }

    ever::browser::SendCefMessage(response.dump());
}

void DispatchMessage(const std::string& payload) {
    ever::utils::command_json::ParsedCommandPayload parsed;
    std::string parse_error;
    if (!ever::utils::command_json::ParseCommandPayload(payload, parsed, parse_error)) {
        SendCommandResponse("", "", "error", parse_error);
        return;
    }

    const std::string& action = parsed.action;
    const std::string& request_id = parsed.request_id;

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

    if (action == "load_project") {
        const ULONGLONG load_begin = GetTickCount64();
        ever::platform::LogDebug(L"[EVER2] Command 'load_project': ensuring replay project hooks on-demand.");
        ever::features::replay_project_logger::LogSnapshotForUiTrigger();
        std::string projects_payload;
        std::wstring load_error;
        if (!ever::features::replay_project_logger::TryBuildProjectsJsonForUiTrigger(projects_payload, load_error)) {
            const std::wstring message = L"[EVER2] Command 'load_project' failed: " + load_error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(load_error));
            return;
        }

        Json projects_event = Json::parse(projects_payload, nullptr, false);
        if (!projects_event.is_discarded() && projects_event.is_object() && !request_id.empty()) {
            projects_event["requestId"] = request_id;
            ever::browser::SendCefMessage(projects_event.dump());
        } else {
            ever::browser::SendCefMessage(projects_payload);
        }

        ever::platform::LogDebug(L"[EVER2] Command 'load_project' accepted and replay data payload was sent.");
        Json extra;
        if (!projects_event.is_discarded() && projects_event.is_object()) {
            extra["projectCount"] = projects_event.value("projectCount", 0);
        }
        const ULONGLONG load_elapsed = GetTickCount64() - load_begin;
        extra["elapsedMs"] = load_elapsed;
        {
            const std::wstring message =
                L"[EVER2] Command 'load_project' completed. elapsedMs=" + std::to_wstring(load_elapsed) +
                L" hookInstalled=" + std::to_wstring(ever::features::replay_project_logger::IsHookInstalled() ? 1 : 0) +
                L" snapshotReady=" + std::to_wstring(ever::features::replay_project_logger::HasSnapshotReady() ? 1 : 0);
            ever::platform::LogDebug(message.c_str());
        }
        SendCommandResponse(action, request_id, "accepted", "Replay project payload sent to UI.", extra);
        return;
    }

    if (action == "add_clip_to_project") {
        ever::platform::LogDebug(L"[EVER2] Command 'add_clip_to_project': ensuring editor project hooks on-demand.");
        const Json& data = parsed.data;
        const int source_index = data.value("sourceClipIndex", -1);
        const int destination_index = data.value("destinationIndex", -1);

        std::wstring add_error;
        const bool add_ok = ever::features::editor_projects::AddClipToCurrentProject(
            source_index,
            destination_index,
            add_error);
        if (!add_ok) {
            const std::wstring message = L"[EVER2] Command 'add_clip_to_project' failed: " + add_error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(add_error));
            return;
        }

        std::string projects_payload;
        std::wstring load_error;
        if (ever::features::replay_project_logger::TryBuildProjectsJsonForUiTrigger(projects_payload, load_error)) {
            Json projects_event = Json::parse(projects_payload, nullptr, false);
            if (!projects_event.is_discarded() && projects_event.is_object() && !request_id.empty()) {
                projects_event["requestId"] = request_id;
                ever::browser::SendCefMessage(projects_event.dump());
            } else {
                ever::browser::SendCefMessage(projects_payload);
            }
        }

        Json extra;
        extra["sourceClipIndex"] = source_index;
        extra["destinationIndex"] = destination_index;
        SendCommandResponse(action, request_id, "accepted", "Clip add requested through native project hooks.", extra);
        return;
    }

    if (action == "save_project") {
        ever::platform::LogDebug(L"[EVER2] Command 'save_project': ensuring editor project hooks on-demand.");
        std::wstring save_error;
        const bool save_ok = ever::features::editor_projects::SaveCurrentProject(save_error);
        if (!save_ok) {
            const std::wstring message = L"[EVER2] Command 'save_project' failed: " + save_error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(save_error));
            return;
        }

        SendCommandResponse(action, request_id, "accepted", "Native SaveProject executed.");
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
