#include "platform_input.h"

void ProcessButtonMessage(ControllerButtonState* newState, bool isDown)
{
    if (newState->endedDown != isDown)
    {
        newState->endedDown = isDown;
        newState->transitionCount++;
    }
}