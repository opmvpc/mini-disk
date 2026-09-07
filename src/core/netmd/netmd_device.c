// netmd_device.c - the device thread. See netmd_device.h for the contract.
#include "netmd_device.h"

// getDiscFlags: a STATUS query that reads one byte and changes nothing, which
// is exactly what a liveness probe should be (research/01 s3.7.1). The budgets
// and the backoff live in netmd_proto.h now.
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

// Proves the handle really talks to a NetMD: one status query through the same
// exchange every other command uses, so the ping tests the code that matters
// and not a second copy of it. Returns the AV/C status byte, or an OsUsbError.
static i32 netmd_ping(NetmdDevice *device) {
    ArenaTemp scratch = arena_temp_begin(device->arena);
    String8 request = str8((u8 *)netmd_ping_frame, sizeof(netmd_ping_frame));
    String8 reply;
    u32 result = netmd_exchange(&device->session, device->arena, request, NETMD_BUDGET_QUERY_MS,
                                &reply);
    i32 answer;
    if (result == NetmdResult_Usb) {
        answer = device->session.usb_error;
    } else if (result == NetmdResult_Timeout) {
        answer = OsUsbError_Timeout;
    } else if (result == NetmdResult_Malformed) {
        answer = OsUsbError_Failed;
    } else {
        answer = (i32)device->session.last_status;
    }
    arena_temp_end(scratch);
    return answer;
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

// The transport the protocol actually talks to: the real one, or the real one
// wrapped in the trace writer when --netmd-trace is on.
static void netmd_device_bind(NetmdDevice *device, const UsbTransport *transport) {
    if (device->trace_path_size == 0) {
        mem_copy(&device->transport, transport, sizeof(UsbTransport));
    } else {
        // Its own arena: the trace grows for the whole session, while the
        // device arena is rewound after every command.
        netmd_trace_init(&device->trace, device->trace_arena, transport);
        netmd_trace_transport(&device->trace, &device->transport);
    }
    NetmdDeviceInfo *info = &device->devices[device->open_index];
    netmd_session_init(&device->session, &device->transport, info->vid, info->pid);
}

static void netmd_device_close(NetmdDevice *device, const NetmdCmd *cmd) {
    // A capture is only worth having complete: it is written when the session
    // ends, whichever way it ends.
    if (device->trace.bound && device->trace_path_size != 0) {
        netmd_trace_write(&device->trace, str8(device->trace_path, device->trace_path_size));
        StructZero(&device->trace);
    }
    if (device->open && !device->test_transport) { os_usb_close(device->usb); }
    device->open = 0;
    device->open_index = 0;
    device->usb.v = 0;
    os_atomic_store_u32(&device->disc_valid, 0);
    StructZero(&device->session);
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
        device->open_index = cmd->device;
        netmd_device_bind(device, device->test_transport);
        device->open = 1;
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
    UsbTransport bus;
    os_usb_transport(device->usb, &bus);
    device->open_index = cmd->device;
    netmd_device_bind(device, &bus);
    device->open = 1;
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
    i32 result = netmd_ping(device);
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

// --- the disc ----------------------------------------------------------------

const DiscLayout *netmd_device_disc(const NetmdDevice *device) {
    if (!os_atomic_load_u32((volatile u32 *)&device->disc_valid)) { return 0; }
    u32 slot = os_atomic_load_u32((volatile u32 *)&device->disc_slot);
    return device->disc[slot & 1u];
}

// Reads the whole disc into the slot the UI is not looking at, then publishes
// it with one store. Nothing is ever half published, and the UI never waits.
static void netmd_device_read_disc(NetmdDevice *device, const NetmdCmd *cmd) {
    NetmdEvent event = netmd_event_make(device, NetmdEvent_Disc, cmd);
    if (device->open_index < device->device_count) {
        netmd_event_fill_device(&event, &device->devices[device->open_index]);
    }
    if (!device->open || !netmd_transport_bound(&device->transport)) {
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_NotOpen;
        netmd_event_push(device, &event);
        return;
    }

    u32 slot = (os_atomic_load_u32(&device->disc_slot) + 1u) & 1u;
    DiscLayout *layout = device->disc[slot];
    u64 started_us = os_time_now_us();
    ArenaTemp scratch = arena_temp_begin(device->arena);
    u32 result = netmd_read_disc(&device->session, device->arena, layout);
    arena_temp_end(scratch);
    event.elapsed_ms = (u32)((os_time_now_us() - started_us) / 1000u);
    event.result = result;

    if (result == NetmdResult_Usb) {
        event.kind = NetmdEvent_Error;
        event.error = device->session.usb_error;
        if (device->session.usb_error == OsUsbError_Disconnected) {
            netmd_device_close(device, 0);
        }
        netmd_event_push(device, &event);
        return;
    }
    if (result != NetmdResult_Ok) {
        // A disc that will not describe itself is a domain answer: the panel
        // says so and the session stays up.
        event.disc_flags = 0;
        netmd_event_push(device, &event);
        return;
    }
    os_atomic_store_u32(&device->disc_slot, slot);
    os_atomic_store_u32(&device->disc_valid, 1);
    event.disc_flags = layout->flags;
    event.track_count = layout->track_count;
    netmd_event_push(device, &event);
}

static void netmd_device_transport_cmd(NetmdDevice *device, const NetmdCmd *cmd) {
    NetmdEvent event = netmd_event_make(device, NetmdEvent_Transport, cmd);
    event.status = cmd->kind;
    if (!device->open || !netmd_transport_bound(&device->transport)) {
        event.kind = NetmdEvent_Error;
        event.error = OsUsbError_NotOpen;
        netmd_event_push(device, &event);
        return;
    }
    ArenaTemp scratch = arena_temp_begin(device->arena);
    u32 result = NetmdResult_NotImplemented;
    switch (cmd->kind) {
        case NetmdCmd_Play: result = netmd_play(&device->session, device->arena); break;
        case NetmdCmd_Pause: result = netmd_pause(&device->session, device->arena); break;
        case NetmdCmd_Stop: result = netmd_stop(&device->session, device->arena); break;
        case NetmdCmd_Next: result = netmd_next(&device->session, device->arena); break;
        case NetmdCmd_Prev: result = netmd_prev(&device->session, device->arena); break;
        case NetmdCmd_Eject: result = netmd_eject(&device->session, device->arena); break;
        default: break;
    }
    arena_temp_end(scratch);
    event.result = result;
    if (result == NetmdResult_Usb) {
        event.kind = NetmdEvent_Error;
        event.error = device->session.usb_error;
        if (device->session.usb_error == OsUsbError_Disconnected) {
            netmd_device_close(device, 0);
        }
    } else if (cmd->kind == NetmdCmd_Eject && result == NetmdResult_Ok) {
        // The TOC is flushed on ejection (s6.3): whatever we had is stale.
        os_atomic_store_u32(&device->disc_valid, 0);
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
    // A disc read across a hotplug is stale by definition: the bay may have
    // been opened while the cable was out (s6.3).
    if (!device->open) { os_atomic_store_u32(&device->disc_valid, 0); }
}

static void netmd_device_exec(NetmdDevice *device, const NetmdCmd *cmd) {
    switch (cmd->kind) {
        case NetmdCmd_Enumerate: netmd_device_enumerate(device, cmd); break;
        case NetmdCmd_DeviceChanged: netmd_device_hotplug(device, cmd); break;
        case NetmdCmd_Open: netmd_device_open(device, cmd); break;
        case NetmdCmd_Ping: netmd_device_ping(device, cmd); break;
        case NetmdCmd_Close: netmd_device_close(device, cmd); break;
        case NetmdCmd_ReadDisc: netmd_device_read_disc(device, cmd); break;
        case NetmdCmd_Play:
        case NetmdCmd_Pause:
        case NetmdCmd_Stop:
        case NetmdCmd_Next:
        case NetmdCmd_Prev:
        case NetmdCmd_Eject: netmd_device_transport_cmd(device, cmd); break;
        case NetmdCmd_Quit: os_atomic_store_u32(&device->running, 0); break;
        default: break;
    }
}

// A bay opened by hand produces no USB event: nothing is plugged or unplugged,
// the TOC simply changes underneath us. The only way to notice is to ask, so
// while a disc session is open the thread wakes every couple of seconds for one
// four byte status query and re-reads when the answer moved. The UI stays
// asleep throughout - only a real change pushes an event (ADR-004).
static void netmd_device_watch(NetmdDevice *device) {
    if (!device->open || !netmd_transport_bound(&device->transport)) { return; }
    ArenaTemp scratch = arena_temp_begin(device->arena);
    b32 present = 0;
    u32 result = netmd_get_disc_present(&device->session, device->arena, &present);
    arena_temp_end(scratch);
    if (result != NetmdResult_Ok) { return; }
    b32 had = os_atomic_load_u32(&device->disc_valid) &&
              (device->disc[os_atomic_load_u32(&device->disc_slot) & 1u]->flags &
               NetmdDiscFlag_Present) != 0;
    if (present == had) { return; }
    NetmdCmd reread;
    StructZero(&reread);
    reread.kind = NetmdCmd_ReadDisc;
    reread.issued_us = os_time_now_us();
    netmd_device_read_disc(device, &reread);
}

static void netmd_device_thread(void *data) {
    NetmdDevice *device = (NetmdDevice *)data;
    while (os_atomic_load_u32(&device->running)) {
        // Infinite while nothing is open: at rest this thread costs nothing.
        u64 timeout = (device->open && device->watch_us != 0) ? device->watch_us
                                                                    : OS_TIMEOUT_INFINITE;
        if (!os_semaphore_wait_for(device->wake, timeout)) {
            netmd_device_watch(device);
            continue;
        }
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
    // A transcript has no clock: a background status query would insert a
    // request the recording never saw. Tests that want the watch set watch_us.
    device->watch_us = 0;
}

void netmd_device_set_trace(NetmdDevice *device, String8 path) {
    Assert(!device->started);
    device->trace_path_size = (u32)Min(path.size, (u64)NETMD_PATH_MAX);
    if (device->trace_path_size != 0) {
        mem_copy(device->trace_path, path.str, device->trace_path_size);
    }
}

void netmd_device_start(NetmdDevice *device, Arena *arena) {
    device->arena = arena;
    if (!device->test_transport) { device->watch_us = NETMD_DISC_WATCH_US; }
    // The two disc buffers come off the arena rather than out of NetmdDevice:
    // 140 KB inside a struct a test declares on its stack is a stack overflow
    // waiting for the first machine with a smaller default.
    device->disc[0] = push_struct_zero(arena, DiscLayout);
    device->disc[1] = push_struct_zero(arena, DiscLayout);
    if (device->trace_path_size != 0) { device->trace_arena = arena_alloc(MB(16)); }
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
