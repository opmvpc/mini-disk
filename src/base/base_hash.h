// base_hash.h - 64 bit hashing (FNV-1a) and integer mixing.
#ifndef BASE_HASH_H
#define BASE_HASH_H

#include "base.h"

#define HASH64_SEED 0xCBF29CE484222325ull

u64 hash64(const void *data, u64 size);
u64 hash64_seed(const void *data, u64 size, u64 seed);
u64 hash64_mix(u64 x);           // splitmix64 finaliser, avalanches a counter
u64 hash64_combine(u64 a, u64 b);

#endif // BASE_HASH_H
