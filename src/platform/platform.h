// platform.h - the only contract towards the OS (research/03 s10.2).
// T-001 implements the subset below; the rest of the sketch lands with later tickets.
#ifndef PLATFORM_H
#define PLATFORM_H

#include "../base/base.h"
#include "../base/base_string.h"

// --- lifecycle -------------------------------------------------------------
void os_init(void);
no_return void os_exit(i32 code);

// --- virtual memory --------------------------------------------------------
void *os_memory_reserve(u64 size);
b32   os_memory_commit(void *ptr, u64 size);
void  os_memory_release(void *ptr, u64 size);
u64   os_page_size(void);

// --- files -----------------------------------------------------------------
String8 os_file_read_all(Arena *arena, String8 path);   // size 0 when unreadable
b32     os_file_write_all(String8 path, String8 data);

// --- time and threads ------------------------------------------------------
u64 os_time_now_us(void);      // monotonic, microseconds
u32 os_thread_current_id(void);

// --- diagnostics -----------------------------------------------------------
void os_debug_print(String8 s);

#endif // PLATFORM_H
