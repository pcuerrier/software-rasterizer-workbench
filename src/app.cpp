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
    PushRenderCmdClear(render_cmds, 0xFF115588); // Clear to a greenish color
    PushRenderCmdRect(render_cmds, 100, 100, 200, 150, 0xFFAA3300); // Draw a red rectangle
}
