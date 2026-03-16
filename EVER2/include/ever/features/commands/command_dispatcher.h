#pragma once

#include <string>

namespace ever::features::commands {

// Polls queued CEF commands and dispatches a bounded batch each tick.
void PumpQueuedCommands();

}
