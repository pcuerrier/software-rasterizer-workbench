#pragma once

// Hardware breakpoint — compiler-specific implementation.
#if defined(_MSC_VER)
    #define DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define DEBUG_BREAK() __builtin_trap()
#else
    // Fallback: write to address 0; the OS catches the access violation.
    #define DEBUG_BREAK() *(volatile int *)0 = 0
#endif

#if ENGINE_DEBUG
    // do { ... } while(0) ensures safe use inside if/else without braces.
    #define ASSERT(Expression) \
        do { \
            if (!(Expression)) { \
                DEBUG_BREAK(); \
            } \
        } while(0)
#else
    #define ASSERT(Expression)
#endif
