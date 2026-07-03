#include "main.h"

#include "logger.h"
#include "platform/app_code.h"
#include "platform/platform_input.h"
#include "platform/renderer.h"

int main(int argc, char* argv[])
{
    // ---- Constants ------------------------------------------------------
    const int TARGET_FPS              = 60;
    constexpr float TARGET_FRAME_TIME = 1.0f / (float)TARGET_FPS;
    const float SLEEP_THRESHOLD_MS    = 2.0f;

    constexpr int CONTROLLER_BUTTON_COUNT = sizeof(ControllerInput) /
                                            sizeof(ControllerButtonState);

    PlatformLogFn logFn = Logger_Init("crash_log.txt", /*truncate=*/true);
    // ---- SDL Init -------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        PLATFORM_LOG_CRITICAL("Failed to initialize SDL: %s", SDL_GetError());
        return -1;
    }

    // ---- Memory ---------------------------------------------------------
    size_t permanentStorageSize = Megabytes(64);
    size_t transientStorageSize = Gigabytes(1);
    void*  appMemoryBlock      = SDL_calloc(1, permanentStorageSize + transientStorageSize);
    if (!appMemoryBlock)
    {
        PLATFORM_LOG_CRITICAL("Failed to allocate application memory!");
        return -1;
    }

    AppMemory appMemory = {};
    InitializeArena(&appMemory.permanentStorage, appMemoryBlock, permanentStorageSize);
    InitializeArena(&appMemory.transientStorage,
                    (uint8_t*)appMemoryBlock + permanentStorageSize,
                    transientStorageSize);

    // ---- Game Code (Hot-Reload) -----------------------------------------
    const char* sourceDLL = "app.dll";
    const char* tempDLL   = "app_temp.dll";
    AppCode     app       = LoadAppCode(sourceDLL, tempDLL);

    // Initialize application state before assets so hub structures sit at the
    // base of permanentStorage (assets are pushed on top of them).
    app.Init(logFn, appMemory);

    // ---- Window & Renderer ----------------------------------------------
    SDL_Window* window = SDL_CreateWindow("JRPG - Workbench", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        PLATFORM_LOG_CRITICAL("Failed to create window: %s", SDL_GetError());
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer)
    {
        PLATFORM_LOG_CRITICAL("Failed to create renderer: %s", SDL_GetError());
        return -1;
    }

    int           renderWidth  = 0;
    int           renderHeight = 0;
    int           bufferPitch  = 0;
    void*         appPixels   = nullptr;
    SDL_Texture*  texture      = nullptr;
    ResizeRenderBuffer(renderer, 1280, 720,
                       &texture, &appPixels,
                       &renderWidth, &renderHeight, &bufferPitch);

    // ---- Timing ---------------------------------------------------------
    uint64_t perfCountFreq        = SDL_GetPerformanceFrequency();
    uint64_t lastCounter          = SDL_GetPerformanceCounter();
    float    currentDeltaTime     = TARGET_FRAME_TIME;

    // ---- Main Loop ------------------------------------------------------
    bool running = true;
    UserInput oldUserInput = {};

    while (running)
    {
        uint64_t currentCounter = SDL_GetPerformanceCounter();
        currentDeltaTime = (float)(currentCounter - lastCounter) / (float)perfCountFreq;

        UserInput userInput    = {};
        // Carry over held button states from last frame
        for (int i = 0; i < CONTROLLER_BUTTON_COUNT; ++i)
        {
            userInput.controllers[0].buttons[i].endedDown =
                oldUserInput.controllers[0].buttons[i].endedDown;
            userInput.controllers[0].buttons[i].transitionCount = 0;
        }

        // Carry over mouse position and held button states from last frame
        userInput.mouse                        = oldUserInput.mouse;
        userInput.mouse.wheelDelta             = 0.0f; // reset per-frame delta
        userInput.mouse.left.transitionCount   = 0;
        userInput.mouse.right.transitionCount  = 0;
        userInput.mouse.middle.transitionCount = 0;

        // ---- Event Processing -------------------------------------------
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                {
                    running = false;
                } break;

                case SDL_EVENT_WINDOW_RESIZED:
                {
                    ResizeRenderBuffer(renderer,
                        event.window.data1, event.window.data2,
                        &texture, &appPixels,
                        &renderWidth, &renderHeight, &bufferPitch);
                } break;
                
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                {
                    if (event.key.repeat) { break; }
                    bool       isDown = (event.type == SDL_EVENT_KEY_DOWN);
                    SDL_Keycode key   = event.key.key;

                    ControllerInput* ctrl = &userInput.controllers[0];
                    if      (key == SDLK_W || key == SDLK_UP)    ProcessButtonMessage(&ctrl->moveUp,        isDown);
                    else if (key == SDLK_A || key == SDLK_LEFT)  ProcessButtonMessage(&ctrl->moveLeft,      isDown);
                    else if (key == SDLK_S || key == SDLK_DOWN)  ProcessButtonMessage(&ctrl->moveDown,      isDown);
                    else if (key == SDLK_D || key == SDLK_RIGHT) ProcessButtonMessage(&ctrl->moveRight,     isDown);
                    else if (key == SDLK_RETURN)                 ProcessButtonMessage(&ctrl->confirm,       isDown);
                    else if (key == SDLK_ESCAPE)                 ProcessButtonMessage(&ctrl->cancel,        isDown);
                    else if (key == SDLK_Z || key == SDLK_X)     ProcessButtonMessage(&ctrl->attack,        isDown);
                    else if (key == SDLK_I)                      ProcessButtonMessage(&ctrl->openInventory, isDown);
                    else if (key == SDLK_TAB)                    ProcessButtonMessage(&ctrl->openMenu,      isDown);
                } break;

                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                {
                    bool isDown = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                    ControllerInput* gctrl = &userInput.controllers[0];
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
                } break;

                case SDL_EVENT_MOUSE_MOTION:
                {
                    userInput.mouse.x = event.motion.x;
                    userInput.mouse.y = event.motion.y;
                } break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                {
                    bool isDown = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    switch (event.button.button)
                    {
                    case SDL_BUTTON_LEFT:   ProcessButtonMessage(&userInput.mouse.left,   isDown); break;
                    case SDL_BUTTON_RIGHT:  ProcessButtonMessage(&userInput.mouse.right,  isDown); break;
                    case SDL_BUTTON_MIDDLE: ProcessButtonMessage(&userInput.mouse.middle, isDown); break;
                    }
                } break;

                case SDL_EVENT_MOUSE_WHEEL:
                {
                    userInput.mouse.wheelDelta += event.wheel.y;
                } break;

                case SDL_EVENT_GAMEPAD_ADDED:
                {
                    SDL_OpenGamepad(event.gdevice.which);
                } break;
            }
        }

        // ---- Hot-Reload -------------------------------------------------
#ifdef DEBUG
        SDL_Time newWriteTime = GetFileLastWriteTime(sourceDLL);
        if (newWriteTime != app.lastWriteTime)
        {
            // TODO: Check if delay is needed
            SDL_Delay(100); // Let the compiler release file locks
            UnloadAppCode(&app);
            app = LoadAppCode(sourceDLL, tempDLL);
        }
#endif
        // ---- Game Update ------------------------------------------------
        AppOffscreenBuffer backbuffer = {};
        backbuffer.memory = appPixels;
        backbuffer.width  = renderWidth;
        backbuffer.height = renderHeight;
        backbuffer.pitch  = bufferPitch;

        app.Update(logFn, appMemory, backbuffer, userInput);

        // Carry over the input state for the next frame
        oldUserInput = userInput;

        // ---- Render -----------------------------------------------------
        Render(renderer, texture, backbuffer);

        // ---- Frame Timing -----------------------------------------------
        // Measure from currentCounter (this frame's start), not lastCounter (previous frame's start).
        // Using lastCounter here would cause the spinlock to exit immediately every other frame
        // because the previous frame's work already consumed the full target duration.
        float workSeconds = (float)(SDL_GetPerformanceCounter() - currentCounter) / (float)perfCountFreq;
        float sleepMs     = (TARGET_FRAME_TIME - workSeconds) * 1000.0f;
        if (sleepMs > SLEEP_THRESHOLD_MS)
        {
            SDL_Delay((uint32_t)(sleepMs - SLEEP_THRESHOLD_MS));
        }
        // Spinlock to nail the exact frame boundary
        float secondsElapsed = workSeconds;
        while (secondsElapsed < TARGET_FRAME_TIME)
        {
            secondsElapsed = (float)(SDL_GetPerformanceCounter() - currentCounter) / (float)perfCountFreq;
        }

        PLATFORM_LOG_TRACE("Frame Time: %.2f ms | FPS: %.2f", secondsElapsed * 1000.0f, 1.0f / secondsElapsed);
        lastCounter = currentCounter;
    }

    // TODO: None of this cleanup is actually necessary since the OS will reclaim all resources on process exit
    // Check if we have large cleanup time and if so, consider removing this code to speed up shutdown.
    UnloadAppCode(&app);
    SDL_free(appPixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#include "logger.cpp"
#include "platform/app_code.cpp"
#include "platform/platform_input.cpp"
#include "platform/renderer.cpp"
