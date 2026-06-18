#pragma once

#include "shared.h"

// ============================================================
//  Platform File I/O
//  DEBUG-only helpers that fulfil the function-pointer contract
//  defined in shared.h. Wired into GameMemory on startup.
// ============================================================

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUG_PlatformReadEntireFile);
DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUG_PlatformFreeFileMemory);
DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUG_PlatformWriteEntireFile);