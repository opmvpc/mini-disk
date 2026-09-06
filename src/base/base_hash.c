#include "base_hash.h"

u64 hash64_seed(const void *data, u64 size, u64 seed) {
    const u8 *bytes = (const u8 *)data;
    u64 hash = seed;
    for (u64 i = 0; i < size; i += 1) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

u64 hash64(const void *data, u64 size) { return hash64_seed(data, size, HASH64_SEED); }

u64 hash64_mix(u64 x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

u64 hash64_combine(u64 a, u64 b) { return hash64_mix(a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))); }
