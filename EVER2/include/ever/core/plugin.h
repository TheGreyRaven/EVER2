#pragma once

namespace ever::core {

void ScriptMain();
void Shutdown();
bool HasScriptMainStarted();
bool IsOverlayInitAttempted();
bool IsOverlayInitialized();
unsigned long long GetScriptMainLoopCount();

}
