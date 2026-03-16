#pragma once

namespace ever::platform {

void InitializeDebugConsole();
void ShutdownDebugConsole();
void LogDebug(const wchar_t* message);

}
