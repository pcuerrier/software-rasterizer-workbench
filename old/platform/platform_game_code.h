#pragma once

#include "shared.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <SDL3/SDL_loadso.h>
#pragma warning(pop)

// Safe no-ops to call if the DLL fails to load or export a symbol
GAME_INIT(GameInitializeStub)
{
    // Do nothing
}

GAME_RUN_FRAME(GameUpdateAndRenderStub)
{
    // Do nothing
}

// ============================================================
//  Hot-Reloadable Game Code
//  Owns the DLL handle and function pointers. The OS layer
//  checks the source DLL's write-time each frame and rebuilds
//  this struct whenever a new compile drops.
// ============================================================

struct GameCode
{
    SDL_SharedObject* gameLibrary;
    GameInitFn*       Initialize;     // Called once at startup; NOT called again on hot-reload
    GameRunFrame*     UpdateAndRender;
    SDL_Time          lastWriteTime;
    bool              isValid;
};

SDL_Time GetFileLastWriteTime(const char* filename);
bool     CopyFileSDL(const char* src, const char* dst);
void     UnloadGameCode(GameCode* code);
GameCode LoadGameCode(const char* sourceDLL, const char* tempDLL);