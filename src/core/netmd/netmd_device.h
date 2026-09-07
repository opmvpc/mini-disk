// netmd_device.h - the device thread and the two queues around it (ADR-008).
//
// Every USB call happens on this thread and nowhere else: a control transfer
// blocks for as long as the device feels like, and the UI must never be the
// one waiting. The main thread posts commands, drains results once per frame,
// and that is the whole contract - the same shape as lib_events (T-010).
#ifndef NETMD_DEVICE_H
#define NETMD_DEVICE_H

#include "../../base/base.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"
#include "netmd_models.h"
#include "netmd_transport.h"

// A NetMD command, posted by the main thread.
typedef enum NetmdCmdKind {
    NetmdCmd_None = 0,
    NetmdCmd_Enumerate,
    NetmdCmd_DeviceChanged,  // hotplug: debounced, then one enumeration
    NetmdCmd_Open,           // `device`: index into the last published list
    NetmdCmd_Ping,           // proves the handle talks (research/01 s2.5-2.7)
    NetmdCmd_Close,
    NetmdCmd_Quit,
    // ReadDisc and the rest of the protocol arrive with T-021.
    NetmdCmd_COUNT
} NetmdCmdKind;

typedef struct NetmdCmd {
    u32 kind;
    u32 device;
    u64 issued_us;  // when it was posted: what measures the hotplug latency
} NetmdCmd;

typedef enum NetmdEventKind {
    NetmdEvent_None = 0,
    NetmdEvent_Devices,  // the device list was refreshed
    NetmdEvent_Opened,
    NetmdEvent_Closed,
    NetmdEvent_Pong,
    NetmdEvent_Error,
    NetmdEvent_COUNT
} NetmdEventKind;

// Names travel by value: a String8 into the device thread's arena would be a
// pointer the UI could read after the arena moved on.
#define NETMD_NAME_MAX 64
#define NETMD_PATH_MAX 256
#define NETMD_DEVICE_MAX 8
#define NETMD_CMD_CAPACITY 32    // power of two
#define NETMD_EVENT_CAPACITY 64  // power of two

// What one command answers with. Fixed size, so both queues are plain rings.
typedef struct NetmdEvent {
    u32 kind;
    u32 state;         // OsUsbState of the device the event is about
    u16 vid;
    u16 pid;
    i32 error;         // OsUsbError on Error, 0 otherwise
    u32 status;        // Pong: the AV/C status byte the device answered with
    u32 device_count;  // NetMD devices the last enumeration found
    u32 problem_code;  // CM_PROB_*, 28 when the driver is missing (P-001)
    u32 name_size;
    u8 name[NETMD_NAME_MAX];  // model name, or what the bus calls it
    u64 issued_us;            // the command's own timestamp
    u64 timestamp_us;
} NetmdEvent;

// One NetMD device as the thread knows it.
typedef struct NetmdDeviceInfo {
    u16 vid;
    u16 pid;
    u32 state;  // OsUsbState
    u32 problem_code;
    u32 name_size;
    u32 path_size;
    u8 name[NETMD_NAME_MAX];
    u8 path[NETMD_PATH_MAX];
} NetmdDeviceInfo;

typedef struct NetmdDevice {
    // --- shared ------------------------------------------------------------
    NetmdCmd commands[NETMD_CMD_CAPACITY];
    volatile u32 cmd_write;  // the main thread only
    volatile u32 cmd_read;   // the device thread only
    NetmdEvent events[NETMD_EVENT_CAPACITY];
    volatile u32 event_write;  // the device thread only
    volatile u32 event_read;   // the main thread only
    volatile u32 running;
    OsSemaphore wake;
    OsThread thread;
    b32 started;

    // --- the device thread's own -------------------------------------------
    Arena *arena;
    NetmdDeviceInfo devices[NETMD_DEVICE_MAX];
    u32 device_count;
    OsUsb usb;
    UsbTransport transport;
    b32 open;
    u32 open_index;

    // --- tests --------------------------------------------------------------
    // A transport set before the thread starts replaces WinUSB entirely: the
    // enumeration answers with `test_vid`/`test_pid` and the open binds this.
    const UsbTransport *test_transport;
    u16 test_vid;
    u16 test_pid;
} NetmdDevice;

// The debounce of the hotplug: Windows sends a burst of WM_DEVICECHANGE for one
// plug (the node, then each interface), and re-enumerating on each of them
// would walk the bus five times for nothing.
#define NETMD_HOTPLUG_DEBOUNCE_US 200000

void netmd_device_start(NetmdDevice *device, Arena *arena);
void netmd_device_stop(NetmdDevice *device);  // posts Quit and joins

// Posts a command; 0 when the queue is full, which means the device thread is
// wedged on a transfer and one more Enumerate would not help anyway.
b32 netmd_device_post(NetmdDevice *device, u32 kind, u32 device_index);
b32 netmd_device_next_event(NetmdDevice *device, NetmdEvent *out);
// Blocks the *calling* thread until one event shows up. For the tests, and for
// nothing else: the UI drains the queue once per frame and never waits.
b32 netmd_device_wait_event(NetmdDevice *device, NetmdEvent *out, u64 timeout_us);

// Both must be called before netmd_device_start.
void netmd_device_set_test_transport(NetmdDevice *device, const UsbTransport *transport, u16 vid,
                                     u16 pid);

#endif  // NETMD_DEVICE_H
