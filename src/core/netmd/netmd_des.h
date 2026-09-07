// netmd_des.h - DES, 3DES and the retail MAC, written from FIPS 46-3 and
// ISO 9797-1 (ADR-008, clean room: the permutation and S-box tables are the
// published standard, and nothing else here comes from anywhere).
//
// The whole NetMD download sequence is built out of five primitives and no
// more (research/01 s4.6-4.12):
//   * DES-ECB encrypt   - the commit authentication, DES-ECB(0^8, sessionKey)
//   * DES-ECB decrypt   - the data key, which is a *decryption* of a random key
//   * DES-CBC encrypt   - setupDownload's 32 bytes, and every audio packet
//   * DES-CBC decrypt   - the 32 byte blob sendTrack answers with
//   * retail MAC        - the session key, from the two nonces
// There is no padding scheme anywhere: every buffer is already a multiple of
// eight, because a SP frame is 2048 bytes and setupDownload is 32.
//
// Speed: the audio of a 74 minute disc is 780 MB through DES-CBC, so the round
// function goes through SP tables (the S box output already permuted by P) and
// the three bit permutations through 8x256 byte-indexed tables. All of them are
// built once, at first use, from the standard tables above - the project builds
// tables at runtime rather than shipping 34 KB of constants in the exe.
#ifndef NETMD_DES_H
#define NETMD_DES_H

#include "../../base/base.h"

#define DES_BLOCK_BYTES 8

typedef struct DesKey {
    u64 subkey[16];  // 48 bits each, right aligned; reversed by des_key_decrypt
} DesKey;

// Two-key EDE (ANSI X9.52), which is all the NetMD protocol uses: K3 = K1.
typedef struct Des3Key {
    DesKey k1;
    DesKey k2;
} Des3Key;

void des_key_init(DesKey *key, const u8 bytes[8]);
void des3_key_init(Des3Key *key, const u8 bytes[16]);

void des_encrypt_block(const DesKey *key, const u8 in[8], u8 out[8]);
void des_decrypt_block(const DesKey *key, const u8 in[8], u8 out[8]);
void des3_encrypt_block(const Des3Key *key, const u8 in[8], u8 out[8]);
void des3_decrypt_block(const Des3Key *key, const u8 in[8], u8 out[8]);

// `size` is a multiple of 8. `in` and `out` may be the same buffer.
void des_ecb_encrypt(const DesKey *key, const u8 *in, u8 *out, u64 size);
void des_ecb_decrypt(const DesKey *key, const u8 *in, u8 *out, u64 size);
// `iv` is read and then updated to the last cipher block, which is what makes
// the chaining continue across two calls - and the audio packets of s4.10 are
// exactly that: one CBC stream cut into pieces.
void des_cbc_encrypt(const DesKey *key, u8 iv[8], const u8 *in, u8 *out, u64 size);
void des_cbc_decrypt(const DesKey *key, u8 iv[8], const u8 *in, u8 *out, u64 size);
void des3_cbc_encrypt(const Des3Key *key, u8 iv[8], const u8 *in, u8 *out, u64 size);

// ISO 9797-1 algorithm 3 (retail MAC / ANSI X9.19), research/01 s4.6: DES-CBC
// with K1 over every block but the last, then one 3DES-EDE step over the last
// with the running IV. `size` is a multiple of 8 and at least 8.
void des_retail_mac(const u8 key[16], const u8 *value, u64 size, u8 out[8]);

#endif  // NETMD_DES_H
