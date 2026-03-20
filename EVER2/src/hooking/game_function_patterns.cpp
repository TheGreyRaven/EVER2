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

const char* const kReplayLoadMontagePatterns[] = {
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B CA 49 8B F0",
    nullptr,
};

const char* const kReplayFileManagerStartEnumerateClipFilesPatterns[] = {
    "48 83 EC 28 4C 8B C1 4C 8B CA 48 8D 0D ?? ?? ?? ?? 33 D2 E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 95 C0 48 83 C4 28 C3",
    nullptr,
};

// This check routine is small and has siblings with near-identical bodies.
// Include surrounding structure (prologue + call + tail jump) to disambiguate.
const char* const kReplayFileManagerCheckEnumerateClipFilesPatterns[] = {
    "48 83 EC 28 8B 05 ?? ?? ?? ?? 85 C0 74 08 83 F8 02 75 14 C6 01 00 C6 01 01 33 C9 E8 ?? ?? ?? ?? B0 01 48 83 C4 28 C3 32 C0 EB ??",
    nullptr,
};

const char* const kReplayClipDataInitPatterns[] = {
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B C2 48 8B F9",
    nullptr,
};

const char* const kReplayClipPopulatePatterns[] = {
    "48 8B C4 48 89 58 10 48 89 68 18 56 57 41 56 48 81 EC 30 ?? 00 00",
    nullptr,
};

const char* const kReplayMontageLoadPatterns[] = {
    "48 8B C4 48 89 58 18 44 88 48 20 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 33 DB",
    nullptr,
};

const char* const kReplayFileManagerStartEnumerateProjectFilesPatterns[] = {
    "48 83 EC 28 4C 8B C1 4C 8B CA 48 8D 0D ?? ?? ?? ?? BA 01 00 00 00 E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 95 C0 48 83 C4 28 C3",
    nullptr,
};

// Starts at a nop due to very small function
const char* const kReplayFileManagerCheckEnumerateProjectFilesPatterns[] = {
    "90 8B 05 ?? ?? ?? ?? 85 C0 74 0D 83 F8 02 74 03 32 C0 C3 C6 01 00 EB 03 C6 01 01 B0 01 C3",
    nullptr,
};

const char* const kReplayMgrInternalStartEnumerateProjectFilesPatterns[] = {
    "48 8B CA 48 8D 15 ?? ?? ?? ?? E9 ?? ?? ?? ?? 90 48 83 EC 28 8B 05",
    nullptr,
};

const char* const kReplayMgrInternalCheckEnumerateProjectFilesPatterns[] = {
    "E9 ?? ?? ?? ?? CC E5 8B E9 ?? ?? ?? ?? CC",
    "E9 ?? ?? ?? ?? CC 21 BA ?? ?? ?? ?? 00 CC",
    nullptr,
};

const char* const kReplayMgrInternalStartEnumerateClipFilesPatterns[] = {
    "48 8B CA 48 8D 15 ?? ?? ?? ?? E9 ?? ?? ?? ?? 90 48 8B CA 48 8D 15 ?? ?? ?? ?? E9 ?? ?? ?? ?? 90 48 83 EC 28",
    nullptr,
};

const char* const kReplayMgrInternalCheckEnumerateClipFilesPatterns[] = {
    "E9 ?? ?? ?? ?? CC E5 8B E9 ?? ?? ?? ?? CC",
    "E9 ?? ?? ?? ?? CC 21 BA ?? ?? ?? ?? 00 CC",
    nullptr,
};

const char* const kVideoEditorProjectPrepareStagingClipByNamePatterns[] = {
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 33 DB 41 8B F1 49",
    nullptr,
};

const char* const kVideoEditorProjectMoveStagingClipToProjectPatterns[] = {
    "48 89 5C 24 08 57 48 83 EC 20 48 83 B9 20 03",
    nullptr,
};

const char* const kVideoEditorProjectSaveProjectPatterns[] = {
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 83 B9 20",
    nullptr,
};

const char* const kVideoEditorInterfaceStartLoadProjectPatterns[] = {
    "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 33 DB E8 ?? ?? ?? ?? 84 C0 74 24",
    nullptr,
};

const char* const kVideoEditorProjectStartLoadPatterns[] = {
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F2 48 8B F9 33 DB E8 ?? ?? ?? ?? 48 85",
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
    case GameFunctionPatternId::ReplayLoadMontage:
        return kReplayLoadMontagePatterns;
    case GameFunctionPatternId::ReplayFileManagerStartEnumerateClipFiles:
        return kReplayFileManagerStartEnumerateClipFilesPatterns;
    case GameFunctionPatternId::ReplayFileManagerCheckEnumerateClipFiles:
        return kReplayFileManagerCheckEnumerateClipFilesPatterns;
    case GameFunctionPatternId::ReplayClipDataInit:
        return kReplayClipDataInitPatterns;
    case GameFunctionPatternId::ReplayClipPopulate:
        return kReplayClipPopulatePatterns;
    case GameFunctionPatternId::ReplayMontageLoad:
        return kReplayMontageLoadPatterns;
    case GameFunctionPatternId::ReplayFileManagerStartEnumerateProjectFiles:
        return kReplayFileManagerStartEnumerateProjectFilesPatterns;
    case GameFunctionPatternId::ReplayFileManagerCheckEnumerateProjectFiles:
        return kReplayFileManagerCheckEnumerateProjectFilesPatterns;
    case GameFunctionPatternId::ReplayMgrInternalStartEnumerateProjectFiles:
        return kReplayMgrInternalStartEnumerateProjectFilesPatterns;
    case GameFunctionPatternId::ReplayMgrInternalCheckEnumerateProjectFiles:
        return kReplayMgrInternalCheckEnumerateProjectFilesPatterns;
    case GameFunctionPatternId::ReplayMgrInternalStartEnumerateClipFiles:
        return kReplayMgrInternalStartEnumerateClipFilesPatterns;
    case GameFunctionPatternId::ReplayMgrInternalCheckEnumerateClipFiles:
        return kReplayMgrInternalCheckEnumerateClipFilesPatterns;
    case GameFunctionPatternId::VideoEditorProjectPrepareStagingClipByName:
        return kVideoEditorProjectPrepareStagingClipByNamePatterns;
    case GameFunctionPatternId::VideoEditorProjectMoveStagingClipToProject:
        return kVideoEditorProjectMoveStagingClipToProjectPatterns;
    case GameFunctionPatternId::VideoEditorProjectSaveProject:
        return kVideoEditorProjectSaveProjectPatterns;
    case GameFunctionPatternId::VideoEditorInterfaceStartLoadProject:
        return kVideoEditorInterfaceStartLoadProjectPatterns;
    case GameFunctionPatternId::VideoEditorProjectStartLoad:
        return kVideoEditorProjectStartLoadPatterns;
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
    case GameFunctionPatternId::ReplayLoadMontage:
        return "fiDeviceReplay::LoadMontage";
    case GameFunctionPatternId::ReplayFileManagerStartEnumerateClipFiles:
        return "ReplayFileManager::StartEnumerateClipFiles";
    case GameFunctionPatternId::ReplayFileManagerCheckEnumerateClipFiles:
        return "ReplayFileManager::CheckEnumerateClipFiles";
    case GameFunctionPatternId::ReplayClipDataInit:
        return "ClipData::Init";
    case GameFunctionPatternId::ReplayClipPopulate:
        return "CClip::Populate";
    case GameFunctionPatternId::ReplayMontageLoad:
        return "CMontage::Load";
    case GameFunctionPatternId::ReplayFileManagerStartEnumerateProjectFiles:
        return "ReplayFileManager::StartEnumerateProjectFiles";
    case GameFunctionPatternId::ReplayFileManagerCheckEnumerateProjectFiles:
        return "ReplayFileManager::CheckEnumerateProjectFiles";
    case GameFunctionPatternId::ReplayMgrInternalStartEnumerateProjectFiles:
        return "CReplayMgrInternal::StartEnumerateProjectFiles";
    case GameFunctionPatternId::ReplayMgrInternalCheckEnumerateProjectFiles:
        return "CReplayMgrInternal::CheckEnumerateProjectFiles";
    case GameFunctionPatternId::ReplayMgrInternalStartEnumerateClipFiles:
        return "CReplayMgrInternal::StartEnumerateClipFiles";
    case GameFunctionPatternId::ReplayMgrInternalCheckEnumerateClipFiles:
        return "CReplayMgrInternal::CheckEnumerateClipFiles";
    case GameFunctionPatternId::VideoEditorProjectPrepareStagingClipByName:
        return "CVideoEditorProject::PrepareStagingClip(name, ownerId, transitions)";
    case GameFunctionPatternId::VideoEditorProjectMoveStagingClipToProject:
        return "CVideoEditorProject::MoveStagingClipToProject";
    case GameFunctionPatternId::VideoEditorProjectSaveProject:
        return "CVideoEditorProject::SaveProject";
    case GameFunctionPatternId::VideoEditorInterfaceStartLoadProject:
        return "CVideoEditorInterface::StartLoadProject";
    case GameFunctionPatternId::VideoEditorProjectStartLoad:
        return "CVideoEditorProject::StartLoad";
    default:
        return "UnknownPattern";
    }
}

}
