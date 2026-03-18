#pragma once

#include <string>

namespace ever::features::replay_project_logger {

// Installs the replay-project enumeration detour once and then becomes a no-op.
void EnsureHookInstalled();

// Emits a readable snapshot to the debug log for UI-driven testing.
void LogSnapshotForUiTrigger();

// Builds grouped replay project/clip data for the UI bridge payload.
// Returns false and writes a user-readable reason to out_error when data is unavailable.
bool TryBuildProjectsJsonForUiTrigger(std::string& out_json, std::wstring& out_error);

}
