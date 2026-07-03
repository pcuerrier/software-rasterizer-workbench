#include "app_code.h"

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

    //PLATFORM_LOG_ERROR("Failed to copy %s to %s: %s", src, dst, SDL_GetError());
    return false;
}

void UnloadAppCode(AppCode* code)
{
    if (code->appLibrary)
    {
        SDL_UnloadObject(code->appLibrary);
        code->appLibrary = nullptr;
    }
    code->isValid = false;
    code->Init    = AppInit_Stub;
    code->Update  = AppUpdate_Stub;
}

AppCode LoadAppCode(const char* sourceDLL, const char* tempDLL)
{
    AppCode result      = {};
    result.lastWriteTime = GetFileLastWriteTime(sourceDLL);

    if (CopyFileSDL(sourceDLL, tempDLL))
    {
        result.appLibrary = SDL_LoadObject(tempDLL);
        if (result.appLibrary)
        {
            result.Init = (AppInitFn*)SDL_LoadFunction(result.appLibrary, "AppInit");
            result.Update = (AppUpdateFn*)SDL_LoadFunction(result.appLibrary, "AppUpdate");
            result.isValid = (result.Init != nullptr) && (result.Update != nullptr);
        }
    }

    if (!result.isValid)
    {
        result.Init   = AppInit_Stub;
        result.Update = AppUpdate_Stub;
    }
    return result;
}