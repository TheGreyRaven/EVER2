#pragma once

#include <string>

namespace ever::features::commands {

// Polls queued CEF commands and dispatches a bounded batch each tick.
void PumpQueuedCommands();

// Executes commands that were deferred for a game-thread context.
void PumpDeferredGameThreadCommands();

}
