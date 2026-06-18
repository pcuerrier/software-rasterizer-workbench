#include "platform_input.h"

void ProcessButtonMessage(GameButtonState* newState, bool isDown)
{
    if (newState->endedDown != isDown)
    {
        newState->endedDown = isDown;
        newState->halfTransitionCount++;
    }
}