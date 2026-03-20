#pragma once

#include <cstdint>
#include <string>

namespace ever::features::rockstar_editor_menu {

void EnsureHooksInstalled();

bool HasProjectContext();

bool AddClipToCurrentProject(int source_index, int destination_index, std::wstring& out_error);

bool AddClipToCurrentProjectByName(
    const std::string& clip_name,
    uint64_t owner_id,
    int destination_index,
    std::wstring& out_error);

bool StartLoadProjectByPath(const std::string& project_path, std::wstring& out_error);

bool SaveCurrentProject(std::wstring& out_error);

}
