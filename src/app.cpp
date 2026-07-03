#include "shared.h"

extern "C" __declspec(dllexport) APP_INIT(AppInit)
{
    LOG_INFO(logFn, "Initializing game.");
}

extern "C" __declspec(dllexport) APP_UPDATE(AppUpdate)
{
    if (userInput.mouse.left.endedDown)
    {
        LOG_INFO(logFn, "Mouse left button pressed at (%.2f, %.2f)", userInput.mouse.x, userInput.mouse.y);
    }
    if (userInput.controllers[0].moveUp.endedDown)
    {
        LOG_INFO(logFn, "Controller 0: Move Up button pressed.");
    }
}
