#include "platform_file_io.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#pragma warning(pop)

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUG_PlatformReadEntireFile)
{
    DebugReadFileResult result = {};
    size_t size;

    void* data = SDL_LoadFile(filename, &size);
    if (data)
    {
        result.contents     = data;
        result.contentsSize = size;
    }
    return result;
}

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUG_PlatformFreeFileMemory)
{
    if (memory)
    {
        SDL_free(memory);
    }
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUG_PlatformWriteEntireFile)
{
    bool result = false;

    SDL_IOStream* file = SDL_IOFromFile(filename, "wb");
    if (file)
    {
        size_t bytesWritten = SDL_WriteIO(file, memory, memorySize);
        if (bytesWritten == memorySize)
        {
            result = true;
        }
        SDL_CloseIO(file);
    }
    return result;
}