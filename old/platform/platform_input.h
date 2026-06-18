#pragma once

#include "shared.h"

// ============================================================
//  Platform Input
//  Translates raw OS key/button events into our GameButtonState
//  transition model (endedDown + halfTransitionCount).
// ============================================================

// Modifies newState in-place: updates endedDown and increments halfTransitionCount.
void ProcessButtonMessage(GameButtonState* newState, bool isDown);