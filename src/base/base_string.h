// base_string.h - String8 slices (not null terminated), UTF-8/UTF-16, own formatting.
#ifndef BASE_STRING_H
#define BASE_STRING_H

#include <stdarg.h>

#include "base.h"
#include "base_arena.h"

typedef struct String8 {
    u8 *str;
    u64 size;
} String8;

typedef struct String16 {
    u16 *str;
    u64 size;  // in u16 units, excluding the null terminator we always append
} String16;

typedef struct String8Node {
    struct String8Node *next;
    String8 str;
} String8Node;

typedef struct String8List {
    String8Node *first;
    String8Node *last;
    u64 count;
    u64 total_size;
} String8List;

#define str8_lit(s) str8((u8 *)(s), sizeof(s) - 1)
#define str8_expand(s) (int)((s).size), (char *)((s).str)

md_inline String8 str8(u8 *str, u64 size) {
    String8 result;
    result.str = str;
    result.size = size;
    return result;
}

String8 str8_cstr(const char *cstr);
String8 str8_copy(Arena *arena, String8 s);
String8 str8_cat(Arena *arena, String8 a, String8 b);
char   *str8_to_cstr(Arena *arena, String8 s);

i32  str8_cmp(String8 a, String8 b);          // -1 / 0 / 1, byte order
b32  str8_eq(String8 a, String8 b);
b32  str8_starts_with(String8 s, String8 prefix);
b32  str8_ends_with(String8 s, String8 suffix);
u64  str8_find(String8 s, String8 needle, u64 start);  // returns s.size if absent
String8 str8_substr(String8 s, u64 offset, u64 size);  // clamped
String8 str8_skip(String8 s, u64 count);
String8 str8_prefix(String8 s, u64 count);
String8 str8_trim(String8 s);                 // ASCII whitespace on both ends

String8List str8_split(Arena *arena, String8 s, u8 separator);
void        str8_list_push(Arena *arena, String8List *list, String8 s);
String8     str8_list_join(Arena *arena, String8List *list, String8 separator);

// --- UTF-8 / UTF-16 --------------------------------------------------------
#define UNICODE_REPLACEMENT 0xFFFDu

typedef struct UnicodeDecode {
    u32 codepoint;
    u32 advance;  // units consumed
} UnicodeDecode;

UnicodeDecode utf8_decode(const u8 *str, u64 max);
UnicodeDecode utf16_decode(const u16 *str, u64 max);
u32 utf8_encode(u8 *out, u32 codepoint);    // returns bytes written (1..4)
u32 utf16_encode(u16 *out, u32 codepoint);  // returns units written (1..2)

// Win32 boundary only: allocate in a scratch arena, pass str, forget.
String16 str16_from_str8(Arena *arena, String8 s);   // null terminated
String8  str8_from_str16(Arena *arena, String16 s);
String8  str8_from_cstr16(Arena *arena, const u16 *cstr);

// --- formatting (replaces the C library, %d %i %u %x %s %S %c %f %%) -------
String8 str8fv(Arena *arena, const char *fmt, va_list args);
String8 str8f(Arena *arena, const char *fmt, ...);
u64     str8_format_buffer(u8 *buffer, u64 capacity, const char *fmt, va_list args);

#endif // BASE_STRING_H
