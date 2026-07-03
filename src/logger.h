#pragma once
// ================================================================
//  logger.h  —  OS-layer logger API
//
//  Include this ONLY in OS-layer translation units (main.cpp, etc.).
//  The game DLL never sees this header — it communicates through the
//  PlatformLogFn pointer stored in GameMemory.
// ================================================================

#include "shared.h"
#include <stdio.h>

PlatformLogFn Logger_Init(const char* logFilePath, bool truncateOnOpen = true);
void          Logger_Shutdown();

// Forward declaration so the macros below can call it.
// Defined in logger.cpp (included via unity build).
void Platform_Log(int level, const char* file, int line, const char* message);

// ----------------------------------------------------------------
//  OS-layer logging macros — no GameMemory* required.
//  Use these anywhere in platform code instead of SDL_Log().
//  The message is formatted into a stack buffer before dispatch
//  so Platform_Log's signature stays a plain const char*.
// ----------------------------------------------------------------
#define PLATFORM_LOG(level, fmt, ...)                                   \
    do {                                                                \
        char _plBuf[1024];                                              \
        SDL_snprintf(_plBuf, sizeof(_plBuf), fmt, ##__VA_ARGS__);       \
        Platform_Log(level, __FILE__, __LINE__, _plBuf);                \
    } while(0)

#define PLATFORM_LOG_TRACE(fmt, ...)    PLATFORM_LOG(PLATFORM_LOG_TRACE,    fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_DEBUG(fmt, ...)    PLATFORM_LOG(PLATFORM_LOG_DEBUG,    fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_INFO(fmt, ...)     PLATFORM_LOG(PLATFORM_LOG_INFO,     fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_WARN(fmt, ...)     PLATFORM_LOG(PLATFORM_LOG_WARN,     fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_ERROR(fmt, ...)    PLATFORM_LOG(PLATFORM_LOG_ERROR,    fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_CRITICAL(fmt, ...) PLATFORM_LOG(PLATFORM_LOG_CRITICAL, fmt, ##__VA_ARGS__)