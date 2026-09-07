// netmd_device.c - the device thread. See netmd_device.h for the contract.
#include "netmd_device.h"

// Budgets of research/01 s2.6: one second per transfer, three for a whole
// interrogation command. The ping is the cheapest of them.
#define NETMD_TRANSFER_TIMEOUT_MS 1000
#define NETMD_PING_BUDGET_MS 3000
#define NETMD_POLL_DELAY_MIN_MS 5
#define NETMD_POLL_DELAY_MAX_MS 200

// getDiscFlags: a STATUS query that reads one byte and changes nothing, which
// is exactly what a liveness probe should be (research/01 s3.7.1).
global const u8 netmd_ping_frame[13] = {0x00, 0x18, 0x06, 0x01, 0x10, 0x10, 0x00,
                                        0xFF, 0x00, 0x00, 0x01, 0x00, 0x0B};

// --- the two rings ---------------------------------------------------------
// One producer and one consumer each, so a store of the index publishes the
// slot and nothing needs a lock (lib_events.h says the same thing at length).

static b32 netmd_cmd_push(NetmdDevice *device, const NetmdCmd *cmd) {
    u32 write = os_atomic_load_u32(&device->cmd_write);
    u32 read = os_atomic_load_u32(&device->cmd_read);
    if (write - read >= NETMD_CMD_CAPACITY) { return 0; }
    mem_copy(&device->commands[write & (NETMD_CMD_CAPACITY - 1)], cmd, sizeof(NetmdCmd));
    os_atomic_store_u32(&device->cmd_write, write + 1);
    return 1;
}

static b32 netmd_cmd_pop(NetmdDevice *device, NetmdCmd *out) {
    u32 read = os_atomic_load_u32(&device->cmd_read);
    if (read == os_atomic_load_u32(&device->cmd_write)) { return 0; }
    mem_copy(out, &device->commands[read & (NETMD_CMD_CAPACITY - 1)], sizeof(NetmdCmd));
    os_atomic_store_u32(&device->cmd_read, read + 1);
    return 1;
}

// The head of the queue without consuming it: what lets a burst of hotplug
// notifications collapse into one enumeration.
static b32 netmd_cmd_peek(NetmdDevice *device, NetmdCmd *out) {
    u32 read = os_atomic_load_u32(&device->cmd_read);
    if (read == os_atomic_load_u32(&device->cmd_write)) { return 0; }
    mem_copy(out, &device->commands[read & (NETMD_CMD_CAPACITY - 1)], sizeof(NetmdCmd));
    return 1;
}

static void netmd_event_push(NetmdDevice *device, const NetmdEvent *event) {
    u32 write = os_atomic_load_u32(&device->event_write);
    u32 read = os_atomic_load_u32(&device->event_read);
    // A full ring means the UI stopped draining; dropping the newest event is
    // the one thing that cannot stall the device thread.
    if (write - read >= NETMD_EVENT_CAPACITY) { return; }
    mem_copy(&device->events[write & (NETMD_EVENT_CAPACITY - 1)], event, sizeof(NetmdEvent));
    os_atomic_store_u32(&device->event_write, write + 1);
    os_request_redraw();  // the panel updates without the user touching anything
}

b32 netmd_device_next_event(NetmdDevice *device, NetmdEvent *out) {
    u32 read = os_atomic_load_u32(&device->event_read);
    if (read == os_atomic_load_u32(&device->event_write)) { return 0; }
    mem_copy(out, &device->events[read & (NETMD_EVENT_CAPACITY - 1)], sizeof(NetmdEvent));
    os_atomic_store_u32(&device->event_read, read + 1);
    return 1;
}

b32 netmd_device_wait_event(NetmdDevice *device, NetmdEvent *out, u64 timeout_us) {
    u64 deadline = os_time_now_us() + timeout_us;
    for (;;) {
        if (netmd_device_next_event(device, out)) { return 1; }
        if (os_time_now_us() >= deadline) { return 0; }
        os_sleep_us(500);
    }
}

// --- the protocol ping ------------------------------------------------------

static i32 netmd_poll(const UsbTransport *transport, u8 *out4) {
    return transport->control(transport->user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, out4, 4,
                              NETMD_TRANSFER_TIMEOUT_MS);
}

// Proves the handle really talks to a NetMD: drain whatever was left over, send
// one status query, wait for the answer with the capped backoff of s2.6, read
// it. Returns the AV/C status byte, or an OsUsbError.
static i32 netmd_ping(const UsbTransport *transport) {
    u8 poll[4];
    i32 result = netmd_poll(transport, poll);
    if (result < 0) { return result; }
    if (result < 4) { return OsUsbError_Failed; }
    // An orphan reply from a previous session: read it and drop it, or every
    // command after this one would answer the previous one (s2.5, pitfall 2).
    if (poll[2] != 0) {
        u8 orphan[NETMD_REPLY_MAX];
        result = transport->control(transport->user, NETMD_RT_IN, poll[1], 0, 0, orphan, poll[2],
                                    NETMD_TRANSFER_TIMEOUT_MS);
        if (result < 0) { return result; }
    }

    u8 frame[sizeof(netmd_ping_frame)];
    mem_copy(frame, netmd_ping_frame, sizeof(frame));
    result = transport->control(transport->user, NETMD_RT_OUT, NETMD_REQ_SEND, 0, 0, frame,
                                sizeof(frame), NETMD_TRANSFER_TIMEOUT_MS);
    if (result < 0) { return result; }

    u32 waited = 0;
    u32 delay = NETMD_POLL_DELAY_MIN_MS;
    for (;;) {
        result = netmd_poll(transport, poll);
        if (result < 0) { return result; }
        // poll[0] == 0 is not an error: it is "not ready yet" (pitfall 4).
        if (result >= 4 && poll[0] != 0) { break; }
        if (waited >= NETMD_PING_BUDGET_MS) { return OsUsbError_Timeout; }
        os_sleep_us((u64)delay * 1000);
        waited += delay;
        delay = (delay < NETMD_POLL_DELAY_MAX_MS) ? delay * 2 : NETMD_POLL_DELAY_MAX_MS;
    }
    if (poll[2] == 0) { return OsUsbError_Failed; }

    u8 reply[NETMD_REPLY_MAX];
    // poll[1] and not 0x81: the factory channel answers on another request and
    // hard coding the standard one breaks it (pitfall 3).
    result = transport->control(transport->user, NETMD_RT_IN, poll[1], 0, 0, reply, poll[2],
                                NETMD_TRANSFER_TIMEOUT_MS);
    if (result < 0) { return result; }
    if (result == 0) { return OsUsbError_Failed; }
    return (i32)reply[0];
}

// --- commands ---------------------------------------------------------------

static NetmdEvent netmd_event_make(const NetmdDevice *device, u32 kind, const NetmdCmd *cmd) {
    NetmdEvent event;
    StructZero(&event);
    event.kind = kind;
    event.device_count = device->device_count;
    event.issued_us = cmd ? cmd->issued_us : 0;
    event.timestamp_us = os_time_now_us();
    return event;
}

static void netmd_event_fill_device(NetmdEvent *event, const NetmdDeviceInfo *info) {
    event->vid = info->vid;
    event->pid = info->pid;
    event->state = info->state;
    event->problem_code = info->problem_code;
    event->name_size = Min(info->name_size, (u32)NETMD_NAME_MAX);
    mem_copy(event->name, info->name, event->name_size);
}

static void netmd_info_set_name(NetmdDeviceInfo *info, String8 name) {
    info->name_size = (u32)Min(name.size, (u64)NETMD_NAME_MAX);
    if (info->name_size != 0) { mem_copy(info->name, name.str, info->name_size); }
}

// The USB layer reports every device node it sees; which of them is a NetMD is
// this layer's business, and the PID table is the only reliable answer
// (research/01 s1.1: they all call themselves "Net MD Walkman").
static void netmd_device_enumerate(NetmdDevice *device, const NetmdCmd *cmd) {
    device->device_count = 0;
    if (device->test_transport) {
        NetmdDeviceInfo *info = &device->devices[0];
        StructZero(info);
        info->vid = device->test_vid;
        info->pid = device->test_pid;
        info->state = OsUsbState_Ready;
        netmd_info_set_name(info, netmd_model_name(info->vid, info->pid));
        device->device_count = 1;
    } else {
        ArenaTemp temp = arena_temp_begin(device->arena);
        OsUsbDeviceList list = os_usb_enumerate(device->arena);
        for (u64 i = 0; i < list.count && device->device_count < NETMD_DEVICE_MAX; i += 1) {
            OsUsbDeviceInfo *found = &list.items[i];
            const NetmdModel *model = netmd_model_lookup(found->vid, found->pid);
            if (!model) { continue; }
            NetmdDeviceInfo *info = &device->devices[device->device_count];
            StructZero(info);
            info->vid = found->vid;
            info->pid = found->pid;
            info->state = found->state;
            info->problem_code = found->problem_code;
            // The model name is the one thing the bus cannot tell us, so it
            // wins over the "Net MD Walkman" every portable answers.
            netmd_info_set_name(info, str8_cstr(model->name));
            info->path_size = (u32)Min(found->path.size, (u64)NETMD_PATH_MAX);
            if (info->path_size != 0) { mem_copy(info->path, found->path.str, info->path_size); }
            device->device_count += 1;
        }
        arena_temp_end(temp);
    }

    NetmdEvent event = netmd_event_make(device, NetmdEvent_Devices, cmd);
    event.device_count = device->device_count;
    if (device->device_count != 0) { netmd_event_fill_device(&event, &device->devices[0]); }
    netmd_event_push(device, &event);
}

static void netmd_device_close(NetmdDevice *device, const NetmdCmd *cmd) {
    if (device->open && !device->test_transport) { os_usb_close(device->usb); }
    device->open = 0;
    device->open_index = 0;
    device->usb.v = 0;
    StructZero(&device->transport);
    if (cmd) {
        NetmdEvent event = netmd_event_make(device, NetmdEvent_Closed, cmd);
        netmd_event_push(device, &event);
    }
}

static void netmd_device_open(NetmdDevice *device, const NetmdCmd *cmd) {
    if (device->open) { netmd_device_close(device, 0); }
    NetmdEvent event = netmd_event_make(device, NetmdEvent_Opened, cmd);
    if (cmd->device >= device->device_count) {
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_NotOpen;
        netmd_event_push(device, &event);
        return;
    }
    NetmdDeviceInfo *info = &device->devices[cmd->device];
    netmd_event_fill_device(&event, info);
    if (device->test_transport) {
        mem_copy(&device->transport, device->test_transport, sizeof(UsbTransport));
        device->open = 1;
        device->open_index = cmd->device;
        netmd_event_push(device, &event);
        return;
    }
    if (info->state != OsUsbState_Ready) {
        // No driver, or somebody else holds it: a domain answer, not a failure
        // of ours - the panel turns it into the Zadig screen (P-001).
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_NotOpen;
        netmd_event_push(device, &event);
        return;
    }
    device->usb = os_usb_open(str8(info->path, info->path_size));
    if (!os_usb_is_open(device->usb)) {
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_Failed;
        event.state = OsUsbState_InUse;
        netmd_event_push(device, &event);
        return;
    }
    os_usb_transport(device->usb, &device->transport);
    device->open = 1;
    device->open_index = cmd->device;
    netmd_event_push(device, &event);
}

static void netmd_device_ping(NetmdDevice *device, const NetmdCmd *cmd) {
    NetmdEvent event = netmd_event_make(device, NetmdEvent_Pong, cmd);
    if (device->open_index < device->device_count) {
        netmd_event_fill_device(&event, &device->devices[device->open_index]);
    }
    if (!device->open || !netmd_transport_bound(&device->transport)) {
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_NotOpen;
        netmd_event_push(device, &event);
        return;
    }
    i32 result = netmd_ping(&device->transport);
    if (result < 0) {
        event.kind = NetmdEvent_Error;
        event.error = result;
        // A device that went away mid transfer takes its handle with it.
        if (result == OsUsbError_Disconnected) { netmd_device_close(device, 0); }
    } else {
        event.status = (u32)result;
    }
    netmd_event_push(device, &event);
}

// Windows announces one plug several times (the node, then every interface it
// exposes). Wait out the burst, swallow what it left in the queue, walk the bus
// once: 200 ms of latency against five enumerations.
static void netmd_device_hotplug(NetmdDevice *device, const NetmdCmd *cmd) {
    os_sleep_us(NETMD_HOTPLUG_DEBOUNCE_US);
    for (;;) {
        NetmdCmd next;
        if (!netmd_cmd_peek(device, &next) || next.kind != NetmdCmd_DeviceChanged) { break; }
        netmd_cmd_pop(device, &next);
    }
    // A device that was open and is gone must not keep a dead handle around.
    netmd_device_enumerate(device, cmd);
    if (device->open && device->device_count == 0) { netmd_device_close(device, 0); }
}

static void netmd_device_exec(NetmdDevice *device, const NetmdCmd *cmd) {
    switch (cmd->kind) {
        case NetmdCmd_Enumerate: netmd_device_enumerate(device, cmd); break;
        case NetmdCmd_DeviceChanged: netmd_device_hotplug(device, cmd); break;
        case NetmdCmd_Open: netmd_device_open(device, cmd); break;
        case NetmdCmd_Ping: netmd_device_ping(device, cmd); break;
        case NetmdCmd_Close: netmd_device_close(device, cmd); break;
        case NetmdCmd_Quit: os_atomic_store_u32(&device->running, 0); break;
        default: break;
    }
}

static void netmd_device_thread(void *data) {
    NetmdDevice *device = (NetmdDevice *)data;
    while (os_atomic_load_u32(&device->running)) {
        os_semaphore_wait(device->wake);
        NetmdCmd cmd;
        while (os_atomic_load_u32(&device->running) && netmd_cmd_pop(device, &cmd)) {
            netmd_device_exec(device, &cmd);
        }
    }
    netmd_device_close(device, 0);
}

// --- the main thread's side -------------------------------------------------

void netmd_device_set_test_transport(NetmdDevice *device, const UsbTransport *transport, u16 vid,
                                     u16 pid) {
    Assert(!device->started);  // the thread would read it while it is written
    device->test_transport = transport;
    device->test_vid = vid;
    device->test_pid = pid;
}

void netmd_device_start(NetmdDevice *device, Arena *arena) {
    device->arena = arena;
    device->running = 1;
    device->wake = os_semaphore_create(0, NETMD_CMD_CAPACITY);
    device->thread = os_thread_create(netmd_device_thread, device, str8_lit("minidisk-device"));
    device->started = 1;
}

void netmd_device_stop(NetmdDevice *device) {
    if (!device->started) { return; }
    netmd_device_post(device, NetmdCmd_Quit, 0);
    os_thread_join(device->thread);
    os_semaphore_destroy(device->wake);
    device->started = 0;
}

b32 netmd_device_post(NetmdDevice *device, u32 kind, u32 device_index) {
    NetmdCmd cmd;
    StructZero(&cmd);
    cmd.kind = kind;
    cmd.device = device_index;
    cmd.issued_us = os_time_now_us();
    if (!netmd_cmd_push(device, &cmd)) { return 0; }
    os_semaphore_signal(device->wake, 1);
    return 1;
}
