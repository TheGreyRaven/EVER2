#include "ever/features/commands/command_dispatcher.h"

#include "ever/browser/native_overlay_renderer.h"
#include "ever/features/rockstar_editor_menu/rockstar_editor_menu_bridge.h"
#include "ever/features/exit_rockstar_editor/exit_rockstar_editor_action.h"
#include "ever/features/quit_game/quit_game_action.h"
#include "ever/features/replay_project_logger/replay_project_logger.h"
#include "ever/platform/debug_console.h"
#include "ever/utils/command_json_utils.h"
#include "ever/utils/replay_clip_utils.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>

namespace ever::features::commands {

namespace {

using Json = nlohmann::json;

constexpr int kMaxMessagesPerPump = 16;
constexpr size_t kMaxDeferredCommands = 128;

std::mutex g_deferred_commands_mutex;
std::deque<std::string> g_deferred_commands;
thread_local bool g_is_game_thread_dispatch = false;

bool ShouldDeferToGameThread(const std::string& action, const Json& data) {
    if (action == "add_clip_to_project" || action == "save_project") {
        return true;
    }

    if (action == "load_project") {
        const std::string project_path = data.value("projectPath", std::string());
        const bool native_load_requested = data.value("nativeLoad", false) || !project_path.empty();
        return native_load_requested;
    }

    return false;
}

void EnqueueDeferredCommand(const std::string& payload) {
    std::lock_guard<std::mutex> lock(g_deferred_commands_mutex);
    if (g_deferred_commands.size() >= kMaxDeferredCommands) {
        g_deferred_commands.pop_front();
    }
    g_deferred_commands.push_back(payload);
}

bool TryPopDeferredCommand(std::string& out_payload) {
    std::lock_guard<std::mutex> lock(g_deferred_commands_mutex);
    if (g_deferred_commands.empty()) {
        return false;
    }

    out_payload = std::move(g_deferred_commands.front());
    g_deferred_commands.pop_front();
    return true;
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

    if (!g_is_game_thread_dispatch && ShouldDeferToGameThread(action, parsed.data)) {
        EnqueueDeferredCommand(payload);
        SendCommandResponse(action, request_id, "accepted", "Command queued for game-thread execution.");
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
        const Json& data = parsed.data;
        const std::string project_path = data.value("projectPath", std::string());
        const int project_index = data.value("projectIndex", -1);
        const bool native_load_requested = data.value("nativeLoad", false) || !project_path.empty();

        if (native_load_requested) {
            std::wstring native_load_error;
            const bool native_load_ok =
                ever::features::rockstar_editor_menu::StartLoadProjectByPath(project_path, native_load_error);
            if (!native_load_ok) {
                const std::wstring message = L"[EVER2] Command 'load_project' native StartLoadProject failed: " + native_load_error;
                ever::platform::LogDebug(message.c_str());
                SendCommandResponse(action, request_id, "error", WideToUtf8(native_load_error));
                return;
            }

            const std::wstring message =
                L"[EVER2] Command 'load_project' requested native StartLoadProject. index=" +
                std::to_wstring(project_index) +
                L" path='" + std::wstring(project_path.begin(), project_path.end()) + L"'";
            ever::platform::LogDebug(message.c_str());
        }

        ever::platform::LogDebug(L"[EVER2] Command 'load_project': ensuring replay project hooks on-demand.");
        ever::features::replay_project_logger::LogSnapshotForUiTrigger();
        std::string projects_payload;
        std::wstring load_error;
        if (!ever::features::replay_project_logger::TryBuildProjectsJsonForUiTrigger(projects_payload, load_error)) {
            if (native_load_requested) {
                Json extra;
                extra["nativeLoadRequested"] = true;
                extra["nativeLoadProjectPath"] = project_path;
                if (project_index >= 0) {
                    extra["nativeLoadProjectIndex"] = project_index;
                }
                SendCommandResponse(
                    action,
                    request_id,
                    "accepted",
                    "Native project load requested. Replay payload refresh is temporarily unavailable.",
                    extra);
                return;
            }

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
        if (native_load_requested) {
            extra["nativeLoadRequested"] = true;
            extra["nativeLoadProjectPath"] = project_path;
            if (project_index >= 0) {
                extra["nativeLoadProjectIndex"] = project_index;
            }
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
        const std::string source_clip_name = data.value("sourceClipBaseName", std::string());
        const std::string source_owner_id_text = data.value("sourceClipOwnerIdText", std::string());

        uint64_t source_owner_id = 0;
        if (!source_owner_id_text.empty()) {
            source_owner_id = _strtoui64(source_owner_id_text.c_str(), nullptr, 10);
        }
        if (source_owner_id == 0) {
            source_owner_id = data.value("sourceClipOwnerId", static_cast<uint64_t>(0));
        }

        if (source_clip_name.empty()) {
            SendCommandResponse(
                action,
                request_id,
                "error",
                "sourceClipBaseName is required for add_clip_to_project.");
            return;
        }

        std::wstring add_error;
        int new_clip_count = 0;
        const bool add_ok = ever::features::rockstar_editor_menu::AddClipToCurrentProjectByName(
            source_clip_name,
            source_owner_id,
            -1,
            add_error,
            new_clip_count);

        if (!add_ok) {
            const std::wstring message = L"[EVER2] Command 'add_clip_to_project' failed: " + add_error;
            ever::platform::LogDebug(message.c_str());
            SendCommandResponse(action, request_id, "error", WideToUtf8(add_error));
            return;
        }

        {
            const std::string source_clip_path = data.value("sourceClipPath", std::string());
            std::string clip_preview_disk_path;
            bool clip_preview_exists = false;
            if (!source_clip_path.empty()) {
                const std::string clips_disk_dir =
                    ever::utils::replay_clip::ReplayUriToDiskPath("replay:/videos/clips/");
                const std::string preview_filename =
                    ever::utils::replay_clip::GuessPreviewPath(source_clip_path);
                if (!preview_filename.empty()) {
                    clip_preview_disk_path = clips_disk_dir + preview_filename;
                    clip_preview_exists =
                        ever::utils::replay_clip::FileExists(clip_preview_disk_path);
                }
            }

            Json clip_added;
            clip_added["event"] = "ever2_clip_added";
            clip_added["newClipCount"] = new_clip_count;
            clip_added["clipBaseName"] = source_clip_name;
            clip_added["clipOwnerId"] = source_owner_id;
            clip_added["clipPreviewDiskPath"] = clip_preview_disk_path;
            clip_added["clipPreviewExists"] = clip_preview_exists;
            ever::browser::SendCefMessage(clip_added.dump());
        }

        Json extra;
        extra["sourceClipBaseName"] = source_clip_name;
        extra["sourceClipOwnerId"] = source_owner_id;
        extra["newClipCount"] = new_clip_count;
        SendCommandResponse(action, request_id, "accepted", "Clip add executed through native project hooks.", extra);
        return;
    }

    if (action == "save_project") {
        ever::platform::LogDebug(L"[EVER2] Command 'save_project': ensuring editor project hooks on-demand.");
        std::wstring save_error;
        const bool save_ok = ever::features::rockstar_editor_menu::SaveCurrentProject(save_error);
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

void PumpDeferredGameThreadCommands() {
    int processed = 0;
    std::string payload;

    const bool previous = g_is_game_thread_dispatch;
    g_is_game_thread_dispatch = true;
    while (processed < kMaxMessagesPerPump && TryPopDeferredCommand(payload)) {
        DispatchMessage(payload);
        ++processed;
    }
    g_is_game_thread_dispatch = previous;

    if (processed > 0) {
        const std::wstring message =
            L"[EVER2] Processed deferred game-thread commands batch size=" + std::to_wstring(processed);
        ever::platform::LogDebug(message.c_str());
    }
}

}
