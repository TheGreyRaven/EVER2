#pragma once

#include <cstdint>
#include <string>

namespace ever::features::rockstar_editor_menu {

void EnsureHooksInstalled();

bool HasProjectContext();

bool AddClipToCurrentProjectByName(
    const std::string& clip_name,
    uint64_t owner_id,
    int destination_index,
    std::wstring& out_error,
    int& out_new_clip_count);

bool StartLoadProjectByPath(const std::string& project_path, std::wstring& out_error);

bool SaveCurrentProject(std::wstring& out_error);

}
