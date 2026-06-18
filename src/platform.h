#pragma once

#pragma warning(disable : 4100) // unreferenced formal parameter
#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union
#pragma warning(disable : 4820) // struct padded due to alignment
#pragma warning(disable : 5045) // Spectre mitigation warning

#include <stdint.h>
#include <stddef.h>

struct AppOffscreenBuffer
{
    void*   memory;
    int32_t width;
    int32_t height;
    int32_t pitch; // bytes per row
};

#define APP_UPDATE(name) void name(AppOffscreenBuffer* buffer)
typedef APP_UPDATE(AppUpdateFn);
