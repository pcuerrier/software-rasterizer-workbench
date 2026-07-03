#include "logger.h"
#include "shared.h"

#pragma warning(push, 0)
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

#include <memory>
#include <vector>

static std::shared_ptr<spdlog::logger> s_Logger;

// ---------------------------------------------------------------------------------------------------------------------
//  Custom flag formatter: %&
//  Emits "filename:line" left-padded to SOURCE_LOC_WIDTH chars
//  so every log line's message column starts at the same offset.
//  Only the filename is shown (no directory path).
// ---------------------------------------------------------------------------------------------------------------------
static constexpr int SOURCE_LOC_WIDTH = 32;

class SourceLocFormatter : public spdlog::custom_flag_formatter
{
public:
    void format(const spdlog::details::log_msg& msg,
                const std::tm&,
                spdlog::memory_buf_t& dest) override
    {
        // Strip directory — show only the filename
        const char* full = msg.source.filename ? msg.source.filename : "";
        const char* name = full;
        for (const char* p = full; *p; ++p)
            if (*p == '/' || *p == '\\') name = p + 1;

        char buf[64];
        SDL_snprintf(buf, sizeof(buf), "%s:%d", name, msg.source.line);

        // Left-align and pad to fixed width
        int len = (int)SDL_strlen(buf);
        dest.append(buf, buf + len);
        for (int i = len; i < SOURCE_LOC_WIDTH; ++i)
            dest.push_back(' ');
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<SourceLocFormatter>();
    }
};

// ---------------------------------------------------------------------------------------------------------------------
//  Build a formatter with our custom %& flag registered.
//  Use set_formatter() on each sink instead of set_pattern().
// ---------------------------------------------------------------------------------------------------------------------
static std::unique_ptr<spdlog::pattern_formatter> MakeFormatter(const char* pattern)
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<SourceLocFormatter>('&').set_pattern(pattern);
    return formatter;
}

static spdlog::level::level_enum ToSpdlogLevel(int platformLevel)
{
    switch (platformLevel)
    {
        case PLATFORM_LOG_TRACE:    return spdlog::level::trace;
        case PLATFORM_LOG_DEBUG:    return spdlog::level::debug;
        case PLATFORM_LOG_INFO:     return spdlog::level::info;
        case PLATFORM_LOG_WARN:     return spdlog::level::warn;
        case PLATFORM_LOG_ERROR:    return spdlog::level::err;
        case PLATFORM_LOG_CRITICAL: return spdlog::level::critical;
        default:                    return spdlog::level::info;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  SDL priority -> our level enum
//  SDL3 priorities: TRACE=1, VERBOSE=2, DEBUG=3, INFO=4,
//                   WARN=5, ERROR=6, CRITICAL=7
// ---------------------------------------------------------------------------------------------------------------------
static int SDLPriorityToPlatformLevel(SDL_LogPriority priority)
{
    switch (priority)
    {
        case SDL_LOG_PRIORITY_TRACE:    return PLATFORM_LOG_TRACE;
        case SDL_LOG_PRIORITY_VERBOSE:  return PLATFORM_LOG_TRACE;
        case SDL_LOG_PRIORITY_DEBUG:    return PLATFORM_LOG_DEBUG;
        case SDL_LOG_PRIORITY_INFO:     return PLATFORM_LOG_INFO;
        case SDL_LOG_PRIORITY_WARN:     return PLATFORM_LOG_WARN;
        case SDL_LOG_PRIORITY_ERROR:    return PLATFORM_LOG_ERROR;
        case SDL_LOG_PRIORITY_CRITICAL: return PLATFORM_LOG_CRITICAL;
        case SDL_LOG_PRIORITY_INVALID:  // fallthrough
        case SDL_LOG_PRIORITY_COUNT:    // fallthrough
        default:                        return PLATFORM_LOG_INFO;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  SDL log hook — SDL doesn't provide call-site file/line, so we
//  tag the source as "SDL" to distinguish it from engine logs.
// ---------------------------------------------------------------------------------------------------------------------
static void SDLCALL SDL_LogHook(void* /*userdata*/, int /*category*/,
                                SDL_LogPriority priority, const char* message)
{
    if (!s_Logger) return;

    spdlog::source_loc loc{ "SDL", 0, "" };
    s_Logger->log(loc, ToSpdlogLevel(SDLPriorityToPlatformLevel(priority)), message);
}

void Platform_Log(int level, const char* file, int line, const char* message)
{
    if (!s_Logger) return;
    spdlog::source_loc loc{ file, line, "" };
    s_Logger->log(loc, ToSpdlogLevel(level), message);
}

PlatformLogFn Logger_Init(const char* logFilePath, bool truncateOnOpen)
{
    std::vector<spdlog::sink_ptr> sinks;

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::info);
    consoleSink->set_formatter(MakeFormatter("%^[%-8l]%$ [%H:%M:%S.%e] (%&)  %v"));
    sinks.push_back(consoleSink);

    if (logFilePath && *logFilePath)
    {
        try
        {
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                logFilePath, truncateOnOpen);
            fileSink->set_level(spdlog::level::trace);
            fileSink->set_formatter(MakeFormatter("[%-8l]  [%Y-%m-%d %H:%M:%S.%e] (%&)  %v"));
            sinks.push_back(fileSink);
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            fprintf(stderr, "[logger] Could not open log file '%s': %s\n",
                    logFilePath, ex.what());
        }
    }

    s_Logger = std::make_shared<spdlog::logger>("engine", sinks.begin(), sinks.end());
    s_Logger->set_level(spdlog::level::trace);
    s_Logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(s_Logger);
    spdlog::set_default_logger(s_Logger);

    // Redirect ALL SDL_Log calls into our logger from this point forward.
    // This captures SDL's own internal warnings/errors too.
    SDL_SetLogOutputFunction(SDL_LogHook, nullptr);

    PLATFORM_LOG_INFO("Logger initialised - file: %s", (logFilePath && *logFilePath) ? logFilePath : "none");

    return Platform_Log;
}

void Logger_Shutdown()
{
    if (s_Logger)
    {
        s_Logger->info("Logger shutting down.");
        s_Logger->flush();
    }
    spdlog::shutdown();
    s_Logger.reset();
}