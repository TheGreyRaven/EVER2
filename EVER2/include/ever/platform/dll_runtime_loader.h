#pragma once

#include <windows.h>

namespace ever::platform {

bool ConfigureRuntimeDllDirectory(HMODULE module_handle);
bool IsFiveMHostProcess();

}
