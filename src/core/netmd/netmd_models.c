// netmd_models.c - the table itself (research/01 s1.2). Sorted by VID then PID
// so a reader can find a device, not because the lookup needs it: 47 entries
// scanned linearly is one cache line walk, done once per hotplug event.
#include "netmd_models.h"

global const NetmdModel netmd_models[] = {
    // Sharp, whose disc rename goes through another descriptor (research/01 s3.9).
    {"Sharp IM-MT899H", NetmdCap_Portable, 0x04DD, 0x7202},
    {"Sharp IM-DR400", NetmdCap_Portable, 0x04DD, 0x9013},
    {"Sharp IM-DR80", NetmdCap_Portable | NetmdCap_MonoUpload, 0x04DD, 0x9014},
    // Panasonic: answers 0803 where the others answer 8003 on the capacity.
    {"Panasonic SJ-MR250", NetmdCap_Portable | NetmdCap_MonoUpload, 0x04DA, 0x23B3},
    {"Panasonic SJ-MR270", NetmdCap_Portable | NetmdCap_MonoUpload, 0x04DA, 0x23B6},
    {"Buffalo MD-HUSB", NetmdCap_Portable, 0x0411, 0x0083},
    {"Kenwood MDX-J9", NetmdCap_Portable, 0x0B28, 0x1004},
    // Sony and its OEMs.
    {"Sony PCLK-XX", NetmdCap_Portable, 0x054C, 0x0034},
    {"Sony NetMD", NetmdCap_Portable, 0x054C, 0x0036},
    {"Sony MZ-N1", NetmdCap_Portable, 0x054C, 0x0075},
    {"Sony NetMD", NetmdCap_Portable, 0x054C, 0x007C},
    {"Sony LAM-1", NetmdCap_Deck, 0x054C, 0x0080},
    {"Sony MDS-JB980 / JE780", NetmdCap_Deck | NetmdCap_MonoUpload, 0x054C, 0x0081},
    {"Sony MZ-N505", NetmdCap_Portable, 0x054C, 0x0084},
    {"Sony MZ-S1", NetmdCap_Portable, 0x054C, 0x0085},
    {"Sony MZ-N707", NetmdCap_Portable, 0x054C, 0x0086},
    {"Sony CMT-C7NT", NetmdCap_Deck, 0x054C, 0x008E},
    {"Sony PCGA-MDN1", NetmdCap_Portable, 0x054C, 0x0097},
    {"Sony CMT-L7HD", NetmdCap_Deck, 0x054C, 0x00AD},
    {"Sony MZ-N10", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00C6},
    {"Sony MZ-N910", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00C7},
    {"Sony MZ-N710 / NF810", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00C8},
    {"Sony MZ-N510 / N610", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00C9},
    {"Sony MZ-NE410 / NF520D", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00CA},
    {"Sony CMT-M333NT / M373NT", NetmdCap_Deck, 0x054C, 0x00E7},
    {"Sony MZ-NE810 / NE910", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x00EB},
    {"Sony LAM", NetmdCap_Deck, 0x054C, 0x0101},
    {"Aiwa AM-NX1", NetmdCap_Portable, 0x054C, 0x0113},
    {"Sony CMT-SE7", NetmdCap_Deck, 0x054C, 0x011A},
    {"Sony MDS-S500", NetmdCap_Deck, 0x054C, 0x013F},
    {"Sony MDS-A1", NetmdCap_Deck, 0x054C, 0x0148},
    {"Aiwa AM-NX9", NetmdCap_Portable, 0x054C, 0x014C},
    {"Sony MZ-NH1", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x017E},
    {"Sony MZ-NH3D", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0180},
    {"Sony MZ-NH900", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0182},
    {"Sony MZ-NH700 / NH800", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0184},
    {"Sony MZ-NH600", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0186},
    {"Sony MZ-NH600D", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0187},
    {"Sony MZ-N920", NetmdCap_Portable | NetmdCap_TypeS, 0x054C, 0x0188},
    {"Sony LAM-3", NetmdCap_Deck, 0x054C, 0x018A},
    {"Sony MZ-DH10P", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x01E9},
    {"Sony MZ-RH10", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x0219},
    {"Sony MZ-RH710 / RH910", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD, 0x054C, 0x021B},
    {"Sony CMT-AH10", NetmdCap_Deck | NetmdCap_HiMD, 0x054C, 0x021D},
    {"Sony CMT-AH10", NetmdCap_Deck | NetmdCap_HiMD, 0x054C, 0x022C},
    {"Sony DS-HMD1", NetmdCap_Portable | NetmdCap_HiMD, 0x054C, 0x023C},
    // The one machine that can hand a recording back to the PC.
    {"Sony MZ-RH1 / M200", NetmdCap_Portable | NetmdCap_TypeS | NetmdCap_HiMD | NetmdCap_Upload, 0x054C, 0x0286},
};

const NetmdModel *netmd_model_lookup(u16 vid, u16 pid) {
    for (u64 i = 0; i < ArrayCount(netmd_models); i += 1) {
        if (netmd_models[i].vid == vid && netmd_models[i].pid == pid) { return &netmd_models[i]; }
    }
    return 0;
}

u64 netmd_model_count(void) { return ArrayCount(netmd_models); }
