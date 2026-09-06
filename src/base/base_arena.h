// base_arena.h - linear allocators over reserved virtual memory (ADR-002, research/03 s1.6).
#ifndef BASE_ARENA_H
#define BASE_ARENA_H

#include "base.h"

#define ARENA_HEADER_SIZE     64             // one cache line, data starts after it
#define ARENA_COMMIT_CHUNK    KB(64)         // VirtualAlloc granularity
#define ARENA_DEFAULT_RESERVE GB(64)         // virtual reserve is free on x64
#define ARENA_DEFAULT_ALIGN   8

typedef struct Arena {
    u64 reserved;   // bytes reserved, header included
    u64 committed;  // bytes committed, header included
    u64 pos;        // current allocation offset from the arena base
    u64 align;      // default alignment
} Arena;

typedef struct ArenaTemp {
    Arena *arena;
    u64 pos;
} ArenaTemp;

Arena *arena_alloc(u64 reserve_size);
void   arena_release(Arena *arena);
void  *arena_push(Arena *arena, u64 size, u64 align);
void  *arena_push_zero(Arena *arena, u64 size, u64 align);
void   arena_pop_to(Arena *arena, u64 pos);
void   arena_clear(Arena *arena);
u64    arena_pos(Arena *arena);

#define push_array(a, T, n)      ((T *)arena_push((a), sizeof(T) * (u64)(n), _Alignof(T)))
#define push_array_zero(a, T, n) ((T *)arena_push_zero((a), sizeof(T) * (u64)(n), _Alignof(T)))
#define push_struct(a, T)        push_array(a, T, 1)
#define push_struct_zero(a, T)   push_array_zero(a, T, 1)

ArenaTemp arena_temp_begin(Arena *arena);
void      arena_temp_end(ArenaTemp temp);

// Scratch arenas: two per thread, borrowed with an explicit conflict list so a
// function writing into an output arena never picks that same arena as scratch.
ArenaTemp scratch_begin(Arena **conflicts, u64 conflict_count);
void      scratch_end(ArenaTemp temp);
void      scratch_thread_release(void);
u64       scratch_thread_committed(void);  // this thread's scratch, for the overlay

#endif // BASE_ARENA_H
