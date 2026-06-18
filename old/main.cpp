#include "main.h"

#include "logger.h"
#include "platform/platform_file_io.h"
#include "platform/platform_game_code.h"
#include "platform/platform_input.h"
#include "platform/platform_audio.h"
#include "platform/platform_renderer.h"
#include "platform/platform_image.h"

#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)

int main(int argc, char* argv[])
{
    PlatformLogFn logFn = Logger_Init("crash_log.txt", /*truncate=*/true);
    // ---- SDL Init -------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    {
        PLATFORM_LOG_CRITICAL("Failed to initialize SDL: %s", SDL_GetError());
        return -1;
    }

    // ---- Memory ---------------------------------------------------------
    size_t permanentStorageSize = Megabytes(64);
    size_t transientStorageSize = Gigabytes(1);
    void*  gameMemoryBlock      = SDL_calloc(1, permanentStorageSize + transientStorageSize);
    if (!gameMemoryBlock)
    {
        PLATFORM_LOG_CRITICAL("Failed to allocate game memory!");
        return -1;
    }

    GameMemory gameMemory = {};
    InitializeArena(&gameMemory.permanentStorage, gameMemoryBlock, permanentStorageSize);
    InitializeArena(&gameMemory.transientStorage,
                    (uint8_t*)gameMemoryBlock + permanentStorageSize,
                    transientStorageSize);

    gameMemory.DEBUG_ReadEntireFile  = DEBUG_PlatformReadEntireFile;
    gameMemory.DEBUG_FreeFileMemory  = DEBUG_PlatformFreeFileMemory;
    gameMemory.DEBUG_WriteEntireFile = DEBUG_PlatformWriteEntireFile;
    gameMemory.Log                   = logFn;

    // ---- Game Code (Hot-Reload) -----------------------------------------
    const char* sourceDLL = "game.dll";
    const char* tempDLL   = "game_temp.dll";
    GameCode    gameCode  = LoadGameCode(sourceDLL, tempDLL);

    // Initialize game state before assets so hub structures sit at the base
    // of permanentStorage (assets are pushed on top of them).
    gameCode.Initialize(&gameMemory);

    // ---- Assets ---------------------------------------------------------
    GameAssets gameAssets = {};
    Platform_LoadAssets(&gameAssets, &gameMemory.permanentStorage);

    // ---- Window & Renderer ----------------------------------------------
    SDL_Window* window = SDL_CreateWindow("Hot-Reloadable Engine", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        PLATFORM_LOG_CRITICAL("Failed to create window: %s", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer     = SDL_CreateRenderer(window, NULL);
    int           renderWidth  = 0;
    int           renderHeight = 0;
    int           bufferPitch  = 0;
    void*         gamePixels   = nullptr;
    SDL_Texture*  texture      = nullptr;
    ResizeRenderBuffer(renderer, 1280, 720,
                       &texture, &gamePixels,
                       &renderWidth, &renderHeight, &bufferPitch);

    // ---- Audio ----------------------------------------------------------
    const int targetFPS    = 60;
    const int safetyFrames = 4;
    PlatformAudio audio    = Platform_InitAudio(targetFPS, safetyFrames);

    // ---- Timing ---------------------------------------------------------
    uint64_t perfCountFreq        = SDL_GetPerformanceFrequency();
    uint64_t lastCounter          = SDL_GetPerformanceCounter();
    float    targetSecondsPerFrame = 1.0f / (float)targetFPS;
    float    currentDeltaTime     = targetSecondsPerFrame;

    GameInput oldGameInput = {};
    bool      running      = true;

    // ---- Main Loop ------------------------------------------------------
    while (running)
    {
        uint64_t currentCounter = SDL_GetPerformanceCounter();
        currentDeltaTime = (float)(currentCounter - lastCounter) / (float)perfCountFreq;

        GameInput gameInput    = {};
        gameInput.deltaTime    = currentDeltaTime;
        gameInput.frameTimeMs  = currentDeltaTime * 1000.0f;

        // Carry over held button states from last frame
        constexpr int kButtonCount =
            sizeof(gameInput.controllers[0].buttons) /
            sizeof(gameInput.controllers[0].buttons[0]);
        for (int i = 0; i < kButtonCount; ++i)
        {
            gameInput.controllers[0].buttons[i].endedDown =
                oldGameInput.controllers[0].buttons[i].endedDown;
            gameInput.controllers[0].buttons[i].halfTransitionCount = 0;
        }

        // Carry over mouse position and held button states from last frame
        gameInput.mouse.x                        = oldGameInput.mouse.x;
        gameInput.mouse.y                        = oldGameInput.mouse.y;
        gameInput.mouse.wheelDelta               = 0.0f; // reset per-frame delta
        gameInput.mouse.left.endedDown           = oldGameInput.mouse.left.endedDown;
        gameInput.mouse.left.halfTransitionCount  = 0;
        gameInput.mouse.right.endedDown          = oldGameInput.mouse.right.endedDown;
        gameInput.mouse.right.halfTransitionCount = 0;
        gameInput.mouse.middle.endedDown          = oldGameInput.mouse.middle.endedDown;
        gameInput.mouse.middle.halfTransitionCount = 0;

        // -- Event Processing --
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                ResizeRenderBuffer(renderer,
                                   event.window.data1, event.window.data2,
                                   &texture, &gamePixels,
                                   &renderWidth, &renderHeight, &bufferPitch);
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                if (event.key.repeat) { break; }
                bool       isDown = (event.type == SDL_EVENT_KEY_DOWN);
                SDL_Keycode key   = event.key.key;

                GameControllerInput* ctrl = &gameInput.controllers[0];
                if      (key == SDLK_W || key == SDLK_UP)    ProcessButtonMessage(&ctrl->moveUp,        isDown);
                else if (key == SDLK_A || key == SDLK_LEFT)  ProcessButtonMessage(&ctrl->moveLeft,      isDown);
                else if (key == SDLK_S || key == SDLK_DOWN)  ProcessButtonMessage(&ctrl->moveDown,      isDown);
                else if (key == SDLK_D || key == SDLK_RIGHT) ProcessButtonMessage(&ctrl->moveRight,     isDown);
                else if (key == SDLK_RETURN)                 ProcessButtonMessage(&ctrl->confirm,       isDown);
                else if (key == SDLK_ESCAPE)                 ProcessButtonMessage(&ctrl->cancel,        isDown);
                else if (key == SDLK_Z || key == SDLK_X)     ProcessButtonMessage(&ctrl->attack,        isDown);
                else if (key == SDLK_I)                      ProcessButtonMessage(&ctrl->openInventory, isDown);
                else if (key == SDLK_TAB)                    ProcessButtonMessage(&ctrl->openMenu,      isDown);
                break;
            }

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                bool isDown = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                GameControllerInput* gctrl = &gameInput.controllers[0];
                switch (event.gbutton.button)
                {
                case SDL_GAMEPAD_BUTTON_DPAD_UP:       ProcessButtonMessage(&gctrl->moveUp,        isDown); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:     ProcessButtonMessage(&gctrl->moveDown,      isDown); break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:     ProcessButtonMessage(&gctrl->moveLeft,      isDown); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:    ProcessButtonMessage(&gctrl->moveRight,     isDown); break;
                case SDL_GAMEPAD_BUTTON_SOUTH:         ProcessButtonMessage(&gctrl->confirm,       isDown); break;
                case SDL_GAMEPAD_BUTTON_EAST:          ProcessButtonMessage(&gctrl->cancel,        isDown); break;
                case SDL_GAMEPAD_BUTTON_WEST:          ProcessButtonMessage(&gctrl->attack,        isDown); break;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: ProcessButtonMessage(&gctrl->openInventory, isDown); break;
                case SDL_GAMEPAD_BUTTON_START:         ProcessButtonMessage(&gctrl->openMenu,      isDown); break;
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
                gameInput.mouse.x = event.motion.x;
                gameInput.mouse.y = event.motion.y;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                bool isDown = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT:   ProcessButtonMessage(&gameInput.mouse.left,   isDown); break;
                case SDL_BUTTON_RIGHT:  ProcessButtonMessage(&gameInput.mouse.right,  isDown); break;
                case SDL_BUTTON_MIDDLE: ProcessButtonMessage(&gameInput.mouse.middle, isDown); break;
                }
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
                gameInput.mouse.wheelDelta += event.wheel.y;
                break;

            case SDL_EVENT_GAMEPAD_ADDED:
                SDL_OpenGamepad(event.gdevice.which);
                break;
            }
        }

        // -- Hot Reload --
        SDL_Time newWriteTime = GetFileLastWriteTime(sourceDLL);
        if (newWriteTime != gameCode.lastWriteTime)
        {
            SDL_Delay(100); // Let the compiler release file locks
            PLATFORM_LOG_INFO("Reloading Game Code!");
            UnloadGameCode(&gameCode);
            gameCode = LoadGameCode(sourceDLL, tempDLL);
        }

        // -- Audio Prep --
        GameAudioBuffer audioBuffer = Platform_PrepareAudioBuffer(&audio);
        LOG_TRACE(&gameMemory, "[AUDIO PRE] bytesQueued: %d | sampleCount: %d",
                  SDL_GetAudioStreamQueued(audio.stream), audioBuffer.sampleCount);

        // -- Game Update --
        GameOffscreenBuffer backbuffer = {};
        backbuffer.memory = gamePixels;
        backbuffer.width  = renderWidth;
        backbuffer.height = renderHeight;
        backbuffer.pitch  = bufferPitch;

        gameCode.UpdateAndRender(&gameMemory, &gameInput, &backbuffer, &audioBuffer, &gameAssets);

        // -- Audio Submit --
        Platform_SubmitAudioBuffer(&audio, &audioBuffer);

        // -- Blit to Screen --
        void* texPixels;
        int   texPitch;
        if (SDL_LockTexture(texture, NULL, &texPixels, &texPitch))
        {
            uint8_t* src = (uint8_t*)gamePixels;
            uint8_t* dst = (uint8_t*)texPixels;
            for (int y = 0; y < renderHeight; ++y)
            {
                SDL_memcpy(dst, src, (size_t)(renderWidth * 4));
                src += bufferPitch;
                dst += texPitch;
            }
            SDL_UnlockTexture(texture);
        }
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        oldGameInput = gameInput;

        // -- Frame Limiter --
        // Measure from currentCounter (this frame's start), not lastCounter (previous frame's start).
        // Using lastCounter here would cause the spinlock to exit immediately every other frame
        // because the previous frame's work already consumed the full target duration.
        float workSeconds = (float)(SDL_GetPerformanceCounter() - currentCounter) / (float)perfCountFreq;
        float sleepMs     = (targetSecondsPerFrame - workSeconds) * 1000.0f;
        if (sleepMs > 2.0f)
        {
            SDL_Delay((uint32_t)(sleepMs - 2.0f));
        }
        // Spinlock to nail the exact frame boundary
        float secondsElapsed = workSeconds;
        while (secondsElapsed < targetSecondsPerFrame)
        {
            secondsElapsed = (float)(SDL_GetPerformanceCounter() - currentCounter) / (float)perfCountFreq;
        }

        LOG_TRACE(&gameMemory, "Frame Time: %.2f ms | FPS: %.2f",
                  secondsElapsed * 1000.0f, 1.0f / secondsElapsed);

        lastCounter = currentCounter;
    }

    return 0;
}

// Unity build — each .cpp is a single translation unit pulled in here
#include "platform/platform_file_io.cpp"
#include "platform/platform_game_code.cpp"
#include "platform/platform_input.cpp"
#include "platform/platform_audio.cpp"
#include "platform/platform_renderer.cpp"
#include "platform/platform_image.cpp"
#include "logger.cpp"