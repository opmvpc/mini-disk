// netmd_models.h - the VID/PID table, the only reliable way to know which
// machine is on the other end of the cable (research/01 s1.1): almost every
// Sony portable answers "Net MD Walkman" on iProduct, model included.
//
// Facts of interoperability, transcribed from research/01 s1.2-1.3; no line of
// netmd-js or libnetmd is copied (ADR-008, s10 of the research).
#ifndef NETMD_MODELS_H
#define NETMD_MODELS_H

#include "../../base/base.h"
#include "../../base/base_string.h"

// What the device can do, as far as v1 cares. The families of research/01 s1.3
// decide the rest; what is not here is a T-021+ concern.
typedef enum NetmdCaps {
    NetmdCap_Portable = 1 << 0,  // a recorder with a bay, not a hi-fi deck
    NetmdCap_Deck = 1 << 1,      // hi-fi component ("NetMD*" in the table)
    NetmdCap_TypeS = 1 << 2,     // Type-S SoC, the LP encoding family
    NetmdCap_HiMD = 1 << 3,      // Hi-MD as well as MiniDisc
    NetmdCap_Upload = 1 << 4,    // legal upload, disc -> PC: the MZ-RH1 alone
    NetmdCap_MonoUpload = 1 << 5,
} NetmdCaps;

// Field order is the pointer first: the table is 47 entries of read only data
// and packing it costs nothing (clang-tidy's padding check agrees).
typedef struct NetmdModel {
    const char *name;
    u32 caps;
    u16 vid;
    u16 pid;
} NetmdModel;

// 0 when the pair is not a NetMD device. This *is* the filter: a USB device the
// table does not know is not ours, whatever its vendor.
const NetmdModel *netmd_model_lookup(u16 vid, u16 pid);
u64 netmd_model_count(void);

md_inline b32 netmd_model_known(u16 vid, u16 pid) { return netmd_model_lookup(vid, pid) != 0; }

// The name to show, "Sony MZ-N505" for the reference device; empty for an
// unknown pair, which the UI never asks about.
md_inline String8 netmd_model_name(u16 vid, u16 pid) {
    const NetmdModel *model = netmd_model_lookup(vid, pid);
    return model ? str8_cstr(model->name) : str8(0, 0);
}

#endif  // NETMD_MODELS_H
