#include "base_string.h"

// --- basics ----------------------------------------------------------------

String8 str8_cstr(const char *cstr) {
    u64 size = 0;
    while (cstr[size] != 0) { size += 1; }
    return str8((u8 *)cstr, size);
}

String8 str8_copy(Arena *arena, String8 s) {
    u8 *bytes = push_array(arena, u8, s.size + 1);
    mem_copy(bytes, s.str, s.size);
    bytes[s.size] = 0;
    return str8(bytes, s.size);
}

String8 str8_cat(Arena *arena, String8 a, String8 b) {
    u8 *bytes = push_array(arena, u8, a.size + b.size + 1);
    mem_copy(bytes, a.str, a.size);
    mem_copy(bytes + a.size, b.str, b.size);
    bytes[a.size + b.size] = 0;
    return str8(bytes, a.size + b.size);
}

char *str8_to_cstr(Arena *arena, String8 s) { return (char *)str8_copy(arena, s).str; }

i32 str8_cmp(String8 a, String8 b) {
    u64 common = Min(a.size, b.size);
    i32 order = mem_cmp(a.str, b.str, common);
    if (order != 0) { return order; }
    if (a.size == b.size) { return 0; }
    return (a.size < b.size) ? -1 : 1;
}

b32 str8_eq(String8 a, String8 b) {
    return (a.size == b.size) && (mem_cmp(a.str, b.str, a.size) == 0);
}

b32 str8_starts_with(String8 s, String8 prefix) {
    return (s.size >= prefix.size) && (mem_cmp(s.str, prefix.str, prefix.size) == 0);
}

b32 str8_ends_with(String8 s, String8 suffix) {
    return (s.size >= suffix.size) &&
           (mem_cmp(s.str + s.size - suffix.size, suffix.str, suffix.size) == 0);
}

u64 str8_find(String8 s, String8 needle, u64 start) {
    if (needle.size == 0 || needle.size > s.size) { return s.size; }
    for (u64 i = start; i + needle.size <= s.size; i += 1) {
        if (mem_cmp(s.str + i, needle.str, needle.size) == 0) { return i; }
    }
    return s.size;
}

String8 str8_substr(String8 s, u64 offset, u64 size) {
    u64 begin = Min(offset, s.size);
    u64 end = Min(begin + size, s.size);
    return str8(s.str + begin, end - begin);
}

String8 str8_skip(String8 s, u64 count) { return str8_substr(s, count, s.size); }
String8 str8_prefix(String8 s, u64 count) { return str8_substr(s, 0, count); }

static b32 str8_is_space(u8 c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

String8 str8_trim(String8 s) {
    u64 begin = 0;
    u64 end = s.size;
    while (begin < end && str8_is_space(s.str[begin])) { begin += 1; }
    while (end > begin && str8_is_space(s.str[end - 1])) { end -= 1; }
    return str8(s.str + begin, end - begin);
}

void str8_list_push(Arena *arena, String8List *list, String8 s) {
    String8Node *node = push_struct_zero(arena, String8Node);
    node->str = s;
    if (list->last != 0) { list->last->next = node; } else { list->first = node; }
    list->last = node;
    list->count += 1;
    list->total_size += s.size;
}

String8List str8_split(Arena *arena, String8 s, u8 separator) {
    String8List list;
    StructZero(&list);
    u64 begin = 0;
    for (u64 i = 0; i <= s.size; i += 1) {
        if (i == s.size || s.str[i] == separator) {
            if (i > begin) { str8_list_push(arena, &list, str8(s.str + begin, i - begin)); }
            begin = i + 1;
        }
    }
    return list;
}

String8 str8_list_join(Arena *arena, String8List *list, String8 separator) {
    u64 total = list->total_size;
    if (list->count > 1) { total += separator.size * (list->count - 1); }
    u8 *bytes = push_array(arena, u8, total + 1);
    u64 at = 0;
    for (String8Node *node = list->first; node != 0; node = node->next) {
        if (at != 0) {
            mem_copy(bytes + at, separator.str, separator.size);
            at += separator.size;
        }
        mem_copy(bytes + at, node->str.str, node->str.size);
        at += node->str.size;
    }
    bytes[total] = 0;
    return str8(bytes, total);
}

// --- unicode ---------------------------------------------------------------

static const u8 utf8_class[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 5,
};

UnicodeDecode utf8_decode(const u8 *str, u64 max) {
    UnicodeDecode result;
    result.codepoint = UNICODE_REPLACEMENT;
    result.advance = 1;
    if (max == 0) { result.advance = 0; return result; }
    u8 byte = str[0];
    u8 byte_class = utf8_class[byte >> 3];
    switch (byte_class) {
        case 1: {
            result.codepoint = byte;
        } break;
        case 2: {
            if (max >= 2 && utf8_class[str[1] >> 3] == 0) {
                result.codepoint = ((u32)(byte & 0x1Fu) << 6) | (u32)(str[1] & 0x3Fu);
                result.advance = 2;
            }
        } break;
        case 3: {
            if (max >= 3 && utf8_class[str[1] >> 3] == 0 && utf8_class[str[2] >> 3] == 0) {
                result.codepoint = ((u32)(byte & 0x0Fu) << 12) | ((u32)(str[1] & 0x3Fu) << 6) |
                                   (u32)(str[2] & 0x3Fu);
                result.advance = 3;
            }
        } break;
        case 4: {
            if (max >= 4 && utf8_class[str[1] >> 3] == 0 && utf8_class[str[2] >> 3] == 0 &&
                utf8_class[str[3] >> 3] == 0) {
                result.codepoint = ((u32)(byte & 0x07u) << 18) | ((u32)(str[1] & 0x3Fu) << 12) |
                                   ((u32)(str[2] & 0x3Fu) << 6) | (u32)(str[3] & 0x3Fu);
                result.advance = 4;
            }
        } break;
        default: break;
    }
    return result;
}

u32 utf8_encode(u8 *out, u32 codepoint) {
    u32 count = 1;
    if (codepoint <= 0x7F) {
        out[0] = (u8)codepoint;
    } else if (codepoint <= 0x7FF) {
        out[0] = (u8)(0xC0u | (codepoint >> 6));
        out[1] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 2;
    } else if (codepoint <= 0xFFFF) {
        out[0] = (u8)(0xE0u | (codepoint >> 12));
        out[1] = (u8)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 3;
    } else if (codepoint <= 0x10FFFF) {
        out[0] = (u8)(0xF0u | (codepoint >> 18));
        out[1] = (u8)(0x80u | ((codepoint >> 12) & 0x3Fu));
        out[2] = (u8)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[3] = (u8)(0x80u | (codepoint & 0x3Fu));
        count = 4;
    } else {
        count = utf8_encode(out, UNICODE_REPLACEMENT);
    }
    return count;
}

UnicodeDecode utf16_decode(const u16 *str, u64 max) {
    UnicodeDecode result;
    result.codepoint = UNICODE_REPLACEMENT;
    result.advance = 1;
    if (max == 0) { result.advance = 0; return result; }
    u16 unit = str[0];
    if (unit < 0xD800u || unit > 0xDFFFu) {
        result.codepoint = unit;
    } else if (unit <= 0xDBFFu && max >= 2 && str[1] >= 0xDC00u && str[1] <= 0xDFFFu) {
        result.codepoint = 0x10000u + (((u32)(unit - 0xD800u) << 10) | (u32)(str[1] - 0xDC00u));
        result.advance = 2;
    }
    return result;
}

u32 utf16_encode(u16 *out, u32 codepoint) {
    if (codepoint < 0x10000u) {
        out[0] = (u16)codepoint;
        return 1;
    }
    u32 v = codepoint - 0x10000u;
    out[0] = (u16)(0xD800u + (v >> 10));
    out[1] = (u16)(0xDC00u + (v & 0x3FFu));
    return 2;
}

String16 str16_from_str8(Arena *arena, String8 s) {
    u16 *units = push_array(arena, u16, s.size * 2 + 1);
    u64 at = 0;
    u64 i = 0;
    while (i < s.size) {
        UnicodeDecode decode = utf8_decode(s.str + i, s.size - i);
        at += utf16_encode(units + at, decode.codepoint);
        i += decode.advance;
    }
    units[at] = 0;
    String16 result;
    result.str = units;
    result.size = at;
    return result;
}

String8 str8_from_str16(Arena *arena, String16 s) {
    u8 *bytes = push_array(arena, u8, s.size * 3 + 1);
    u64 at = 0;
    u64 i = 0;
    while (i < s.size) {
        UnicodeDecode decode = utf16_decode(s.str + i, s.size - i);
        at += utf8_encode(bytes + at, decode.codepoint);
        i += decode.advance;
    }
    bytes[at] = 0;
    return str8(bytes, at);
}

String8 str8_from_cstr16(Arena *arena, const u16 *cstr) {
    u64 size = 0;
    while (cstr[size] != 0) { size += 1; }
    String16 s;
    s.str = (u16 *)cstr;
    s.size = size;
    return str8_from_str16(arena, s);
}

// --- formatting ------------------------------------------------------------

typedef struct FormatSink {
    u8 *buffer;
    u64 capacity;
    u64 size;  // bytes that would be written, may exceed capacity
} FormatSink;

static void format_byte(FormatSink *sink, u8 byte) {
    if (sink->size < sink->capacity) { sink->buffer[sink->size] = byte; }
    sink->size += 1;
}

static void format_bytes(FormatSink *sink, const u8 *bytes, u64 count) {
    for (u64 i = 0; i < count; i += 1) { format_byte(sink, bytes[i]); }
}

static void format_u64(FormatSink *sink, u64 value, u32 base, u32 min_digits) {
    static const char digits[] = "0123456789abcdef";
    u8 tmp[64];
    u32 count = 0;
    do {
        tmp[count] = (u8)digits[value % base];
        value /= base;
        count += 1;
    } while (value != 0);
    while (count < min_digits && count < ArrayCount(tmp)) {
        tmp[count] = '0';
        count += 1;
    }
    while (count > 0) {
        count -= 1;
        format_byte(sink, tmp[count]);
    }
}

static void format_i64(FormatSink *sink, i64 value, u32 min_digits) {
    u64 magnitude;
    if (value < 0) {
        format_byte(sink, '-');
        magnitude = (u64)(-(value + 1)) + 1;  // no overflow on I64_MIN
    } else {
        magnitude = (u64)value;
    }
    format_u64(sink, magnitude, 10, min_digits);
}

static void format_f64(FormatSink *sink, f64 value, u32 decimals) {
    u64 bits = 0;
    mem_copy(&bits, &value, sizeof(bits));
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) {
        b32 is_nan = (bits & 0x000FFFFFFFFFFFFFull) != 0;
        if (!is_nan && (bits >> 63) != 0) { format_byte(sink, '-'); }
        format_bytes(sink, is_nan ? (const u8 *)"nan" : (const u8 *)"inf", 3);
        return;
    }
    if (value < 0.0) {
        format_byte(sink, '-');
        value = -value;
    }
    u64 scale = 1;
    for (u32 i = 0; i < decimals; i += 1) { scale *= 10; }
    // Split before rounding so huge values do not overflow the fraction path.
    u64 whole = (u64)value;
    f64 rest = value - (f64)whole;
    u64 fraction = (u64)(rest * (f64)scale + 0.5);
    if (fraction >= scale) {
        whole += 1;
        fraction -= scale;
    }
    format_u64(sink, whole, 10, 1);
    if (decimals > 0) {
        format_byte(sink, '.');
        format_u64(sink, fraction, 10, decimals);
    }
}

u64 str8_format_buffer(u8 *buffer, u64 capacity, const char *fmt, va_list args) {
    FormatSink sink;
    sink.buffer = buffer;
    sink.capacity = capacity;
    sink.size = 0;
    for (u64 i = 0; fmt[i] != 0; i += 1) {
        if (fmt[i] != '%') {
            format_byte(&sink, (u8)fmt[i]);
            continue;
        }
        i += 1;
        u32 min_digits = 0;
        if (fmt[i] == '0') {
            i += 1;
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                min_digits = min_digits * 10 + (u32)(fmt[i] - '0');
                i += 1;
            }
        }
        u32 long_count = 0;
        while (fmt[i] == 'l' || fmt[i] == 'z') {
            long_count += 1;
            i += 1;
        }
        switch (fmt[i]) {
            case 'd':
            case 'i': {
                i64 value = long_count ? va_arg(args, i64) : (i64)va_arg(args, i32);
                format_i64(&sink, value, min_digits);
            } break;
            case 'u': {
                u64 value = long_count ? va_arg(args, u64) : (u64)va_arg(args, u32);
                format_u64(&sink, value, 10, min_digits);
            } break;
            case 'x': {
                u64 value = long_count ? va_arg(args, u64) : (u64)va_arg(args, u32);
                format_u64(&sink, value, 16, min_digits);
            } break;
            case 'c': format_byte(&sink, (u8)va_arg(args, i32)); break;
            case 'f': format_f64(&sink, va_arg(args, f64), min_digits ? min_digits : 3); break;
            case 's': {
                String8 s = str8_cstr(va_arg(args, const char *));
                format_bytes(&sink, s.str, s.size);
            } break;
            case 'S': {
                String8 s = va_arg(args, String8);
                format_bytes(&sink, s.str, s.size);
            } break;
            case '%': format_byte(&sink, '%'); break;
            default: format_byte(&sink, (u8)fmt[i]); break;
        }
    }
    if (capacity > 0) {
        u64 terminator = Min(sink.size, capacity - 1);
        buffer[terminator] = 0;
    }
    return sink.size;
}

String8 str8fv(Arena *arena, const char *fmt, va_list args) {
    u8 stack_buffer[1024];
    va_list probe;
    va_copy(probe, args);
    u64 size = str8_format_buffer(stack_buffer, sizeof(stack_buffer), fmt, probe);
    va_end(probe);
    if (size < sizeof(stack_buffer)) { return str8_copy(arena, str8(stack_buffer, size)); }
    u8 *bytes = push_array(arena, u8, size + 1);
    str8_format_buffer(bytes, size + 1, fmt, args);
    return str8(bytes, size);
}

String8 str8f(Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 result = str8fv(arena, fmt, args);
    va_end(args);
    return result;
}

String8 str8f_buf(u8 *buffer, u64 capacity, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    u64 size = str8_format_buffer(buffer, capacity, fmt, args);
    va_end(args);
    return str8(buffer, Min(size, capacity));
}
