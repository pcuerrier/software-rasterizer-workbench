#include "shared.h"

extern "C" __declspec(dllexport) APP_INIT(AppInit)
{
    LOG_INFO(logFn, "Initializing game.");
}

extern "C" __declspec(dllexport) APP_UPDATE(AppUpdate)
{
    LOG_INFO(logFn, "Updating game state.");
}
