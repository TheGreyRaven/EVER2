#pragma once

namespace ever::hooking {

enum class GameFunctionPatternId {
    StartShutdownTasks,
    VideoEditorClose,
    ReplayEnumerateProjects,
    ReplayLoadMontage,
    ReplayFileManagerStartEnumerateClipFiles,
    ReplayFileManagerCheckEnumerateClipFiles,
    ReplayClipDataInit,
    ReplayClipPopulate,
    ReplayMontageLoad,
    ReplayFileManagerStartEnumerateProjectFiles,
    ReplayFileManagerCheckEnumerateProjectFiles,
    ReplayMgrInternalStartEnumerateProjectFiles,
    ReplayMgrInternalCheckEnumerateProjectFiles,
    ReplayMgrInternalStartEnumerateClipFiles,
    ReplayMgrInternalCheckEnumerateClipFiles,
    VideoEditorProjectPrepareStagingClipByIndex,
    VideoEditorProjectPrepareStagingClipByName,
    VideoEditorProjectMoveStagingClipToProject,
    VideoEditorProjectSaveProject,
    VideoEditorInterfaceStartLoadProject,
    VideoEditorProjectStartLoad,
};

// Returns a null-terminated array of candidate pattern strings for a target id
// The caller iterates until a null entry
// Might not be the best solution
const char* const* GetGameFunctionPatternCandidates(GameFunctionPatternId id);

const char* GetGameFunctionPatternName(GameFunctionPatternId id);

}
