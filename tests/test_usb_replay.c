// test_usb_replay.c - T-020: the PID table, the replay transport and the round
// trip through the device thread. Not one byte of USB is involved: that is the
// whole point of UsbTransport (ADR-008).

// The ping as netmd_device.c emits it (research/01 s2.5-2.7): one poll before
// the command, the getDiscFlags frame, a poll that says "not yet", a poll that
// says nine bytes are ready, then the read.
static String8 test_usb_ping_trace(void) {
    return str8_lit(
            "# poll before sending: nothing is pending, we may talk\n"
            "> c1 01 00 00 00 00 04 00\n"
            "< 00 81 00 00\n"
            "\n"
            "# getDiscFlags, a status query that changes nothing\n"
            "> 41 80 00 00 00 00 0d 00 00 18 06 01 10 10 00 ff 00 00 01 00 0b\n"
            "\n"
            "# not ready yet: buf[0] == 0 is normal, not an error\n"
            "> c1 01 00 00 00 00 04 00\n"
            "< 00 81 00 00\n"
            "# nine bytes, to be read with the request buf[1] names\n"
            "> c1 01 00 00 00 00 04 00\n"
            "< 01 81 09 00\n"
            "> c1 81 00 00 00 00 09 00\n"
            "< 09 18 06 01 10 10 00 00 0b\n");
}

TEST(usb_models) {
    Unused(arena);
    const NetmdModel *n505 = netmd_model_lookup(0x054C, 0x0084);
    EXPECT(n505 != 0);
    EXPECT(str8_eq(str8_cstr(n505->name), str8_lit("Sony MZ-N505")));
    EXPECT((n505->caps & NetmdCap_Portable) != 0);
    // A Type-R portable neither uploads nor speaks Hi-MD.
    EXPECT((n505->caps & NetmdCap_Upload) == 0);
    EXPECT((n505->caps & NetmdCap_HiMD) == 0);

    // The one machine that hands a recording back, and a deck.
    const NetmdModel *rh1 = netmd_model_lookup(0x054C, 0x0286);
    EXPECT(rh1 != 0 && (rh1->caps & NetmdCap_Upload) != 0);
    const NetmdModel *deck = netmd_model_lookup(0x054C, 0x0081);
    EXPECT(deck != 0 && (deck->caps & NetmdCap_Deck) != 0);
    // Another vendor entirely, and the Sharp rename quirk lives behind it.
    EXPECT(netmd_model_lookup(0x04DD, 0x9014) != 0);

    // The table *is* the filter: a mouse is not a NetMD, and neither is a Sony
    // device whose PID we have never seen.
    EXPECT(netmd_model_lookup(0x046D, 0xC077) == 0);
    EXPECT(netmd_model_lookup(0x054C, 0x9999) == 0);
    EXPECT(netmd_model_known(0x054C, 0x0084));
    EXPECT(netmd_model_count() > 40);
    EXPECT(str8_eq(netmd_model_name(0x054C, 0x0084), str8_lit("Sony MZ-N505")));
    EXPECT(netmd_model_name(0x054C, 0x9999).size == 0);
}

TEST(usb_replay_answers) {
    Unused(arena);
    NetmdReplay replay;
    netmd_replay_init(&replay, test_usb_ping_trace());
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    u8 poll[4];
    mem_set(poll, 0xEE, sizeof(poll));
    i32 got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, poll, 4, 1000);
    EXPECT(got == 4);
    EXPECT(poll[0] == 0x00 && poll[1] == 0x81 && poll[2] == 0x00);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(!netmd_replay_done(&replay));  // the transcript has more to say

    // A host to device transfer: nothing comes back, the byte count does.
    u8 frame[13] = {0x00, 0x18, 0x06, 0x01, 0x10, 0x10, 0x00,
                    0xFF, 0x00, 0x00, 0x01, 0x00, 0x0B};
    got = transport.control(transport.user, NETMD_RT_OUT, NETMD_REQ_SEND, 0, 0, frame,
                            sizeof(frame), 1000);
    EXPECT(got == 13);
    EXPECT(netmd_replay_ok(&replay));
}

TEST(usb_replay_divergence) {
    Unused(arena);
    NetmdReplay replay;
    netmd_replay_init(&replay, test_usb_ping_trace());
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    // The read request instead of the poll: the same shape, one byte apart.
    u8 buffer[4];
    i32 got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_READ, 0, 0, buffer, 4,
                                1000);
    EXPECT(got == OsUsbError_Divergence);
    EXPECT(!netmd_replay_ok(&replay));
    EXPECT(replay.fail == NetmdReplayFail_Diverged);
    EXPECT(replay.expected_size == 8 && replay.actual_size == 8);
    EXPECT(replay.expected[1] == NETMD_REQ_POLL && replay.actual[1] == NETMD_REQ_READ);
    EXPECT(!netmd_replay_done(&replay));

    // And it stays failed: replaying an answer to a request that already drifted
    // is exactly what a replay transport must never do.
    got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, buffer, 4, 1000);
    EXPECT(got == OsUsbError_Divergence);
    EXPECT(replay.exchanges == 0);
}

TEST(usb_replay_timeout_and_end) {
    Unused(arena);
    NetmdReplay replay;
    netmd_replay_init(&replay,
                      str8_lit("# the device never answers this one\n"
                               "! timeout\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 04 00\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    u8 poll[4];
    mem_set(poll, 0xEE, sizeof(poll));
    i32 got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, poll, 4, 1000);
    EXPECT(got == OsUsbError_Timeout);
    // A timeout is a device that stayed silent, not a script that broke: the
    // request was the right one, so the replay carries on.
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(poll[0] == 0xEE);  // nothing was written back

    got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, poll, 4, 1000);
    EXPECT(got == 4 && poll[2] == 0x04);
    EXPECT(netmd_replay_done(&replay));

    // One request past the end of the transcript is a divergence too.
    got = transport.control(transport.user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, poll, 4, 1000);
    EXPECT(got == OsUsbError_Divergence);
    EXPECT(replay.fail == NetmdReplayFail_Exhausted);
}

// The whole point of the ticket: Enumerate, Open, Ping, Close through the
// device thread, with the transcript standing in for the MZ-N505.
TEST(usb_device_round_trip) {
    NetmdReplay replay;
    netmd_replay_init(&replay, test_usb_ping_trace());
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    NetmdDevice device;
    StructZero(&device);
    netmd_device_set_test_transport(&device, &transport, 0x054C, 0x0084);
    netmd_device_start(&device, arena);

    NetmdEvent event;
    EXPECT(netmd_device_post(&device, NetmdCmd_Enumerate, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Devices);
    EXPECT(event.device_count == 1);
    EXPECT(event.vid == 0x054C && event.pid == 0x0084);
    EXPECT(event.state == OsUsbState_Ready);
    EXPECT(str8_eq(str8(event.name, event.name_size), str8_lit("Sony MZ-N505")));

    EXPECT(netmd_device_post(&device, NetmdCmd_Open, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Opened);

    EXPECT(netmd_device_post(&device, NetmdCmd_Ping, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Pong);
    EXPECT(event.status == NetmdStatus_Accepted);  // 0x09, the AV/C value
    EXPECT(event.issued_us != 0);

    EXPECT(netmd_device_post(&device, NetmdCmd_Close, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Closed);

    netmd_device_stop(&device);
    // The device thread emitted exactly the session that was recorded.
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(netmd_replay_done(&replay));
    EXPECT(replay.exchanges == 5);
}

// An open on an index the enumeration never published is a domain error, not a
// crash: the panel shows it and life goes on.
TEST(usb_device_open_out_of_range) {
    NetmdReplay replay;
    netmd_replay_init(&replay, str8_lit("# nothing is ever sent here\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    NetmdDevice device;
    StructZero(&device);
    netmd_device_set_test_transport(&device, &transport, 0x054C, 0x0084);
    netmd_device_start(&device, arena);

    NetmdEvent event;
    EXPECT(netmd_device_post(&device, NetmdCmd_Open, 3));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Error);
    EXPECT(event.error == OsUsbError_NotOpen);

    // A ping with nothing open answers the same way, and never touches USB.
    EXPECT(netmd_device_post(&device, NetmdCmd_Ping, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Error);
    netmd_device_stop(&device);
    EXPECT(replay.exchanges == 0);
}

// A hotplug notification: the thread debounces it and re-enumerates by itself,
// which is what makes the panel follow the cable with no user action.
TEST(usb_device_hotplug_debounce) {
    NetmdReplay replay;
    netmd_replay_init(&replay, str8_lit("# no traffic: enumeration only\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    NetmdDevice device;
    StructZero(&device);
    netmd_device_set_test_transport(&device, &transport, 0x054C, 0x0084);
    netmd_device_start(&device, arena);

    u64 start = os_time_now_us();
    // The burst Windows really sends for one plug: the node, then its
    // interfaces. One enumeration must come out of it.
    for (u32 i = 0; i < 4; i += 1) {
        EXPECT(netmd_device_post(&device, NetmdCmd_DeviceChanged, 0));
    }
    NetmdEvent event;
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Devices && event.device_count == 1);
    u64 elapsed_us = os_time_now_us() - start;
    EXPECT(elapsed_us >= NETMD_HOTPLUG_DEBOUNCE_US);  // it did wait out the burst
    EXPECT(elapsed_us < 500000);                      // and the panel follows in time

    // The three other notifications were swallowed, not queued: no second
    // enumeration follows.
    EXPECT(!netmd_device_wait_event(&device, &event, 300000));
    netmd_device_stop(&device);
}

// The real bus, on whatever machine runs the tests: no device is required, but
// whatever is enumerated has to be coherent, and a NetMD among it gets named in
// the output - that line is the record of the on-device validation.
TEST(usb_enumerate_is_coherent) {
    OsUsbDeviceList list = os_usb_enumerate(arena);
    for (u64 i = 0; i < list.count; i += 1) {
        OsUsbDeviceInfo *info = &list.items[i];
        EXPECT(info->vid != 0);
        EXPECT(info->state < OsUsbState_COUNT);
        // Ready means "os_usb_open has something to open".
        EXPECT(info->state != OsUsbState_Ready || info->path.size != 0);
        EXPECT(info->state != OsUsbState_NoDriver || info->path.size == 0);
        const NetmdModel *model = netmd_model_lookup(info->vid, info->pid);
        if (!model) { continue; }
        test_report("  netmd %04x:%04x %s state=%u problem=%u bus=\"%S\"\n", info->vid, info->pid,
                    model->name, info->state, info->problem_code, info->bus_name);
    }
}

static void test_usb_replay_run_all(void) {
    RUN(usb_models);
    RUN(usb_replay_answers);
    RUN(usb_replay_divergence);
    RUN(usb_replay_timeout_and_end);
    RUN(usb_device_round_trip);
    RUN(usb_device_open_out_of_range);
    RUN(usb_device_hotplug_debounce);
    RUN(usb_enumerate_is_coherent);
}
