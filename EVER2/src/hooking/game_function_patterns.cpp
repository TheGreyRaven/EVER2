#include "ever/hooking/game_function_patterns.h"

namespace ever::hooking {

namespace {

const char* const kStartShutdownTasksPatterns[] = {
    "40 53 48 83 EC 20 8B 01 48 8B D9 83 E8 05 83 F8 09 76 ??",
    "BA B8 0B 00 00 4C 8B 00 48 8B C8 41 FF 90 58 01 00 00 8A 05 ?? ?? ?? ?? 88 43 1C",
    nullptr,
};

// TODO: None of these patterns are working, might be becuse I was tired.
const char* const kVideoEditorClosePatterns[] = {
    "53 48 83 EC 20 8B 05 ?? ?? ?? ?? 8A D9 85 C0 74 ?? 83 F8 05 74 ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ??",
    "84 DB 74 ?? B9 08 00 00 00 E8 ?? ?? ?? ?? 84 C0 75 ?? B2 01 33 C9 E8 ?? ?? ?? ?? 83 25 ?? ?? ?? ?? 00 C6 05 ?? ?? ?? ?? 00 C7 05 ?? ?? ?? ?? 05 00 00 00 48 83 C4 20 5B C3",
    "E8 ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 08 75 05 E8 ?? ?? ?? ?? 83",
    nullptr,
};

const char* const kReplayEnumerateProjectPatterns[] = {
    "48 8D 8E 38 01 00 00 BA 10 00 00 00 48 8D 98 48 01 00 00 E8 ?? ?? ?? ?? 8B 4B 08 BA 0C 00 00 00 89 48 08 48 8D 48 0C 48 2B D8 8A 04 0B 88 01 49 03 CE 49 2B D6 75 F3 33 DB",
    "FF 90 D0 00 00 00 8A D8 E9 ?? ?? ?? ?? 8A C3 E9 ?? ?? ?? ?? 4C 8B 44 24 58 4D 3B C7 74 ?? 49 8B 04 24 49 8B D0 49 8B CC FF 90 D8 00 00 00 33 C9 41 38 8E ?? ?? ?? ?? 75 ?? 8D 41 01 38 8D ?? ?? ?? ?? 74 ?? 88 87 ?? ?? ?? ?? 38 4C 24 40 74 ?? 88 87 ?? ?? ?? ?? 8B 87 ?? ?? ?? ?? 88 8F ?? ?? ?? ??",
    nullptr,
};

}

const char* const* GetGameFunctionPatternCandidates(GameFunctionPatternId id) {
    switch (id) {
    case GameFunctionPatternId::StartShutdownTasks:
        return kStartShutdownTasksPatterns;
    case GameFunctionPatternId::VideoEditorClose:
        return kVideoEditorClosePatterns;
    case GameFunctionPatternId::ReplayEnumerateProjects:
        return kReplayEnumerateProjectPatterns;
    default:
        return nullptr;
    }
}

const char* GetGameFunctionPatternName(GameFunctionPatternId id) {
    switch (id) {
    case GameFunctionPatternId::StartShutdownTasks:
        return "NetworkExitFlow::StartShutdownTasks";
    case GameFunctionPatternId::VideoEditorClose:
        return "CVideoEditorUi::Close";
    case GameFunctionPatternId::ReplayEnumerateProjects:
        return "fiDeviceReplay::Enumerate";
    default:
        return "UnknownPattern";
    }
}

}
