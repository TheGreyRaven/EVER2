#pragma once

namespace ever::features::replay_project_logger {

// Installs the replay-project enumeration detour once and then becomes a no-op.
void EnsureHookInstalled();

// Emits a readable snapshot to the debug log for UI-driven testing.
void LogSnapshotForUiTrigger();

}
