#pragma once

#include <string>

namespace ever::features::editor_projects {

// Installs project-level hooks used to capture an active CVideoEditorProject pointer.
void EnsureHooksInstalled();

// Returns true when an active project pointer has been observed by a hooked call.
bool HasProjectContext();

// Adds a clip from source_index into destination_index inside the active project.
// This follows native PrepareStagingClip(sourceIndex) -> MoveStagingClipToProject(destIndex).
bool AddClipToCurrentProject(int source_index, int destination_index, std::wstring& out_error);

// Saves the active project through native CVideoEditorProject::SaveProject.
bool SaveCurrentProject(std::wstring& out_error);

}
