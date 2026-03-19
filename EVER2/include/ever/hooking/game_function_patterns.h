#pragma once

namespace ever::hooking {

enum class GameFunctionPatternId {
    StartShutdownTasks,
    VideoEditorClose,
    ReplayEnumerateProjects,
    ReplayLoadMontage,
    ReplayFileManagerStartEnumerateProjectFiles,
    ReplayFileManagerCheckEnumerateProjectFiles,
    ReplayMgrInternalStartEnumerateProjectFiles,
    ReplayMgrInternalCheckEnumerateProjectFiles,
    VideoEditorProjectPrepareStagingClipByIndex,
    VideoEditorProjectMoveStagingClipToProject,
    VideoEditorProjectSaveProject,
};

// Returns a null-terminated array of candidate pattern strings for a target id
// The caller iterates until a null entry
// Might not be the best solution
const char* const* GetGameFunctionPatternCandidates(GameFunctionPatternId id);

const char* GetGameFunctionPatternName(GameFunctionPatternId id);

}
