#include "platform_game_code.h"

SDL_Time GetFileLastWriteTime(const char* filename)
{
    SDL_PathInfo info;
    if (SDL_GetPathInfo(filename, &info))
    {
        return info.modify_time;
    }
    return 0;
}

bool CopyFileSDL(const char* src, const char* dst)
{
    const int maxRetries  = 10;
    const int retryDelay  = 50; // ms

    for (int attempt = 0; attempt < maxRetries; ++attempt)
    {
        size_t size;
        void*  data = SDL_LoadFile(src, &size);
        if (data)
        {
            SDL_IOStream* out = SDL_IOFromFile(dst, "wb");
            if (out)
            {
                SDL_WriteIO(out, data, size);
                SDL_CloseIO(out);
                SDL_free(data);
                return true;
            }
            SDL_free(data);
        }
        SDL_Delay(retryDelay);
    }

    PLATFORM_LOG_ERROR("Failed to copy %s to %s: %s", src, dst, SDL_GetError());
    return false;
}

void UnloadGameCode(GameCode* code)
{
    if (code->gameLibrary)
    {
        SDL_UnloadObject(code->gameLibrary);
        code->gameLibrary = nullptr;
    }
    code->isValid         = false;
    code->Initialize      = GameInitializeStub;
    code->UpdateAndRender = GameUpdateAndRenderStub;
}

GameCode LoadGameCode(const char* sourceDLL, const char* tempDLL)
{
    GameCode result      = {};
    result.lastWriteTime = GetFileLastWriteTime(sourceDLL);

    if (CopyFileSDL(sourceDLL, tempDLL))
    {
        result.gameLibrary = SDL_LoadObject(tempDLL);
        if (result.gameLibrary)
        {
            result.Initialize = (GameInitFn*)SDL_LoadFunction(
                result.gameLibrary, "GameInitialize"
            );

            result.UpdateAndRender = (GameRunFrame*)SDL_LoadFunction(
                result.gameLibrary, "GameUpdateAndRender"
            );
            result.isValid = (result.Initialize != nullptr) && (result.UpdateAndRender != nullptr);
        }
    }

    if (!result.isValid)
    {
        result.Initialize     = GameInitializeStub;
        result.UpdateAndRender = GameUpdateAndRenderStub;
    }
    return result;
}