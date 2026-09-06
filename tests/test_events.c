// test_events.c - T-002: event ring buffer, scancode table, DropFiles UTF-8.
// Included by test_main.c (unity build), which also includes win32_window.c.

TEST(event_ring) {
    Win32EventRing ring;
    win32_event_ring_init(&ring, arena, 8);

    OsEvent event;
    EXPECT(!win32_event_ring_pop(&ring, &event));

    // Fill, drain, and check ordering.
    for (u32 i = 0; i < 8; i += 1) {
        OsEvent pushed;
        StructZero(&pushed);
        pushed.kind = OsEvent_KeyDown;
        pushed.key = OsKey_A + i;
        EXPECT(win32_event_ring_push(&ring, &pushed));
    }
    // One too many: refused, counted, and the queue is left intact.
    {
        OsEvent overflow;
        StructZero(&overflow);
        overflow.kind = OsEvent_KeyUp;
        ring.dropped = 0;
        EXPECT(!win32_event_ring_push(&ring, &overflow));
        EXPECT(ring.dropped == 1);
    }
    for (u32 i = 0; i < 8; i += 1) {
        EXPECT(win32_event_ring_pop(&ring, &event));
        EXPECT(event.kind == OsEvent_KeyDown);
        EXPECT(event.key == OsKey_A + i);
    }
    EXPECT(!win32_event_ring_pop(&ring, &event));

    // Wrap around: indices keep growing, the mask keeps them in the array.
    for (u32 round = 0; round < 5; round += 1) {
        for (u32 i = 0; i < 6; i += 1) {
            OsEvent pushed;
            StructZero(&pushed);
            pushed.kind = OsEvent_Char;
            pushed.codepoint = round * 100 + i;
            EXPECT(win32_event_ring_push(&ring, &pushed));
        }
        for (u32 i = 0; i < 6; i += 1) {
            EXPECT(win32_event_ring_pop(&ring, &event));
            EXPECT(event.codepoint == round * 100 + i);
        }
    }
    EXPECT(ring.read == ring.write);
    EXPECT(ring.read == 8 + 30);
}

TEST(scancode_table) {
    Unused(arena);
    // Positional letters: the physical key, whatever the layout calls it.
    EXPECT(win32_key_from_scancode(0x10) == OsKey_Q);
    EXPECT(win32_key_from_scancode(0x1E) == OsKey_A);
    EXPECT(win32_key_from_scancode(0x2C) == OsKey_Z);
    EXPECT(win32_key_from_scancode(0x32) == OsKey_M);
    EXPECT(win32_key_from_scancode(0x01) == OsKey_Escape);
    EXPECT(win32_key_from_scancode(0x39) == OsKey_Space);
    EXPECT(win32_key_from_scancode(0x1C) == OsKey_Enter);
    EXPECT(win32_key_from_scancode(0x0B) == OsKey_0);
    EXPECT(win32_key_from_scancode(0x02) == OsKey_1);
    EXPECT(win32_key_from_scancode(0x3B) == OsKey_F1);
    EXPECT(win32_key_from_scancode(0x58) == OsKey_F12);
    EXPECT(win32_key_from_scancode(0x2A) == OsKey_LeftShift);
    EXPECT(win32_key_from_scancode(0x1D) == OsKey_LeftCtrl);

    // Extended keys: same scancode, different key.
    EXPECT(win32_key_from_scancode(0x11C) == OsKey_NumpadEnter);
    EXPECT(win32_key_from_scancode(0x11D) == OsKey_RightCtrl);
    EXPECT(win32_key_from_scancode(0x148) == OsKey_Up);
    EXPECT(win32_key_from_scancode(0x150) == OsKey_Down);
    EXPECT(win32_key_from_scancode(0x14B) == OsKey_Left);
    EXPECT(win32_key_from_scancode(0x14D) == OsKey_Right);
    EXPECT(win32_key_from_scancode(0x153) == OsKey_Delete);
    EXPECT(win32_key_from_scancode(0x15B) == OsKey_LeftSuper);
    EXPECT(win32_key_from_scancode(0x138) == OsKey_RightAlt);
    EXPECT(win32_key_from_scancode(0x1FF) == OsKey_None);

    // Every key we produce must fit in the u8 table.
    EXPECT(OsKey_COUNT <= 256);
    for (u32 i = 0; i < ArrayCount(win32_scancode_to_key); i += 1) {
        EXPECT(win32_scancode_to_key[i] < OsKey_COUNT);
    }
    // The 26 letters appear exactly once each.
    u32 letter_hits[26];
    mem_zero(letter_hits, sizeof(letter_hits));
    for (u32 i = 0; i < ArrayCount(win32_scancode_to_key); i += 1) {
        u32 key = win32_scancode_to_key[i];
        if (key >= OsKey_A && key <= OsKey_Z) { letter_hits[key - OsKey_A] += 1; }
    }
    for (u32 i = 0; i < 26; i += 1) { EXPECT(letter_hits[i] == 1); }

    // The scancode of lParam carries the extended flag in bit 24.
    EXPECT(win32_scancode_from_lparam(0x00480001ll) == 0x48);
    EXPECT(win32_scancode_from_lparam(0x01480001ll) == 0x148);
}

TEST(drop_path_utf8) {
    // Accents: the case the acceptance criteria cares about.
    const u16 accented[] = {'C', ':', '\\', 'M', 0x00FC, 's', 'i', 'q', 'u', 'e',
                            '\\', 0x00E9, 't', 0x00E9, '.', 'm', 'p', '3'};
    String8 path = win32_drop_path_to_utf8(arena, accented, ArrayCount(accented));
    EXPECT(str8_eq(path, str8_lit("C:\\M\xC3\xBCsique\\\xC3\xA9t\xC3\xA9.mp3")));

    // Japanese: MD titles and folder names are often kana.
    const u16 kana[] = {'D', ':', '\\', 0x30DF, 0x30E5, 0x30FC, 0x30B8, 0x30C3, 0x30AF};
    String8 kana_path = win32_drop_path_to_utf8(arena, kana, ArrayCount(kana));
    EXPECT(str8_eq(kana_path, str8_lit("D:\\\xE3\x83\x9F\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xB8"
                                       "\xE3\x83\x83\xE3\x82\xAF")));

    // Surrogate pair (U+1F3B5 musical note) must become one 4 byte sequence.
    const u16 emoji[] = {'X', ':', '\\', 0xD83C, 0xDFB5};
    String8 emoji_path = win32_drop_path_to_utf8(arena, emoji, ArrayCount(emoji));
    EXPECT(str8_eq(emoji_path, str8_lit("X:\\\xF0\x9F\x8E\xB5")));

    // Round trip: UTF-8 back to UTF-16 gives the original units back.
    String8 source = str8_lit("C:\\M\xC3\xBCsique\\\xC3\xA9t\xC3\xA9.mp3");
    String16 wide = str16_from_str8(arena, source);
    EXPECT(wide.size == ArrayCount(accented));
    EXPECT(mem_cmp(wide.str, accented, sizeof(accented)) == 0);
    EXPECT(str8_eq(win32_drop_path_to_utf8(arena, wide.str, wide.size), source));

    // An empty drop entry is an empty path, not a crash.
    EXPECT(win32_drop_path_to_utf8(arena, accented, 0).size == 0);
}

static void test_events_run_all(void) {
    RUN(event_ring);
    RUN(scancode_table);
    RUN(drop_path_utf8);
}
