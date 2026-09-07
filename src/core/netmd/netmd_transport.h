// netmd_transport.h - the three calls the NetMD protocol needs from a bus, and
// nothing else (ADR-008). Two implementations exist: WinUSB, bound in
// platform/win32/win32_usb.c, and the transcript replay of netmd_replay.c which
// the tests run against with no device plugged in.
//
// The struct is function pointers rather than a compile time switch on purpose:
// the same protocol code runs against both, so a test failure means a protocol
// bug and never a build variant.
#ifndef NETMD_TRANSPORT_H
#define NETMD_TRANSPORT_H

#include "../../base/base.h"
#include "../../platform/platform.h"

// The three vendor requests of the command channel (research/01 s2.4). Every
// NetMD command travels on EP0; the bulk pipes only carry audio.
#define NETMD_REQ_POLL 0x01u  // IN, 4 bytes: is a reply waiting, and how long
#define NETMD_REQ_SEND 0x80u  // OUT: the command frame
#define NETMD_REQ_READ 0x81u  // IN: the reply frame - but poll[1] is what says so

// bmRequestType: vendor, recipient interface, one bit of direction.
#define NETMD_RT_OUT 0x41u
#define NETMD_RT_IN  0xC1u

#define NETMD_EP_BULK_IN  0x01u
#define NETMD_EP_BULK_OUT 0x02u

// A reply length travels in one byte of the poll answer, so no reply can be
// longer than this - it is why long titles are read page by page (T-021).
#define NETMD_REPLY_MAX 255

// AV/C status bytes, first byte of a reply (research/01 s3.2). The values of
// netmd-js are wrong and are not what these are.
typedef enum NetmdStatus {
    NetmdStatus_NotImplemented = 0x08,
    NetmdStatus_Accepted = 0x09,
    NetmdStatus_Rejected = 0x0A,
    NetmdStatus_InTransition = 0x0B,
    NetmdStatus_Implemented = 0x0C,
    NetmdStatus_Changed = 0x0D,
    NetmdStatus_Interim = 0x0F,
} NetmdStatus;

// Byte counts on success, an OsUsbError on failure: the protocol layer tests
// `< 0` and nothing else.
typedef struct UsbTransport {
    i32 (*control)(void *user, u8 request_type, u8 request, u16 value, u16 index, void *buffer,
                   u32 length, u32 timeout_ms);
    i32 (*bulk_write)(void *user, u8 endpoint, const void *data, u32 length, u32 timeout_ms);
    i32 (*bulk_read)(void *user, u8 endpoint, void *data, u32 length, u32 timeout_ms);
    void *user;
} UsbTransport;

// The WinUSB binding, implemented in platform/win32/win32_usb.c.
void os_usb_transport(OsUsb usb, UsbTransport *out);

md_inline b32 netmd_transport_bound(const UsbTransport *transport) {
    return transport->control != 0;
}

#endif  // NETMD_TRANSPORT_H
