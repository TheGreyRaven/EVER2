#pragma once

#include "ever/hooking/game_function_patterns.h"
#include "ever/hooking/pattern_scanner.h"
#include "ever/hooking/x64_detour.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace ever::features::rockstar_editor_menu {

using PrepareStagingClipByNameFn = bool(__fastcall*)(
    void* this_ptr,
    const char* clip_name,
    uint64_t owner_id,
    uint32_t start_transition,
    uint32_t end_transition);
using MoveStagingClipToProjectFn = bool(__fastcall*)(void* this_ptr, int destination_index);
using SaveProjectFn = int(__fastcall*)(void* this_ptr, const char* filename, bool show_spinner_with_text);
using StartLoadProjectFn = bool(__fastcall*)(const char* path);
using ProjectStartLoadFn = bool(__fastcall*)(void* this_ptr, const char* path);
using MontageLoadFn = bool(__fastcall*)(void* this_ptr, const char* filename, uint64_t arg3, uint64_t arg4);

extern std::shared_ptr<PLH::x64Detour> g_project_start_load_detour;
extern std::shared_ptr<PLH::x64Detour> g_montage_load_detour;

extern PrepareStagingClipByNameFn g_prepare_clip_by_name;
extern MoveStagingClipToProjectFn g_move_clip_original;
extern SaveProjectFn g_save_project_original;
extern StartLoadProjectFn g_start_load_project;
extern ProjectStartLoadFn g_project_start_load_original;
extern MontageLoadFn g_montage_load_original;

extern std::atomic<void*> g_last_project_ptr;
extern std::atomic<bool> g_montage_load_complete;
extern std::atomic<uint64_t> g_hook_hits;
extern std::atomic<uint32_t> g_install_attempts;
extern std::atomic<ULONGLONG> g_last_install_attempt_tick;
extern std::mutex g_install_mutex;
extern std::mutex g_project_path_mutex;
extern std::string g_last_project_path;

uint64_t ResolveFunctionStartFromUnwind(uint64_t hit_address, uint64_t module_base, uint64_t module_size);

uint64_t ResolvePatternToFunctionStart(
    ever::hooking::PatternScanner& scanner,
    ever::hooking::GameFunctionPatternId pattern_id,
    const wchar_t* log_prefix);

void CaptureProjectContext(void* this_ptr, const wchar_t* hook_name);

}
