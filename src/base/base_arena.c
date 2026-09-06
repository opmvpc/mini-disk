#include "base_arena.h"
#include "../platform/platform.h"

#define SCRATCH_COUNT   2
#define SCRATCH_RESERVE MB(256)

thread_var Arena *tls_scratch[SCRATCH_COUNT];

Arena *arena_alloc(u64 reserve_size) {
    u64 reserved = AlignPow2(reserve_size, ARENA_COMMIT_CHUNK);
    u8 *base = (u8 *)os_memory_reserve(reserved);
    AssertAlways(base != 0);
    AssertAlways(os_memory_commit(base, ARENA_COMMIT_CHUNK));
    Arena *arena = (Arena *)base;
    arena->reserved = reserved;
    arena->committed = ARENA_COMMIT_CHUNK;
    arena->pos = ARENA_HEADER_SIZE;
    arena->align = ARENA_DEFAULT_ALIGN;
    return arena;
}

void arena_release(Arena *arena) {
    os_memory_release(arena, arena->reserved);
}

void *arena_push(Arena *arena, u64 size, u64 align) {
    Assert(IsPow2(align));
    u64 pos = AlignPow2(arena->pos, align);
    u64 next = pos + size;
    // Not defensive programming: an arena that overflows is a sizing bug.
    AssertAlways(next <= arena->reserved);
    if (next > arena->committed) {
        u64 target = AlignPow2(next, ARENA_COMMIT_CHUNK);
        if (target > arena->reserved) { target = arena->reserved; }
        AssertAlways(os_memory_commit((u8 *)arena + arena->committed, target - arena->committed));
        arena->committed = target;
    }
    arena->pos = next;
    return (u8 *)arena + pos;
}

void *arena_push_zero(Arena *arena, u64 size, u64 align) {
    void *result = arena_push(arena, size, align);
    mem_zero(result, size);
    return result;
}

void arena_pop_to(Arena *arena, u64 pos) {
    Assert(pos >= ARENA_HEADER_SIZE && pos <= arena->pos);
    arena->pos = pos;
}

void arena_clear(Arena *arena) { arena->pos = ARENA_HEADER_SIZE; }

u64 arena_pos(Arena *arena) { return arena->pos; }

ArenaTemp arena_temp_begin(Arena *arena) {
    ArenaTemp temp;
    temp.arena = arena;
    temp.pos = arena->pos;
    return temp;
}

void arena_temp_end(ArenaTemp temp) { arena_pop_to(temp.arena, temp.pos); }

ArenaTemp scratch_begin(Arena **conflicts, u64 conflict_count) {
    Arena *chosen = 0;
    for (u64 i = 0; i < SCRATCH_COUNT && chosen == 0; i += 1) {
        if (tls_scratch[i] == 0) { tls_scratch[i] = arena_alloc(SCRATCH_RESERVE); }
        b32 conflicting = 0;
        for (u64 c = 0; c < conflict_count; c += 1) {
            if (conflicts[c] == tls_scratch[i]) { conflicting = 1; }
        }
        if (!conflicting) { chosen = tls_scratch[i]; }
    }
    AssertAlways(chosen != 0);
    return arena_temp_begin(chosen);
}

void scratch_end(ArenaTemp temp) { arena_temp_end(temp); }

void scratch_thread_release(void) {
    for (u64 i = 0; i < SCRATCH_COUNT; i += 1) {
        if (tls_scratch[i] != 0) {
            arena_release(tls_scratch[i]);
            tls_scratch[i] = 0;
        }
    }
}
