#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ever::utils::command_json {

struct ParsedCommandPayload {
    std::string action;
    std::string request_id;
    nlohmann::json data;
};

inline bool ParseCommandPayload(
    const std::string& payload,
    ParsedCommandPayload& out_payload,
    std::string& out_error) {
    out_payload = {};
    out_error.clear();

    const nlohmann::json request = nlohmann::json::parse(payload, nullptr, false);
    if (request.is_discarded() || !request.is_object()) {
        out_error = "Invalid JSON payload.";
        return false;
    }

    out_payload.action = request.value("action", std::string());
    out_payload.request_id = request.value("requestId", std::string());
    if (request.contains("data") && request["data"].is_object()) {
        out_payload.data = request["data"];
    } else {
        out_payload.data = nlohmann::json::object();
    }
    return true;
}

}
