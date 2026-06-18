#pragma once

#include "debug.h"
#include <stdint.h>
#include <stddef.h>

// A basic bump allocator.
struct MemoryArena
{
    uint8_t* base;
    size_t   size;
    size_t   used;
};

// Initializes the arena with the raw memory block provided by the OS.
inline void InitializeArena(MemoryArena* arena, void* backingMemory, size_t size)
{
    arena->base = (uint8_t*)backingMemory;
    arena->size = size;
    arena->used = 0;
}

// Clears the arena (perfect for transient/per-frame memory).
inline void ClearArena(MemoryArena* arena)
{
    arena->used = 0;
}

inline void* PushSize(MemoryArena* arena, size_t size, size_t alignment = 8)
{
    // 1. Figure out the current memory address
    size_t currentPtr = (size_t)(arena->base + arena->used);

    // 2. Calculate how many bytes we are off from the target alignment
    size_t alignmentOffset = 0;
    size_t alignmentMask   = alignment - 1;
    if (currentPtr & alignmentMask)
    {
        alignmentOffset = alignment - (currentPtr & alignmentMask);
    }

    // 3. The total size needed includes the padding for alignment
    size_t effectiveSize = size + alignmentOffset;

    // 4. Ensure we don't overflow our pre-allocated block
    ASSERT((arena->used + effectiveSize) <= arena->size);

    // 5. Calculate the final aligned pointer
    void* result = arena->base + arena->used + alignmentOffset;

    // 6. Advance the arena tracker
    arena->used += effectiveSize;

    // Zero the memory for deterministic behavior
    uint8_t* bytePtr = (uint8_t*)result;
    for (size_t i = 0; i < size; ++i)
    {
        bytePtr[i] = 0;
    }

    return result;
}

// Allocates a single struct of a specific type.
#define PushStruct(arena, type) (type*)PushSize(arena, sizeof(type))

// Allocates an array of a specific type.
#define PushArray(arena, count, type) (type*)PushSize(arena, (count) * sizeof(type))
