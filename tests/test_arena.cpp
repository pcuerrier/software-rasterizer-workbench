// ============================================================
//  Tests for the arena bump allocator (src/arena.h)
//
//  Approach: "include the source." arena.h is header-only (all
//  functions are `inline`), so we include it directly and it is
//  compiled into this test executable. No SDL, no platform layer,
//  no separate library — a pure unit under test.
//
//  Each test pins ONE invariant with a real failure mode. We test
//  the *contract* (aligned, zeroed, disjoint, correctly accounted),
//  not the implementation, so the allocator can be rewritten later
//  and these tests still guard it.
// ============================================================

#include <gtest/gtest.h>
#include <cstring>   // memset

#include "arena.h"

// The allocator's initial state is the foundation everything else
// assumes. If `used` doesn't start at zero, the very first allocation
// lands in the middle of the buffer.
TEST(Arena, Initialize_StartsEmpty)
{
    uint8_t backing[256];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    EXPECT_EQ(arena.base, backing);
    EXPECT_EQ(arena.size, sizeof(backing));
    EXPECT_EQ(arena.used, size_t(0));
}

// Alignment is the allocator's core job: a misaligned pointer handed
// to code expecting an aligned type is a subtle, platform-dependent
// crash. We first knock the offset off-alignment, then demand aligned
// blocks and check the absolute address is aligned.
TEST(Arena, Push_RespectsAlignment)
{
    uint8_t backing[256];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    PushSize(&arena, 1, 1);                     // consume 1 byte -> likely misaligned now
    void* p8 = PushSize(&arena, 4, 8);
    EXPECT_EQ((uintptr_t)p8 % 8, uintptr_t(0));

    PushSize(&arena, 1, 1);                     // knock it off again
    void* p16 = PushSize(&arena, 4, 16);
    EXPECT_EQ((uintptr_t)p16 % 16, uintptr_t(0));
}

// Callers rely on fresh allocations being zeroed (structs default to
// 0). We dirty the whole backing buffer first, then verify the block
// PushSize hands back is zero.
TEST(Arena, Push_ZeroesMemory)
{
    uint8_t backing[256];
    memset(backing, 0xFF, sizeof(backing));

    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    uint8_t* p = (uint8_t*)PushSize(&arena, 16, 1);
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(p[i], 0) << "byte " << i << " was not zeroed";
}

// The fundamental bump-allocator invariant: each allocation is a
// distinct region, and `used` advances by the requested size. We use
// alignment 1 so there is no padding and the accounting is exact.
TEST(Arena, Push_AllocationsAreDisjoint)
{
    uint8_t backing[256];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    uint8_t* p1 = (uint8_t*)PushSize(&arena, 10, 1);
    EXPECT_EQ(arena.used, size_t(10));

    uint8_t* p2 = (uint8_t*)PushSize(&arena, 10, 1);
    EXPECT_EQ(arena.used, size_t(20));
    EXPECT_GE(p2, p1 + 10) << "second allocation overlaps the first";
}

// PushStruct must forward sizeof(T) to PushSize and return a pointer
// we can actually use. Default alignment is 8.
TEST(Arena, PushStruct_AllocatesStructSize)
{
    struct Foo { uint32_t a; uint32_t b; };     // 8 bytes

    uint8_t backing[256];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    size_t before = arena.used;
    Foo* f = PushStruct(&arena, Foo);

    EXPECT_EQ((uintptr_t)f % 8, uintptr_t(0));
    EXPECT_GE(arena.used - before, sizeof(Foo));

    f->a = 111; f->b = 222;                     // must be in-bounds & usable
    EXPECT_EQ(f->a, 111u);
    EXPECT_EQ(f->b, 222u);
}

// PushArray must reserve count * sizeof(T). If the multiply is wrong,
// writing all N elements overruns the allocation — which a sanitizer
// or the disjointness of a following allocation would expose.
TEST(Arena, PushArray_AllocatesCount)
{
    uint8_t backing[512];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    const int N = 8;
    int32_t* arr = PushArray(&arena, N, int32_t);

    for (int i = 0; i < N; ++i) arr[i] = i * 7;      // write the whole array
    for (int i = 0; i < N; ++i) EXPECT_EQ(arr[i], i * 7);

    EXPECT_GE(arena.used, size_t(N * sizeof(int32_t)));
}

// The transient arena is cleared every frame; Clear must reset `used`
// to zero AND make that memory available again from the top.
TEST(Arena, Clear_ResetsAndReuses)
{
    uint8_t backing[256];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    void* p1 = PushSize(&arena, 32, 8);
    ClearArena(&arena);
    EXPECT_EQ(arena.used, size_t(0));

    void* p2 = PushSize(&arena, 32, 8);          // same size+alignment
    EXPECT_EQ(p1, p2) << "memory was not reused after Clear";
}

// Boundary case: allocating exactly the remaining capacity must
// succeed. The guard is `used + size <= arena->size`; an off-by-one
// to `<` would wrongly abort here. Alignment 1 => no padding, so the
// buffer fills exactly.
TEST(Arena, Push_ExactFitSucceeds)
{
    uint8_t backing[64];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    void* p = PushSize(&arena, sizeof(backing), 1);
    EXPECT_EQ(p, backing);
    EXPECT_EQ(arena.used, sizeof(backing));
}

// The overflow guard only exists in debug builds (ASSERT -> __debugbreak;
// it compiles to nothing in release). So this death test is compiled
// only when DEBUG is defined. It asserts that asking for more than the
// arena holds terminates the process instead of silently overflowing.
//
// Convention: death-test suites are named "...DeathTest". The "" regex
// matches any (here, empty) child output. If this ever pops a dialog or
// hangs in your environment, run with --gtest_death_test_style=threadsafe
// or drop it — its everyday value is lower than the tests above.
#if DEBUG
TEST(ArenaDeathTest, Push_OverflowTripsAssert)
{
    uint8_t backing[16];
    MemoryArena arena;
    InitializeArena(&arena, backing, sizeof(backing));

    EXPECT_DEATH(PushSize(&arena, sizeof(backing) + 1, 1), "");
}
#endif
