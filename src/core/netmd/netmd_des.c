// netmd_des.c - see netmd_des.h. FIPS 46-3, table for table.
#include "netmd_des.h"

#include "../../platform/platform.h"

// --- the standard tables ----------------------------------------------------
// FIPS 46-3 numbers bits 1..64 from the most significant bit of the first byte,
// and every table below is a list of *source* bit numbers for each output bit,
// exactly as the standard prints them.

global const u8 des_ip[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17,  9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};

global const u8 des_fp[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41,  9, 49, 17, 57, 25};

global const u8 des_e[48] = {
    32,  1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32,  1};

global const u8 des_p[32] = {
    16,  7, 20, 21, 29, 12, 28, 17,  1, 15, 23, 26,  5, 18, 31, 10,
     2,  8, 24, 14, 32, 27,  3,  9, 19, 13, 30,  6, 22, 11,  4, 25};

global const u8 des_pc1[56] = {
    57, 49, 41, 33, 25, 17,  9,  1, 58, 50, 42, 34, 26, 18,
    10,  2, 59, 51, 43, 35, 27, 19, 11,  3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,  7, 62, 54, 46, 38, 30, 22,
    14,  6, 61, 53, 45, 37, 29, 21, 13,  5, 28, 20, 12,  4};

global const u8 des_pc2[48] = {
    14, 17, 11, 24,  1,  5,  3, 28, 15,  6, 21, 10,
    23, 19, 12,  4, 26,  8, 16,  7, 27, 20, 13,  2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};

global const u8 des_shifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

global const u8 des_sbox[8][64] = {
    {14,  4, 13,  1,  2, 15, 11,  8,  3, 10,  6, 12,  5,  9,  0,  7,
      0, 15,  7,  4, 14,  2, 13,  1, 10,  6, 12, 11,  9,  5,  3,  8,
      4,  1, 14,  8, 13,  6,  2, 11, 15, 12,  9,  7,  3, 10,  5,  0,
     15, 12,  8,  2,  4,  9,  1,  7,  5, 11,  3, 14, 10,  0,  6, 13},
    {15,  1,  8, 14,  6, 11,  3,  4,  9,  7,  2, 13, 12,  0,  5, 10,
      3, 13,  4,  7, 15,  2,  8, 14, 12,  0,  1, 10,  6,  9, 11,  5,
      0, 14,  7, 11, 10,  4, 13,  1,  5,  8, 12,  6,  9,  3,  2, 15,
     13,  8, 10,  1,  3, 15,  4,  2, 11,  6,  7, 12,  0,  5, 14,  9},
    {10,  0,  9, 14,  6,  3, 15,  5,  1, 13, 12,  7, 11,  4,  2,  8,
     13,  7,  0,  9,  3,  4,  6, 10,  2,  8,  5, 14, 12, 11, 15,  1,
     13,  6,  4,  9,  8, 15,  3,  0, 11,  1,  2, 12,  5, 10, 14,  7,
      1, 10, 13,  0,  6,  9,  8,  7,  4, 15, 14,  3, 11,  5,  2, 12},
    { 7, 13, 14,  3,  0,  6,  9, 10,  1,  2,  8,  5, 11, 12,  4, 15,
     13,  8, 11,  5,  6, 15,  0,  3,  4,  7,  2, 12,  1, 10, 14,  9,
     10,  6,  9,  0, 12, 11,  7, 13, 15,  1,  3, 14,  5,  2,  8,  4,
      3, 15,  0,  6, 10,  1, 13,  8,  9,  4,  5, 11, 12,  7,  2, 14},
    { 2, 12,  4,  1,  7, 10, 11,  6,  8,  5,  3, 15, 13,  0, 14,  9,
     14, 11,  2, 12,  4,  7, 13,  1,  5,  0, 15, 10,  3,  9,  8,  6,
      4,  2,  1, 11, 10, 13,  7,  8, 15,  9, 12,  5,  6,  3,  0, 14,
     11,  8, 12,  7,  1, 14,  2, 13,  6, 15,  0,  9, 10,  4,  5,  3},
    {12,  1, 10, 15,  9,  2,  6,  8,  0, 13,  3,  4, 14,  7,  5, 11,
     10, 15,  4,  2,  7, 12,  9,  5,  6,  1, 13, 14,  0, 11,  3,  8,
      9, 14, 15,  5,  2,  8, 12,  3,  7,  0,  4, 10,  1, 13, 11,  6,
      4,  3,  2, 12,  9,  5, 15, 10, 11, 14,  1,  7,  6,  0,  8, 13},
    { 4, 11,  2, 14, 15,  0,  8, 13,  3, 12,  9,  7,  5, 10,  6,  1,
     13,  0, 11,  7,  4,  9,  1, 10, 14,  3,  5, 12,  2, 15,  8,  6,
      1,  4, 11, 13, 12,  3,  7, 14, 10, 15,  6,  8,  0,  5,  9,  2,
      6, 11, 13,  8,  1,  4, 10,  7,  9,  5,  0, 15, 14,  2,  3, 12},
    {13,  2,  8,  4,  6, 15, 11,  1, 10,  9,  3, 14,  5,  0, 12,  7,
      1, 15, 13,  8, 10,  3,  7,  4, 12,  5,  6, 11,  0, 14,  9,  2,
      7, 11,  4,  1,  9, 12, 14,  2,  0,  6, 10, 13, 15,  3,  5,  8,
      2,  1, 14,  7,  4, 10,  8, 13, 15, 12,  9,  0,  3,  5,  6, 11}};

// --- the derived tables -----------------------------------------------------
// 34 KB built once at first use. A bit-at-a-time permutation would cost 64
// iterations per block per permutation; a byte-indexed table costs eight loads.

global u32 des_sp[8][64];        // S box output already permuted by P
global u64 des_ip_table[8][256];
global u64 des_fp_table[8][256];
global u64 des_e_table[4][256];  // the 32 -> 48 bit expansion, right aligned

// 0 nobody, 1 one thread is building, 2 ready. The build is idempotent, but a
// racing reader could see a half filled table, so the losers wait.
global volatile u32 des_tables_state;

// `table` lists the source bit of each output bit, 1 based, MSB first.
// `dst` is `in_bytes` rows of 256 contributions to OR together.
static void des_build_perm_table(const u8 *table, u32 out_bits, u32 in_bytes, u64 *dst) {
    for (u32 byte = 0; byte < in_bytes; byte += 1) {
        for (u32 value = 0; value < 256; value += 1) {
            u64 acc = 0;
            for (u32 i = 0; i < out_bits; i += 1) {
                u32 source = table[i];
                if ((source - 1u) / 8u != byte) { continue; }
                u32 bit = (source - 1u) % 8u;  // 0 is the MSB of the byte
                if (((value >> (7u - bit)) & 1u) == 0) { continue; }
                acc |= (u64)1 << (out_bits - 1u - i);
            }
            dst[byte * 256u + value] = acc;
        }
    }
}

static void des_build_tables(void) {
    des_build_perm_table(des_ip, 64, 8, &des_ip_table[0][0]);
    des_build_perm_table(des_fp, 64, 8, &des_fp_table[0][0]);
    des_build_perm_table(des_e, 48, 4, &des_e_table[0][0]);
    for (u32 box = 0; box < 8; box += 1) {
        for (u32 index = 0; index < 64; index += 1) {
            // s3.1 of the standard: the outer bits pick the row, the four
            // middle ones the column.
            u32 row = ((index >> 4) & 2u) | (index & 1u);
            u32 column = (index >> 1) & 15u;
            u32 value = des_sbox[box][row * 16u + column];
            // The four bits land at positions 4*box+1 .. 4*box+4 of a 32 bit
            // word, then P moves them where they belong.
            u32 pre = value << (28u - 4u * box);
            u32 permuted = 0;
            for (u32 i = 0; i < 32; i += 1) {
                if ((pre >> (32u - des_p[i])) & 1u) { permuted |= 1u << (31u - i); }
            }
            des_sp[box][index] = permuted;
        }
    }
}

static void des_tables_ensure(void) {
    if (os_atomic_load_u32(&des_tables_state) == 2) { return; }
    if (os_atomic_cas_u32(&des_tables_state, 0, 1) == 0) {
        des_build_tables();
        os_atomic_store_u32(&des_tables_state, 2);
        return;
    }
    while (os_atomic_load_u32(&des_tables_state) != 2) { os_cpu_pause(); }
}

// --- block plumbing ---------------------------------------------------------

md_inline u64 des_load_be64(const u8 *p) {
    return ((u64)p[0] << 56) | ((u64)p[1] << 48) | ((u64)p[2] << 40) | ((u64)p[3] << 32) |
           ((u64)p[4] << 24) | ((u64)p[5] << 16) | ((u64)p[6] << 8) | (u64)p[7];
}

md_inline void des_store_be64(u8 *p, u64 v) {
    p[0] = (u8)(v >> 56);
    p[1] = (u8)(v >> 48);
    p[2] = (u8)(v >> 40);
    p[3] = (u8)(v >> 32);
    p[4] = (u8)(v >> 24);
    p[5] = (u8)(v >> 16);
    p[6] = (u8)(v >> 8);
    p[7] = (u8)v;
}

md_inline u64 des_perm64(const u64 table[8][256], u64 x) {
    return table[0][(u32)(x >> 56) & 0xFFu] | table[1][(u32)(x >> 48) & 0xFFu] |
           table[2][(u32)(x >> 40) & 0xFFu] | table[3][(u32)(x >> 32) & 0xFFu] |
           table[4][(u32)(x >> 24) & 0xFFu] | table[5][(u32)(x >> 16) & 0xFFu] |
           table[6][(u32)(x >> 8) & 0xFFu] | table[7][(u32)x & 0xFFu];
}

md_inline u32 des_f(u32 r, u64 subkey) {
    u64 x = (des_e_table[0][(r >> 24) & 0xFFu] | des_e_table[1][(r >> 16) & 0xFFu] |
             des_e_table[2][(r >> 8) & 0xFFu] | des_e_table[3][r & 0xFFu]) ^
            subkey;
    return des_sp[0][(u32)(x >> 42) & 63u] | des_sp[1][(u32)(x >> 36) & 63u] |
           des_sp[2][(u32)(x >> 30) & 63u] | des_sp[3][(u32)(x >> 24) & 63u] |
           des_sp[4][(u32)(x >> 18) & 63u] | des_sp[5][(u32)(x >> 12) & 63u] |
           des_sp[6][(u32)(x >> 6) & 63u] | des_sp[7][(u32)x & 63u];
}

static u64 des_core(const DesKey *key, u64 block, b32 decrypt) {
    u64 state = des_perm64(des_ip_table, block);
    u32 left = (u32)(state >> 32);
    u32 right = (u32)state;
    for (u32 round = 0; round < 16; round += 1) {
        u64 subkey = key->subkey[decrypt ? (15u - round) : round];
        u32 next = left ^ des_f(right, subkey);
        left = right;
        right = next;
    }
    // The halves come back swapped: the last round's swap is undone by writing
    // R || L, which is what makes decryption the same loop backwards.
    return des_perm64(des_fp_table, ((u64)right << 32) | (u64)left);
}

// The key schedule runs once per key, so it stays a bit-at-a-time permutation:
// 104 iterations against the 34 KB a table would cost to save nothing.
static u64 des_permute_bits(u64 source, u32 source_bits, const u8 *table, u32 out_bits) {
    u64 out = 0;
    for (u32 i = 0; i < out_bits; i += 1) {
        if ((source >> (source_bits - table[i])) & 1u) { out |= (u64)1 << (out_bits - 1u - i); }
    }
    return out;
}

void des_key_init(DesKey *key, const u8 bytes[8]) {
    des_tables_ensure();
    u64 pc1 = des_permute_bits(des_load_be64(bytes), 64, des_pc1, 56);
    u32 c = (u32)(pc1 >> 28) & 0x0FFFFFFFu;
    u32 d = (u32)pc1 & 0x0FFFFFFFu;
    for (u32 round = 0; round < 16; round += 1) {
        u32 shift = des_shifts[round];
        c = ((c << shift) | (c >> (28u - shift))) & 0x0FFFFFFFu;
        d = ((d << shift) | (d >> (28u - shift))) & 0x0FFFFFFFu;
        key->subkey[round] = des_permute_bits(((u64)c << 28) | (u64)d, 56, des_pc2, 48);
    }
}

void des3_key_init(Des3Key *key, const u8 bytes[16]) {
    des_key_init(&key->k1, bytes);
    des_key_init(&key->k2, bytes + 8);
}

void des_encrypt_block(const DesKey *key, const u8 in[8], u8 out[8]) {
    des_store_be64(out, des_core(key, des_load_be64(in), 0));
}

void des_decrypt_block(const DesKey *key, const u8 in[8], u8 out[8]) {
    des_store_be64(out, des_core(key, des_load_be64(in), 1));
}

void des3_encrypt_block(const Des3Key *key, const u8 in[8], u8 out[8]) {
    u64 block = des_load_be64(in);
    block = des_core(&key->k1, block, 0);
    block = des_core(&key->k2, block, 1);
    block = des_core(&key->k1, block, 0);
    des_store_be64(out, block);
}

void des3_decrypt_block(const Des3Key *key, const u8 in[8], u8 out[8]) {
    u64 block = des_load_be64(in);
    block = des_core(&key->k1, block, 1);
    block = des_core(&key->k2, block, 0);
    block = des_core(&key->k1, block, 1);
    des_store_be64(out, block);
}

// --- modes ------------------------------------------------------------------

void des_ecb_encrypt(const DesKey *key, const u8 *in, u8 *out, u64 size) {
    for (u64 at = 0; at < size; at += 8) {
        des_store_be64(out + at, des_core(key, des_load_be64(in + at), 0));
    }
}

void des_ecb_decrypt(const DesKey *key, const u8 *in, u8 *out, u64 size) {
    for (u64 at = 0; at < size; at += 8) {
        des_store_be64(out + at, des_core(key, des_load_be64(in + at), 1));
    }
}

void des_cbc_encrypt(const DesKey *key, u8 iv[8], const u8 *in, u8 *out, u64 size) {
    u64 chain = des_load_be64(iv);
    for (u64 at = 0; at < size; at += 8) {
        chain = des_core(key, des_load_be64(in + at) ^ chain, 0);
        des_store_be64(out + at, chain);
    }
    des_store_be64(iv, chain);
}

void des_cbc_decrypt(const DesKey *key, u8 iv[8], const u8 *in, u8 *out, u64 size) {
    u64 chain = des_load_be64(iv);
    for (u64 at = 0; at < size; at += 8) {
        // Read the cipher block before writing: `in` and `out` may be the same.
        u64 cipher = des_load_be64(in + at);
        des_store_be64(out + at, des_core(key, cipher, 1) ^ chain);
        chain = cipher;
    }
    des_store_be64(iv, chain);
}

void des3_cbc_encrypt(const Des3Key *key, u8 iv[8], const u8 *in, u8 *out, u64 size) {
    u64 chain = des_load_be64(iv);
    for (u64 at = 0; at < size; at += 8) {
        u64 block = des_load_be64(in + at) ^ chain;
        block = des_core(&key->k1, block, 0);
        block = des_core(&key->k2, block, 1);
        chain = des_core(&key->k1, block, 0);
        des_store_be64(out + at, chain);
    }
    des_store_be64(iv, chain);
}

void des_retail_mac(const u8 key[16], const u8 *value, u64 size, u8 out[8]) {
    Des3Key ede;
    des3_key_init(&ede, key);
    // Everything but the last block, with K1 alone. On the NetMD case that is
    // one block (the host nonce) and the loop runs once.
    u64 chain = 0;
    for (u64 at = 0; at + 8 < size; at += 8) {
        chain = des_core(&ede.k1, des_load_be64(value + at) ^ chain, 0);
    }
    u64 last = des_load_be64(value + size - 8) ^ chain;
    last = des_core(&ede.k1, last, 0);
    last = des_core(&ede.k2, last, 1);
    last = des_core(&ede.k1, last, 0);
    des_store_be64(out, last);
}
