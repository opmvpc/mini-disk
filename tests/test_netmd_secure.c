// test_netmd_secure.c - T-042: DES, the retail MAC, the key derivations, the
// packet framing, and three downloads replayed end to end.
//
// Every hex constant below comes from one of two places and never from the code
// under test: the published DES known-answer vectors and FIPS 81 appendix B for
// the cipher itself, and `python tools/netmd_crypto_check.py` - an independent
// pure-Python DES - for everything NetMD-specific. The ten known-answer vectors
// were also checked against Windows CNG (`--cng`), so three implementations
// agree before a single byte of protocol is exercised.

static void test_hex(const char *text, u8 *out, u64 count) {
    for (u64 i = 0; i < count; i += 1) {
        u8 hi = (u8)text[i * 2];
        u8 lo = (u8)text[i * 2 + 1];
        hi = (u8)((hi <= '9') ? hi - '0' : (hi | 0x20u) - 'a' + 10u);
        lo = (u8)((lo <= '9') ? lo - '0' : (lo | 0x20u) - 'a' + 10u);
        out[i] = (u8)((hi << 4) | lo);
    }
}

static b32 test_hex_eq(const u8 *bytes, const char *expected, u64 count) {
    u8 want[64];
    Assert(count <= sizeof(want));
    test_hex(expected, want, count);
    return mem_cmp(bytes, want, count) == 0;
}

// --- DES itself (FIPS 46-3) --------------------------------------------------

typedef struct TestDesVector {
    const char *key;
    const char *plain;
    const char *cipher;
} TestDesVector;

TEST(netmd_des_known_answers) {
    Unused(arena);
    // The classic NBS/NIST single-block set. All ten agree with Windows CNG.
    static const TestDesVector vectors[10] = {
        {"133457799bbccdff", "0123456789abcdef", "222e5ffa2c5c9d64"},
        {"0000000000000000", "0000000000000000", "8ca64de9c1b123a7"},
        {"ffffffffffffffff", "ffffffffffffffff", "7359b2163e4edc58"},
        {"3000000000000000", "1000000000000001", "958e6e627a05557b"},
        {"1111111111111111", "1111111111111111", "f40379ab9e0ec533"},
        {"0123456789abcdef", "1111111111111111", "17668dfc7292532d"},
        {"1111111111111111", "0123456789abcdef", "8a5ae1f81ab8f2dd"},
        {"fedcba9876543210", "0123456789abcdef", "ed39d950fa74bcc4"},
        {"7ca110454a1a6e57", "01a1d6d039776742", "690f5b0d9a26939b"},
        {"0131d9619dc1376e", "5cd54ca83def57da", "7a389d10354bd271"},
    };
    for (u32 i = 0; i < ArrayCount(vectors); i += 1) {
        u8 key[8], plain[8], cipher[8], back[8];
        test_hex(vectors[i].key, key, 8);
        test_hex(vectors[i].plain, plain, 8);
        DesKey schedule;
        des_key_init(&schedule, key);
        des_encrypt_block(&schedule, plain, cipher);
        EXPECT(test_hex_eq(cipher, vectors[i].cipher, 8));
        des_decrypt_block(&schedule, cipher, back);
        EXPECT(mem_cmp(back, plain, 8) == 0);
    }

    // The four weak keys are involutions - E(E(x)) == x - which no wrong key
    // schedule survives, and no S box content can fake.
    static const char *weak[4] = {"0101010101010101", "fefefefefefefefe", "e0e0e0e0f1f1f1f1",
                                  "1f1f1f1f0e0e0e0e"};
    for (u32 i = 0; i < ArrayCount(weak); i += 1) {
        u8 key[8], plain[8], once[8], twice[8];
        test_hex(weak[i], key, 8);
        test_hex("0123456789abcdef", plain, 8);
        DesKey schedule;
        des_key_init(&schedule, key);
        des_encrypt_block(&schedule, plain, once);
        des_encrypt_block(&schedule, once, twice);
        EXPECT(mem_cmp(twice, plain, 8) == 0);
    }
}

TEST(netmd_des_modes) {
    // FIPS 81 appendix B and C, verbatim: key 0123456789abcdef, IV
    // 1234567890abcdef, "Now is the time for all ".
    u8 key[8], iv[8];
    test_hex("0123456789abcdef", key, 8);
    test_hex("1234567890abcdef", iv, 8);
    const u8 *text = (const u8 *)"Now is the time for all ";
    u8 *out = push_array(arena, u8, 24);
    DesKey schedule;
    des_key_init(&schedule, key);
    des_ecb_encrypt(&schedule, text, out, 24);
    EXPECT(test_hex_eq(out, "3fa40e8a984d48156a271787ab8883f9893d51ec4b563b53", 24));
    u8 chain[8];
    mem_copy(chain, iv, 8);
    des_cbc_encrypt(&schedule, chain, text, out, 24);
    EXPECT(test_hex_eq(out, "e5c7cdde872bf27c43e934008c389c0f683788499a7c05f6", 24));
    // The IV is left on the last cipher block, which is what makes the packets
    // of s4.10 one continuous stream.
    EXPECT(mem_cmp(chain, out + 16, 8) == 0);

    mem_copy(chain, iv, 8);
    des_cbc_decrypt(&schedule, chain, out, out, 24);  // in place, as s4.11 does it
    EXPECT(mem_cmp(out, text, 24) == 0);

    // 3DES EDE with the EKB root key as K1 || K2.
    Des3Key ede;
    des3_key_init(&ede, netmd_ekb_open_source_root_key());
    u8 block[8], triple[8];
    test_hex("0011223344556677", block, 8);
    des3_encrypt_block(&ede, block, triple);
    EXPECT(test_hex_eq(triple, "23b102ef07b0c612", 8));
    des3_decrypt_block(&ede, triple, triple);
    EXPECT(mem_cmp(triple, block, 8) == 0);
}

TEST(netmd_retail_mac) {
    Unused(arena);
    const u8 *root = netmd_ekb_open_source_root_key();
    u8 value[24], mac[8];
    test_hex("000102030405060708090a0b0c0d0e0f", value, 16);
    des_retail_mac(root, value, 16, mac);
    EXPECT(test_hex_eq(mac, "4eaf5da7593ae4d6", 8));
    // 24 bytes exercises the DES-CBC half over more than one block, which the
    // 16 byte NetMD case degenerates away (s4.6).
    test_hex("00112233445566778899aabbccddeeff0123456789abcdef", value, 24);
    des_retail_mac(root, value, 24, mac);
    EXPECT(test_hex_eq(mac, "e82a639c8c0bddf4", 8));
}

// --- the NetMD derivations (s4.6, s4.7, s4.10, s4.12) ------------------------

TEST(netmd_secure_derivations) {
    const u8 *root = netmd_ekb_open_source_root_key();
    u8 nonces[16];
    test_hex("00112233445566778899aabbccddeeff", nonces, 16);
    u8 session_key[8];
    des_retail_mac(root, nonces, 16, session_key);
    EXPECT(test_hex_eq(session_key, "d1747712a78fe4b0", 8));

    DesKey session;
    des_key_init(&session, session_key);

    // s4.7: 01010101 || contentID || KEK, DES-CBC, IV zero.
    u8 plain[32];
    plain[0] = plain[1] = plain[2] = plain[3] = 0x01;
    mem_copy(plain + 4, netmd_content_id, 20);
    mem_copy(plain + 24, netmd_kek, 8);
    u8 *cipher = push_array(arena, u8, 32);
    u8 iv[8];
    mem_zero(iv, sizeof(iv));
    des_cbc_encrypt(&session, iv, plain, cipher, 32);
    EXPECT(test_hex_eq(cipher, "d252d4356b88b3f051407f3a4822e3e1", 16));
    EXPECT(test_hex_eq(cipher + 16, "f2a80e45d0beda792dfa5282fb7f01fc", 16));

    // s4.12: the commit authentication.
    u8 zero[8], auth[8];
    mem_zero(zero, sizeof(zero));
    des_encrypt_block(&session, zero, auth);
    EXPECT(test_hex_eq(auth, "e00939d8bebf47c0", 8));

    // s4.10: the data key is a *decryption* under the KEK, and the device gets
    // the raw key back by encrypting it.
    DesKey kek;
    des_key_init(&kek, netmd_kek);
    u8 raw_key[8], data_key[8], recovered[8];
    test_hex("0123456789abcdef", raw_key, 8);
    des_decrypt_block(&kek, raw_key, data_key);
    EXPECT(test_hex_eq(data_key, "8a862fcb2e6e2d81", 8));
    des_encrypt_block(&kek, data_key, recovered);
    EXPECT(mem_cmp(recovered, raw_key, 8) == 0);
}

// --- packet framing (s4.10, s4.11) -------------------------------------------

TEST(netmd_secure_packet_framing) {
    // Two SP frames of a ramp, the same bytes the trace generator uses.
    u64 size = 2 * DSP_SP_FRAME_BYTES;
    u8 *audio = push_array(arena, u8, size);
    for (u64 i = 0; i < size; i += 1) { audio[i] = (u8)((i * 7 + 3) & 0xFF); }

    u8 raw_key[8];
    test_hex("0123456789abcdef", raw_key, 8);
    DesKey key;
    des_key_init(&key, raw_key);
    u8 iv[8];
    mem_zero(iv, sizeof(iv));
    u8 *whole = push_array(arena, u8, size);
    des_cbc_encrypt(&key, iv, audio, whole, size);
    EXPECT(test_hex_eq(whole, "b508e8ede7b971b5", 8));
    EXPECT(test_hex_eq(whole + size - 8, "347ff71bea9305ce", 8));

    // The chaining rule: cutting the stream into two packets and carrying the
    // IV over must produce exactly the same bytes (s4.10). This is the bug that
    // would corrupt every track after the first megabyte and nothing else.
    u8 *split = push_array(arena, u8, size);
    mem_zero(iv, sizeof(iv));
    des_cbc_encrypt(&key, iv, audio, split, 1024);
    des_cbc_encrypt(&key, iv, audio + 1024, split + 1024, size - 1024);
    EXPECT(mem_cmp(split, whole, size) == 0);

    // s4.11: totalBytes counts the 24 byte header, frames counts the payload.
    u32 frame_size = netmd_secure_frame_size(NETMD_WIREFORMAT_PCM);
    EXPECT(frame_size == 2048);
    EXPECT(netmd_secure_frame_size(NETMD_WIREFORMAT_LP2) == 192);
    EXPECT(netmd_secure_frame_size(NETMD_WIREFORMAT_LP4) == 96);
    EXPECT(size / frame_size == 2);
    EXPECT(size + NETMD_PACKET_HEADER_BYTES == 4120);

    // s4.5: the EKB is chosen by leaf id, and only the erased deck gets the
    // other one.
    u8 leaf[8];
    mem_set(leaf, 0xFF, sizeof(leaf));
    EXPECT(netmd_ekb_select(leaf, 0x054C, 0x0081)->id == 0x13371337u);
    EXPECT(netmd_ekb_select(leaf, 0x054C, 0x0084)->id == 0x26422642u);
    leaf[3] = 0x00;
    EXPECT(netmd_ekb_select(leaf, 0x054C, 0x0081)->id == 0x26422642u);
}

TEST(netmd_title_encoding) {
    Unused(arena);
    u8 out[64];
    u64 size = netmd_sjis_from_utf8(str8_lit("Track A"), out, sizeof(out));
    EXPECT(size == 7 && mem_cmp(out, "Track A", 7) == 0);
    // Half-width katakana: U+FF76 U+FF9E, the voiced KA of s3.11, is two bytes
    // in the TOC and costs two characters of the budget.
    size = netmd_sjis_from_utf8(str8_lit("\xEF\xBD\xB6\xEF\xBE\x9E"), out, sizeof(out));
    EXPECT(size == 2 && out[0] == 0xB6 && out[1] == 0xDE);
    // Anything plan_toc_sanitize would have dropped is dropped rather than
    // written as a byte the device would show as noise.
    size = netmd_sjis_from_utf8(str8_lit("A\xC3\xA9""B"), out, sizeof(out));
    EXPECT(size == 2 && out[0] == 'A' && out[1] == 'B');
}

// --- the download, replayed ---------------------------------------------------

typedef struct TestUploadRandom {
    u32 calls;
} TestUploadRandom;

// The injection point that makes an encrypted transfer deterministic: the host
// nonce first, then the packet key. Both are the constants the trace generator
// used, so every encrypted byte on the wire is pinned.
static void test_upload_random(void *user, u8 *dst, u64 size) {
    TestUploadRandom *state = (TestUploadRandom *)user;
    Assert(size == 8);
    const char *value = (state->calls == 0) ? "0011223344556677" : "0123456789abcdef";
    test_hex(value, dst, 8);
    state->calls += 1;
}

typedef struct TestAudioBlock {
    const u8 *bytes;
    u64 size;
    u64 at;
} TestAudioBlock;

static u64 test_audio_read(void *user, u8 *dst, u64 size) {
    TestAudioBlock *block = (TestAudioBlock *)user;
    u64 take = Min(size, block->size - block->at);
    mem_copy(dst, block->bytes + block->at, take);
    block->at += take;
    return take;
}

typedef struct TestUploadRun {
    NetmdReplay replay;
    NetmdSession session;
    UsbTransport transport;
    NetmdUploadPlan plan;
    NetmdUploadState state;
    NetmdUploadEntry entries[2];
    TestAudioBlock blocks[2];
    TestUploadRandom random;
    u8 *audio;
    u32 progress_events;
    u32 track_done_events;
    u32 cancel_after;  // entries committed before the cancel flag goes up
} TestUploadRun;

static void test_upload_progress(void *user, const NetmdUploadPlan *plan,
                                 const NetmdUploadState *state, u32 event) {
    TestUploadRun *run = (TestUploadRun *)user;
    Unused(plan);
    Unused(state);
    if (event == NetmdUploadEvent_Progress) { run->progress_events += 1; }
    if (event == NetmdUploadEvent_TrackDone) {
        run->track_done_events += 1;
        // The cancellation the ticket asks for: between two tracks, from
        // another thread's point of view, with a transfer already committed.
        if (run->cancel_after != 0 && run->track_done_events == run->cancel_after) {
            os_atomic_store_u32(&run->state.cancel, 1);
        }
    }
}

static TestUploadRun *test_upload_prepare(Arena *arena, const char *trace, u32 entry_count) {
    TestUploadRun *run = push_struct_zero(arena, TestUploadRun);
    if (!netmd_replay_load(&run->replay, arena, str8_cstr(trace))) { return 0; }
    netmd_replay_transport(&run->replay, &run->transport);
    netmd_session_init(&run->session, &run->transport, 0x054C, 0x0084);

    u64 size = 2 * DSP_SP_FRAME_BYTES;
    run->audio = push_array(arena, u8, size);
    for (u64 i = 0; i < size; i += 1) { run->audio[i] = (u8)((i * 7 + 3) & 0xFF); }

    static const char *titles[2] = {"Test", "Second"};
    for (u32 i = 0; i < entry_count; i += 1) {
        NetmdUploadEntry *entry = &run->entries[i];
        run->blocks[i].bytes = run->audio;
        run->blocks[i].size = size;
        entry->data.read = test_audio_read;
        entry->data.user = &run->blocks[i];
        entry->data.total_bytes = size;
        entry->duration_ms = 24;  // two SP frames is 23.2 ms
        String8 title = str8_cstr(titles[i]);
        entry->title_size = (u32)title.size;
        mem_copy(entry->title, title.str, title.size);
    }
    run->plan.entries = run->entries;
    run->plan.count = entry_count;
    run->plan.random = test_upload_random;
    run->plan.random_user = &run->random;
    return run;
}

// On a divergence, the two frames side by side: which byte of which command
// differs is the whole diagnosis.
static void test_upload_report(TestUploadRun *run) {
    if (netmd_replay_ok(&run->replay)) { return; }
    test_report("    replay diverged at line %llu (fail %u)\n", run->replay.fail_line,
                run->replay.fail);
    ArenaTemp scratch = scratch_begin(0, 0);
    u64 count = Min(Max(run->replay.expected_size, run->replay.actual_size), (u64)48);
    u8 *line = push_array(scratch.arena, u8, count * 3 + 1);
    static const char digits[] = "0123456789abcdef";
    for (u32 side = 0; side < 2; side += 1) {
        const u8 *bytes = side ? run->replay.actual : run->replay.expected;
        u64 size = side ? run->replay.actual_size : run->replay.expected_size;
        for (u64 i = 0; i < count; i += 1) {
            line[i * 3] = (u8)((i < size) ? digits[bytes[i] >> 4] : '.');
            line[i * 3 + 1] = (u8)((i < size) ? digits[bytes[i] & 15] : '.');
            line[i * 3 + 2] = ' ';
        }
        test_report("      %s (%llu) %S\n", side ? "got " : "want", size,
                    str8(line, count * 3));
    }
    scratch_end(scratch);
}

TEST(netmd_upload_replay_sp) {
    TestUploadRun *run = test_upload_prepare(arena, "tests\\netmd\\mzn505_upload_sp.trace", 1);
    EXPECT(run != 0);
    if (!run) { return; }
    String8 disc_title = str8_lit("Demo");
    run->plan.write_disc_title = 1;
    run->plan.disc_title_size = (u32)disc_title.size;
    mem_copy(run->plan.disc_title, disc_title.str, disc_title.size);

    u32 result = netmd_upload_run(&run->session, arena, &run->plan, &run->state,
                                  test_upload_progress, run);
    EXPECT(result == NetmdResult_Ok);
    EXPECT(netmd_replay_ok(&run->replay));
    EXPECT(netmd_replay_done(&run->replay));
    EXPECT(run->entries[0].status == NetmdUploadStatus_Done);
    EXPECT(run->entries[0].track == 0);
    EXPECT(run->entries[0].bytes == 2 * DSP_SP_FRAME_BYTES);
    // The random source was asked twice and no more: one host nonce, one
    // packet key. A third call would mean a nonce got regenerated somewhere.
    EXPECT(run->random.calls == 2);
    EXPECT(run->track_done_events == 1);
    // The run is over: the flag the app checks before closing is down again.
    EXPECT(!netmd_upload_active(&run->state));
    EXPECT(run->state.done == 1);
    test_upload_report(run);
}

TEST(netmd_upload_replay_cancel) {
    TestUploadRun *run = test_upload_prepare(arena, "tests\\netmd\\mzn505_upload_cancel.trace",
                                             2);
    EXPECT(run != 0);
    if (!run) { return; }
    run->cancel_after = 1;

    u32 result = netmd_upload_run(&run->session, arena, &run->plan, &run->state,
                                  test_upload_progress, run);
    EXPECT(result == NetmdResult_Cancelled);
    // The whole transcript was played: the cancel did not skip the teardown,
    // it went through it. That is the property the ticket asks for - a
    // cancelled burn leaves the device in the state the next one can enter.
    EXPECT(netmd_replay_ok(&run->replay));
    EXPECT(netmd_replay_done(&run->replay));
    // The committed track stays committed. Erasing it to "clean up" would
    // destroy a track the device really wrote.
    EXPECT(run->entries[0].status == NetmdUploadStatus_Done);
    EXPECT(run->entries[1].status == NetmdUploadStatus_Cancelled);
    EXPECT(run->state.done == 1);
    EXPECT(!netmd_upload_active(&run->state));
    test_upload_report(run);
}

TEST(netmd_upload_replay_resume) {
    // The resume: the same entry array, with the first one already Done. The
    // transcript holds one download and no more, so a run that re-sent the
    // first track would diverge on its very first frame.
    TestUploadRun *run = test_upload_prepare(arena, "tests\\netmd\\mzn505_upload_resume.trace",
                                             2);
    EXPECT(run != 0);
    if (!run) { return; }
    run->entries[0].status = NetmdUploadStatus_Done;
    run->entries[0].track = 0;
    // The second entry is the one the transcript downloads, and it lands as
    // track 0 there because the fixture's disc is blank in both runs.
    String8 title = str8_lit("Second");
    run->entries[1].title_size = (u32)title.size;
    mem_copy(run->entries[1].title, title.str, title.size);
    String8 disc_title = str8_lit("Demo");
    run->plan.write_disc_title = 1;
    run->plan.disc_title_size = (u32)disc_title.size;
    mem_copy(run->plan.disc_title, disc_title.str, disc_title.size);

    u32 result = netmd_upload_run(&run->session, arena, &run->plan, &run->state,
                                  test_upload_progress, run);
    EXPECT(result == NetmdResult_Ok);
    EXPECT(netmd_replay_ok(&run->replay));
    EXPECT(netmd_replay_done(&run->replay));
    EXPECT(run->entries[1].status == NetmdUploadStatus_Done);
    // Only the pending entry counted towards the bytes of the run.
    EXPECT(run->state.bytes_total == 2 * DSP_SP_FRAME_BYTES);
    EXPECT(run->state.done == 1);
    test_upload_report(run);
}

TEST(netmd_upload_eta) {
    Unused(arena);
    NetmdUploadState state;
    StructZero(&state);
    // Nothing sent yet: the nominal SP rate, 44100 * 4 bytes a second (s4.9).
    state.bytes_total = 44100 * 4 * 60;
    EXPECT(netmd_upload_eta_s(&state) == 60);
    state.bytes_done = 44100 * 4 * 30;
    EXPECT(netmd_upload_eta_s(&state) == 30);
    state.track_bytes = 44100 * 4 * 30;
    EXPECT(netmd_upload_eta_s(&state) == 0);
}


// --- the real device (opt in) -------------------------------------------------
// `build\tests.exe --device-upload` writes ONE ten second sine to the disc that
// is in the machine and reads the disc back. It is off by default and skipped
// with a line rather than a failure when the flag is absent, because the whole
// suite has to stay runnable with nothing plugged in - and because a test that
// writes to somebody's disc must never do it by surprise.
//
// It never erases anything: the track is appended, the disc title is left
// alone (a disc with groups keeps them), and the run stops before writing if
// the disc is missing, unwritable or protected.

TEST(netmd_device_real_upload) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 command_line = os_command_line(arena);
    if (str8_find(command_line, str8_lit("--device-upload"), 0) >= command_line.size) {
        test_report("    skipped (pass --device-upload to run it on the real device)\n");
        arena_temp_end(scratch);
        return;
    }

    OsUsbDeviceList list = os_usb_enumerate(arena);
    OsUsbDeviceInfo *found = 0;
    for (u64 i = 0; i < list.count; i += 1) {
        if (netmd_model_lookup(list.items[i].vid, list.items[i].pid) &&
            list.items[i].state == OsUsbState_Ready) {
            found = &list.items[i];
            break;
        }
    }
    EXPECT(found != 0);
    if (!found) {
        test_report("    no NetMD with a WinUSB driver bound\n");
        arena_temp_end(scratch);
        return;
    }
    OsUsb usb = os_usb_open(found->path);
    EXPECT(os_usb_is_open(usb));
    if (!os_usb_is_open(usb)) {
        arena_temp_end(scratch);
        return;
    }
    UsbTransport transport;
    os_usb_transport(usb, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, found->vid, found->pid);

    DiscLayout *before = push_struct_zero(arena, DiscLayout);
    u32 result = netmd_read_disc(&session, arena, before);
    EXPECT(result == NetmdResult_Ok);
    b32 writable = (before->flags & NetmdDiscFlag_Present) != 0 &&
                   (before->flags & NetmdDiscFlag_Writable) != 0 &&
                   (before->flags & NetmdDiscFlag_WriteProtected) == 0;
    test_report("    disc: %u track(s), %u s recorded, %u s free, flags 0x%x\n",
                before->track_count, (u32)(before->capacity.recorded.ms / 1000u),
                (u32)(before->capacity.available.ms / 1000u), before->flags);
    EXPECT(writable);
    if (result != NetmdResult_Ok || !writable) {
        test_report("    refusing to write: no disc, not recordable, or tab open\n");
        os_usb_close(usb);
        arena_temp_end(scratch);
        return;
    }

    // Ten seconds of a 440 Hz sine at -12 dBFS, 44.1 kHz stereo, s16 big endian
    // (s4.9), zero padded to whole 2048 byte frames.
    u64 sample_count = 44100u * 10u;
    u64 payload = sample_count * 4u;
    u64 total = ((payload + DSP_SP_FRAME_BYTES - 1) / DSP_SP_FRAME_BYTES) * DSP_SP_FRAME_BYTES;
    u8 *audio = push_array_zero(arena, u8, total);
    for (u64 i = 0; i < sample_count; i += 1) {
        f64 phase = 2.0 * 3.14159265358979 * 440.0 * (f64)i / 44100.0;
        i32 value = (i32)(dsp_sin_f64(phase) * 8192.0);
        u8 hi = (u8)((value >> 8) & 0xFF);
        u8 lo = (u8)(value & 0xFF);
        audio[i * 4 + 0] = hi;
        audio[i * 4 + 1] = lo;
        audio[i * 4 + 2] = hi;
        audio[i * 4 + 3] = lo;
    }

    TestAudioBlock block;
    StructZero(&block);
    block.bytes = audio;
    block.size = total;
    NetmdUploadEntry *entry = push_struct_zero(arena, NetmdUploadEntry);
    entry->data.read = test_audio_read;
    entry->data.user = &block;
    entry->data.total_bytes = total;
    entry->duration_ms = 10000;
    String8 title = str8_lit("MINIDISK TEST");
    entry->title_size = (u32)title.size;
    mem_copy(entry->title, title.str, title.size);

    NetmdUploadPlan *plan = push_struct_zero(arena, NetmdUploadPlan);
    plan->entries = entry;
    plan->count = 1;
    plan->write_disc_title = 0;  // this disc has groups; they are not ours to rewrite

    NetmdUploadState *state = push_struct_zero(arena, NetmdUploadState);
    u64 started = os_time_now_us();
    result = netmd_upload_run(&session, arena, plan, state, 0, 0);
    u64 elapsed_us = os_time_now_us() - started;
    EXPECT(result == NetmdResult_Ok);
    test_report("    upload: result %u, track %u, %llu bytes in %llu ms (%f x real time)\n",
                result, entry->track, entry->bytes, elapsed_us / 1000u,
                10000000.0 / (f64)Max(elapsed_us, (u64)1));

    DiscLayout *after = push_struct_zero(arena, DiscLayout);
    result = netmd_read_disc(&session, arena, after);
    EXPECT(result == NetmdResult_Ok);
    EXPECT(after->track_count == before->track_count + 1);
    if (after->track_count == before->track_count + 1) {
        NetmdTrack *written = &after->tracks[after->track_count - 1];
        EXPECT(str8_eq(str8(written->title, written->title_size), title));
        EXPECT(written->encoding == NetmdEncoding_SP);
        test_report("    written: \"%S\", %llu ms, encoding %u\n",
                    str8(written->title, written->title_size), written->duration_ms,
                    written->encoding);
    }
    // MD_MODE_TABLE (T-031, ex-T-045): a SP cluster is 2 s, so ten seconds of
    // audio must cost exactly five of them. The difference the device reports
    // is the measurement the acceptance criterion asks for.
    u64 free_before = before->capacity.available.ms;
    u64 free_after = after->capacity.available.ms;
    test_report("    free time: %llu ms -> %llu ms, cost %lld ms (table says %u ms)\n",
                free_before, free_after, (i64)free_before - (i64)free_after,
                5u * NETMD_SP_CLUSTER_MS);
    os_usb_close(usb);
    arena_temp_end(scratch);
}

static void test_netmd_secure_run_all(void) {
    RUN(netmd_des_known_answers);
    RUN(netmd_des_modes);
    RUN(netmd_retail_mac);
    RUN(netmd_secure_derivations);
    RUN(netmd_secure_packet_framing);
    RUN(netmd_title_encoding);
    RUN(netmd_upload_replay_sp);
    RUN(netmd_upload_replay_cancel);
    RUN(netmd_upload_replay_resume);
    RUN(netmd_upload_eta);
    RUN(netmd_device_real_upload);
}
