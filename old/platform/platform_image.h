#pragma once

#include "shared.h"

// Loads all game assets into the provided arena.
// Call once before the game loop. File paths are internal to this module.
void Platform_LoadAssets(GameAssets* assets, MemoryArena* arena);
