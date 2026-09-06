// base_crt_stubs.c - symbols MSVC emits on its own. Release (no CRT) only.
// Included by main.c when BUILD_NO_CRT is 1.
#include "base.h"

// The names below are dictated by the toolchain (MSVC codegen and the PE loader),
// not chosen by us, hence the reserved identifier exemption for this file only.
// NOLINTBEGIN(bugprone-reserved-identifier)

typedef unsigned __int64 crt_size;

// Declared before the #pragma function so the compiler knows the intrinsics we replace.
void *memset(void *dst, int value, crt_size size);
void *memcpy(void *dst, const void *src, crt_size size);
void *memmove(void *dst, const void *src, crt_size size);
int memcmp(const void *a, const void *b, crt_size size);

int _fltused = 0x9875;  // referenced as soon as a translation unit touches a float

#pragma function(memset)
void *memset(void *dst, int value, crt_size size) {
    __stosb((u8 *)dst, (u8)value, size);
    return dst;
}

#pragma function(memcpy)
void *memcpy(void *dst, const void *src, crt_size size) {
    __movsb((u8 *)dst, (const u8 *)src, size);
    return dst;
}

#pragma function(memmove)
void *memmove(void *dst, const void *src, crt_size size) {
    mem_move(dst, src, size);
    return dst;
}

#pragma function(memcmp)
int memcmp(const void *a, const void *b, crt_size size) { return mem_cmp(a, b, size); }

// --- thread local storage --------------------------------------------------
// Without the CRT nobody emits the PE TLS directory, so __declspec(thread)
// (used by the scratch arenas) links to an unresolved _tls_index. We author the
// directory ourselves; the loader then allocates the .tls block per thread.
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma section(".tls", long, read, write)
#pragma section(".tls$ZZZ", long, read, write)

__declspec(allocate(".tls")) char _tls_start = 0;
__declspec(allocate(".tls$ZZZ")) char _tls_end = 0;
unsigned long _tls_index = 0;

typedef struct TlsDirectory64 {
    u64 raw_data_start;
    u64 raw_data_end;
    u64 index_address;
    u64 callbacks_address;
    u32 zero_fill_size;
    u32 characteristics;
} TlsDirectory64;

#pragma const_seg(".rdata$T")
extern const TlsDirectory64 _tls_used;
const TlsDirectory64 _tls_used = {
    (u64)&_tls_start, (u64)&_tls_end, (u64)&_tls_index, 0, 0, 0,
};
#pragma const_seg()

// NOLINTEND(bugprone-reserved-identifier)
