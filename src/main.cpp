#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_loadso.h>
#pragma warning(pop)

#include "platform.h"

static void AppUpdate_Stub(AppOffscreenBuffer*) {}

struct AppCode
{
    SDL_SharedObject*     library;
    AppUpdateFn*          Update;
    SDL_Time              lastWriteTime;
    bool                  isValid;
};

static SDL_Time GetFileWriteTime(const char* path)
{
    SDL_PathInfo info = {};
    SDL_GetPathInfo(path, &info);
    return info.modify_time;
}

static AppCode LoadApp(const char* sourceDLL, const char* tempDLL)
{
    AppCode app      = {};
    app.Update = AppUpdate_Stub;

    CopyFile(sourceDLL, tempDLL, FALSE);
    app.library = SDL_LoadObject(tempDLL);
    if (app.library)
    {
        app.Update = (AppUpdateFn*)SDL_LoadFunction(app.library, "AppUpdate");
        app.isValid         = (app.Update != nullptr);
    }
    if (!app.isValid)
        app.Update = AppUpdate_Stub;

    app.lastWriteTime = GetFileWriteTime(sourceDLL);
    return app;
}

static void UnloadApp(AppCode* app)
{
    if (app->library)
        SDL_UnloadObject(app->library);
    *app                = {};
    app->Update = AppUpdate_Stub;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Software Rasterizer Workbench", 1280, 720,
                                          SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!window || !renderer)
    {
        SDL_Log("Failed to create window/renderer: %s", SDL_GetError());
        return -1;
    }

    int   renderWidth  = 1280;
    int   renderHeight = 720;
    int   pitch        = renderWidth * 4;
    void* pixels       = SDL_malloc((size_t)(pitch * renderHeight));

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             renderWidth, renderHeight);

    const char* sourceDLL = "app.dll";
    const char* tempDLL   = "app_temp.dll";
    AppCode     app       = LoadApp(sourceDLL, tempDLL);

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                renderWidth  = event.window.data1;
                renderHeight = event.window.data2;
                pitch        = renderWidth * 4;
                SDL_free(pixels);
                pixels = SDL_malloc((size_t)(pitch * renderHeight));
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer,
                                            SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            renderWidth, renderHeight);
                break;
            }
        }

        // Hot reload
        SDL_Time newWriteTime = GetFileWriteTime(sourceDLL);
        if (newWriteTime != app.lastWriteTime)
        {
            SDL_Delay(100);
            UnloadApp(&app);
            app = LoadApp(sourceDLL, tempDLL);
        }

        AppOffscreenBuffer buffer = {};
        buffer.memory = pixels;
        buffer.width  = renderWidth;
        buffer.height = renderHeight;
        buffer.pitch  = pitch;

        app.Update(&buffer);

        void* texPixels = nullptr;
        int   texPitch  = 0;
        if (SDL_LockTexture(texture, NULL, &texPixels, &texPitch))
        {
            uint8_t* src = (uint8_t*)pixels;
            uint8_t* dst = (uint8_t*)texPixels;
            for (int y = 0; y < renderHeight; ++y)
            {
                SDL_memcpy(dst, src, (size_t)(renderWidth * 4));
                src += pitch;
                dst += texPitch;
            }
            SDL_UnlockTexture(texture);
        }
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    UnloadApp(&app);
    SDL_free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
