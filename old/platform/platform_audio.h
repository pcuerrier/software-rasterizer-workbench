#pragma once

#include "shared.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#pragma warning(pop)

// ============================================================
//  Platform Audio
//  Owns the SDL audio device, stream, and sample scratch buffer.
//  Platform_PrepareAudioBuffer computes how many samples are
//  needed to top up the queue; Platform_SubmitAudioBuffer pushes
//  the filled samples and starts the device on the first call.
// ============================================================

struct PlatformAudio
{
    SDL_AudioSpec     spec;
    SDL_AudioDeviceID device;
    SDL_AudioStream*  stream;
    int16_t*          samples;       // Scratch buffer for one frame's worth of audio
    int               samplesPerFrame;
    int               safetyFrames;
    bool              started;       // Becomes true after the first real submit
};

// Initialise the audio device and allocate the scratch buffer.
// targetFPS drives samplesPerFrame; safetyFrames controls queue depth.
PlatformAudio Platform_InitAudio(int targetFPS, int safetyFrames);

// Calculate how many new samples are needed this frame and populate
// a GameAudioBuffer ready to hand to the game DLL.
GameAudioBuffer Platform_PrepareAudioBuffer(PlatformAudio* audio);

// Push the filled buffer to the SDL audio stream.
// Starts the device on the first non-zero submission.
void Platform_SubmitAudioBuffer(PlatformAudio* audio, GameAudioBuffer* buffer);