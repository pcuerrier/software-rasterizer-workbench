#pragma once

#include "shared.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <SDL3/SDL_loadso.h>
#pragma warning(pop)

// Safe no-ops to call if the DLL fails to load or export a symbol
APP_INIT(AppInit_Stub)
{
    // Do nothing
}

APP_UPDATE(AppUpdate_Stub)
{
    // Do nothing
}

struct AppCode
{
    SDL_SharedObject* appLibrary;
    AppInitFn*        Init;     // Called once at startup; NOT called again on hot-reload
    AppUpdateFn*      Update;
    SDL_Time          lastWriteTime;
    bool              isValid;
};

SDL_Time GetFileLastWriteTime(const char* filename);
bool     CopyFileSDL(const char* src, const char* dst);
void     UnloadAppCode(AppCode* code);
AppCode  LoadAppCode(const char* sourceDLL, const char* tempDLL);