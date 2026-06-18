#include "platform_audio.h"

PlatformAudio Platform_InitAudio(int targetFPS, int safetyFrames)
{
    PlatformAudio audio = {};
    audio.safetyFrames  = safetyFrames;

    audio.spec.freq     = 48000;
    audio.spec.channels = 2;
    audio.spec.format   = SDL_AUDIO_S16;

    audio.device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio.spec);
    audio.stream = SDL_CreateAudioStream(&audio.spec, &audio.spec);
    SDL_BindAudioStream(audio.device, audio.stream);

    audio.samplesPerFrame = audio.spec.freq / targetFPS;

    int maxSamples  = audio.samplesPerFrame * safetyFrames;
    audio.samples   = (int16_t*)SDL_malloc(
        (size_t)(maxSamples * audio.spec.channels) * sizeof(int16_t)
    );

    // Pre-fill with silence so there is no underrun on the very first frame
    int     silenceSamples = audio.samplesPerFrame * safetyFrames;
    int16_t* silence       = (int16_t*)SDL_calloc(
        (size_t)(silenceSamples * audio.spec.channels), sizeof(int16_t)
    );
    SDL_PutAudioStreamData(audio.stream, silence,
        silenceSamples * audio.spec.channels * (int)sizeof(int16_t));
    SDL_free(silence);

    return audio;
}

GameAudioBuffer Platform_PrepareAudioBuffer(PlatformAudio* audio)
{
    int bytesPerSample  = audio->spec.channels * (int)sizeof(int16_t);
    int targetBytes     = audio->samplesPerFrame * audio->safetyFrames * bytesPerSample;
    int bytesQueued     = SDL_GetAudioStreamQueued(audio->stream);
    int bytesToWrite    = targetBytes - bytesQueued;

    GameAudioBuffer buffer  = {};
    buffer.samplesPerSecond = audio->spec.freq;
    buffer.samples          = audio->samples;
    buffer.sampleCount      = (bytesToWrite > 0) ? (bytesToWrite / bytesPerSample) : 0;
    return buffer;
}

void Platform_SubmitAudioBuffer(PlatformAudio* audio, GameAudioBuffer* buffer)
{
    if (buffer->sampleCount <= 0) { return; }

    SDL_PutAudioStreamData(
        audio->stream,
        audio->samples,
        buffer->sampleCount * audio->spec.channels * (int)sizeof(int16_t)
    );

    if (!audio->started)
    {
        SDL_ResumeAudioDevice(audio->device);
        audio->started = true;
    }
}