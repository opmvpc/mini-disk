#include "plan_file.h"

static const u8 plan_file_magic[8] = {'M', 'D', 'S', 'K', 'P', 'L', 'N', 0};

md_inline u64 plan_file_align(u64 value) { return AlignPow2(value, 8ull); }

// The plan's storage, emptied: both arenas belong to it, exactly as the
// library's belong to the library (lib_cache_load).
static void plan_storage_reset(Plan *plan) {
    Arena *arena = plan->arena;
    Arena *text = plan->text;
    arena_clear(arena);
    if (text != arena) { arena_clear(text); }
    plan_init(plan, arena, text);
}

// --- binary: writing ----------------------------------------------------------

String8 plan_serialize(Arena *arena, const Plan *plan) {
    u64 entry_total = plan_entry_count(plan);

    PlanFileHeader header;
    StructZero(&header);
    mem_copy(header.magic, plan_file_magic, sizeof(header.magic));
    header.version = PLAN_FILE_VERSION;
    header.disc_count = plan->disc_count;
    header.entry_count = entry_total;
    header.title = plan->title;
    header.strings_offset = plan_file_align(sizeof(PlanFileHeader));
    header.strings_size = plan->strings.size;
    header.string_count = plan->strings.count;
    header.discs_offset = plan_file_align(header.strings_offset + header.strings_size);
    header.discs_size = plan->disc_count * sizeof(PlanFileDisc);
    header.entries_offset = plan_file_align(header.discs_offset + header.discs_size);
    header.entries_size = entry_total * sizeof(PlanFileEntry);
    header.created_us = os_time_now_us();
    header.file_size = plan_file_align(header.entries_offset + header.entries_size);

    u8 *buffer = push_array_zero(arena, u8, header.file_size);
    mem_copy(buffer, &header, sizeof(header));
    mem_copy(buffer + header.strings_offset, plan->strings.base, header.strings_size);

    PlanFileDisc *discs = (PlanFileDisc *)(buffer + header.discs_offset);
    PlanFileEntry *entries = (PlanFileEntry *)(buffer + header.entries_offset);
    u64 written = 0;
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        const PlanDisc *disc = &plan->discs[d];
        PlanFileDisc *record = &discs[d];
        record->title = disc->title;
        record->length_min = disc->length_min;
        record->default_mode = disc->default_mode;
        record->entry_count = disc->entry_count;
        record->group_live = disc->group_live;
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) { record->group_name[g] = disc->groups[g].name; }
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            PlanFileEntry *out = &entries[written + i];
            out->track_id = disc->track_id[i];
            out->path_id = disc->path_id[i];
            out->title_override = disc->title_override[i];
            out->duration_ms = disc->duration_ms[i];
            out->gain_db = disc->gain_db[i];
            out->fade_in_ms = disc->fade_in_ms[i];
            out->fade_out_ms = disc->fade_out_ms[i];
            out->trim_head_ms = disc->trim_head_ms[i];
            out->trim_tail_ms = disc->trim_tail_ms[i];
            out->mode = disc->mode[i];
            out->flags = disc->flags[i];
            out->group_id = disc->group_id[i];
        }
        written += disc->entry_count;
    }
    AssertAlways(written == entry_total);
    return str8(buffer, header.file_size);
}

// The atomic write of ADR-010 s9.3: the bytes land in a .tmp, they are on the
// platter before the rename that publishes them, and a failure leaves the old
// file exactly as it was. This is the part that costs 7 ms, and the part that
// runs on a job.
static PlanFileStatus plan_write_atomic(String8 path, String8 bytes) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 tmp = str8_cat(scratch.arena, path, str8_lit(".tmp"));
    PlanFileStatus status = PlanFile_Ok;
    if (!os_file_write_all(tmp, bytes) || !os_file_move_replace(tmp, path)) {
        os_file_delete(tmp);
        status = PlanFile_WriteFailed;
    }
    scratch_end(scratch);
    return status;
}

// The synchronous save: the import/export paths, the tests and the bench. The
// application goes through the saver instead (plan_save_async).
PlanFileStatus plan_save(const Plan *plan, String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 bytes = plan_serialize(scratch.arena, plan);
    PlanFileStatus status = plan_write_atomic(path, bytes);
    scratch_end(scratch);
    return status;
}

// --- binary: the boundary -------------------------------------------------------

md_inline b32 plan_section_ok(u64 offset, u64 size, u64 file_size) {
    return (offset % 8) == 0 && offset <= file_size && size <= file_size - offset;
}

md_inline b32 plan_bit_get(const u64 *bits, u64 index) {
    return (bits[index >> 6] >> (index & 63)) & 1;
}

// Walks the blob once: every string is a u16 length and its bytes, the first is
// the empty one, and the last ends exactly on the section. The bitmap it fills
// is what makes a string id checkable - an id in the middle of a string looks
// perfectly plausible to a bounds test and is caught here.
static b32 plan_strings_validate(const u8 *strings, u64 size, u64 expected_count, u64 *bits) {
    if (size < 2 || strings[0] != 0 || strings[1] != 0) { return 0; }
    u64 at = 0;
    u64 count = 0;
    while (at < size) {
        if (at + 2 > size) { return 0; }
        u64 length = (u64)strings[at] | ((u64)strings[at + 1] << 8);
        if (at + 2 + length > size) { return 0; }
        bits[at >> 6] |= (u64)1 << (at & 63);
        if (at != 0) { count += 1; }  // offset 0 is the empty string, never interned
        at += 2 + length;
    }
    return at == size && count == expected_count;
}

static b32 plan_file_validate(const PlanFileHeader *header, const u8 *base, u64 mapped_size,
                              const u64 *string_bits) {
    if (header->file_size != mapped_size) { return 0; }
    if (header->disc_count == 0 || header->disc_count > PLAN_DISC_MAX) { return 0; }
    if (header->entry_count > (u64)PLAN_DISC_MAX * PLAN_ENTRY_MAX) { return 0; }
    if (header->discs_size != header->disc_count * sizeof(PlanFileDisc)) { return 0; }
    if (header->entries_size != header->entry_count * sizeof(PlanFileEntry)) { return 0; }
    if (header->strings_offset < sizeof(PlanFileHeader)) { return 0; }
    if (!plan_section_ok(header->strings_offset, header->strings_size, mapped_size)) { return 0; }
    if (!plan_section_ok(header->discs_offset, header->discs_size, mapped_size)) { return 0; }
    if (!plan_section_ok(header->entries_offset, header->entries_size, mapped_size)) { return 0; }
    if (header->title >= header->strings_size || !plan_bit_get(string_bits, header->title)) {
        return 0;
    }

    const PlanFileDisc *discs = (const PlanFileDisc *)(base + header->discs_offset);
    const PlanFileEntry *entries = (const PlanFileEntry *)(base + header->entries_offset);
    u64 seen = 0;
    for (u64 d = 0; d < header->disc_count; d += 1) {
        const PlanFileDisc *disc = &discs[d];
        if (!plan_length_valid(disc->length_min)) { return 0; }
        if (disc->default_mode >= PlanMode_COUNT) { return 0; }
        if (disc->entry_count > PLAN_ENTRY_MAX) { return 0; }
        if (disc->title >= header->strings_size || !plan_bit_get(string_bits, disc->title)) {
            return 0;
        }
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
            u32 name = disc->group_name[g];
            if (!(disc->group_live & (1u << g))) { continue; }
            if (name >= header->strings_size || !plan_bit_get(string_bits, name)) { return 0; }
        }
        if (seen + disc->entry_count > header->entry_count) { return 0; }
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            const PlanFileEntry *entry = &entries[seen + i];
            if (entry->mode >= PlanMode_COUNT) { return 0; }
            if (entry->flags & ~(u32)PLAN_ENTRY_FLAG_MASK) { return 0; }
            if (entry->group_id != PLAN_GROUP_NONE &&
                (entry->group_id >= PLAN_GROUP_MAX ||
                 !(disc->group_live & (1u << entry->group_id)))) {
                return 0;
            }
            if (entry->path_id >= header->strings_size ||
                !plan_bit_get(string_bits, entry->path_id)) {
                return 0;
            }
            if (entry->title_override >= header->strings_size ||
                !plan_bit_get(string_bits, entry->title_override)) {
                return 0;
            }
        }
        seen += disc->entry_count;
    }
    return seen == header->entry_count;
}

PlanFileStatus plan_load(Plan *plan, String8 path) {
    OsFileMap map;
    if (!os_file_map(&map, path)) { return PlanFile_Missing; }
    if (map.size < sizeof(PlanFileHeader)) {
        os_file_unmap(&map);
        return PlanFile_Corrupt;
    }
    PlanFileHeader header;
    mem_copy(&header, map.data, sizeof(header));
    if (mem_cmp(header.magic, plan_file_magic, sizeof(plan_file_magic)) != 0) {
        os_file_unmap(&map);
        return PlanFile_Corrupt;
    }
    if (header.version != PLAN_FILE_VERSION) {
        os_file_unmap(&map);
        return PlanFile_BadVersion;
    }
    if (header.strings_size < 2 || header.strings_size > PLAN_FILE_STRINGS_MAX ||
        !plan_section_ok(header.strings_offset, header.strings_size, map.size)) {
        os_file_unmap(&map);
        return PlanFile_Corrupt;
    }

    ArenaTemp scratch = scratch_begin(0, 0);
    u64 *bits = push_array_zero(scratch.arena, u64, (header.strings_size + 63) / 64);
    b32 ok = plan_strings_validate(map.data + header.strings_offset, header.strings_size,
                                   header.string_count, bits) &&
             plan_file_validate(&header, map.data, map.size, bits);
    if (!ok) {
        scratch_end(scratch);
        os_file_unmap(&map);
        return PlanFile_Corrupt;
    }

    // Canonical from here on. The blob is replayed through lib_intern rather
    // than copied with its hash table: interning a table's strings in blob
    // order into an empty one reproduces the very same offsets (the blob holds
    // no duplicates), so every StringId in the file stays valid and the table
    // we end up with was built by the same code as any other.
    plan_storage_reset(plan);
    const u8 *strings = map.data + header.strings_offset;
    for (u64 at = 2; at < header.strings_size;) {
        u64 length = (u64)strings[at] | ((u64)strings[at + 1] << 8);
        StringId id = lib_intern(&plan->strings, str8((u8 *)strings + at + 2, length));
        AssertAlways(id == (StringId)at);
        at += 2 + length;
    }

    const PlanFileDisc *discs = (const PlanFileDisc *)(map.data + header.discs_offset);
    const PlanFileEntry *entries = (const PlanFileEntry *)(map.data + header.entries_offset);
    plan->title = (StringId)header.title;
    plan->disc_count = (u32)header.disc_count;
    u64 seen = 0;
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        const PlanFileDisc *record = &discs[d];
        PlanDisc *disc = &plan->discs[d];
        disc->title = record->title;
        disc->length_min = record->length_min;
        disc->default_mode = record->default_mode;
        disc->entry_count = record->entry_count;
        disc->group_live = record->group_live;
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) { disc->groups[g].name = record->group_name[g]; }
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            const PlanFileEntry *in = &entries[seen + i];
            PlanEntry entry;
            StructZero(&entry);
            entry.track_id = in->track_id;
            entry.path_id = in->path_id;
            entry.title_override = in->title_override;
            entry.duration_ms = in->duration_ms;
            entry.gain_db = in->gain_db;
            entry.fade_in_ms = in->fade_in_ms;
            entry.fade_out_ms = in->fade_out_ms;
            entry.trim_head_ms = in->trim_head_ms;
            entry.trim_tail_ms = in->trim_tail_ms;
            entry.mode = in->mode;
            entry.flags = in->flags;
            entry.group_id = in->group_id;
            plan_entry_store(disc, i, &entry);
        }
        plan_groups_refresh(disc);
        seen += disc->entry_count;
    }
    plan->revision += 1;
    scratch_end(scratch);
    os_file_unmap(&map);
    return PlanFile_Ok;
}

// --- text: one line per track ------------------------------------------------

static const char *plan_mode_name[PlanMode_COUNT] = {"SP", "LP2", "LP4"};

// Tabs and newlines are the format's structure; a title carrying one is shown
// with a space instead. The binary file is the master, this is the readable view.
static String8 plan_text_clean(Arena *arena, String8 s) {
    String8 out = str8_copy(arena, s);
    for (u64 i = 0; i < out.size; i += 1) {
        if (out.str[i] == '\t' || out.str[i] == '\n' || out.str[i] == '\r') { out.str[i] = ' '; }
    }
    return out;
}

PlanFileStatus plan_export_text(const Plan *plan, String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    Arena *arena = scratch.arena;
    String8List lines;
    StructZero(&lines);
    str8_list_push(arena, &lines,
                   str8f(arena, "# minidisk plan %u\n", (u32)PLAN_FILE_VERSION));
    str8_list_push(arena, &lines,
                   str8f(arena, "plan\t%S\n",
                         plan_text_clean(arena, plan_string(plan, plan->title))));
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        const PlanDisc *disc = &plan->discs[d];
        str8_list_push(arena, &lines,
                       str8f(arena, "disc\t%S\t%u\t%s\n",
                             plan_text_clean(arena, plan_string(plan, disc->title)),
                             (u32)disc->length_min, plan_mode_name[disc->default_mode]));
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
            if (!(disc->group_live & (1u << g))) { continue; }
            str8_list_push(arena, &lines,
                           str8f(arena, "group\t%u\t%S\n", g,
                                 plan_text_clean(arena, plan_string(plan, disc->groups[g].name))));
        }
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            String8 group = (disc->group_id[i] == PLAN_GROUP_NONE)
                                ? str8_lit("-")
                                : str8f(arena, "%u", (u32)disc->group_id[i]);
            String8 gain = (disc->gain_db[i] == PLAN_GAIN_NONE)
                               ? str8_lit("-")
                               : str8f(arena, "%d", (i32)disc->gain_db[i]);
            str8_list_push(
                arena, &lines,
                str8f(arena, "track\t%s%s\t%u\t%S\t%S\t%u\t%u\t%u\t%u\t%S\t%S\n",
                      plan_mode_name[disc->mode[i]],
                      (disc->flags[i] & PlanEntryFlag_Mono) ? "-MONO" : "",
                      disc->duration_ms[i], group, gain, (u32)disc->trim_head_ms[i],
                      (u32)disc->trim_tail_ms[i], (u32)disc->fade_in_ms[i],
                      (u32)disc->fade_out_ms[i],
                      plan_text_clean(arena, plan_string(plan, disc->title_override[i])),
                      plan_text_clean(arena, plan_string(plan, disc->path_id[i]))));
        }
    }
    String8 text = str8_list_join(arena, &lines, str8(0, 0));
    String8 tmp = str8_cat(arena, path, str8_lit(".tmp"));
    PlanFileStatus status = PlanFile_Ok;
    if (!os_file_write_all(tmp, text) || !os_file_move_replace(tmp, path)) {
        os_file_delete(tmp);
        status = PlanFile_WriteFailed;
    }
    scratch_end(scratch);
    return status;
}

// --- text: reading, which is a boundary too ------------------------------------

typedef struct PlanTextCursor {
    String8 line;
    u64 at;
} PlanTextCursor;

// The next tab separated field. A missing field comes back empty, which every
// caller below reads as "malformed" through its own parse.
static String8 plan_text_field(PlanTextCursor *cursor) {
    if (cursor->at > cursor->line.size) { return str8(0, 0); }
    u64 start = cursor->at;
    u64 end = start;
    while (end < cursor->line.size && cursor->line.str[end] != '\t') { end += 1; }
    cursor->at = end + 1;
    return str8_substr(cursor->line, start, end - start);
}

static b32 plan_text_u32(String8 s, u32 *out) {
    if (s.size == 0 || s.size > 10) { return 0; }
    u64 value = 0;
    for (u64 i = 0; i < s.size; i += 1) {
        if (s.str[i] < '0' || s.str[i] > '9') { return 0; }
        value = value * 10 + (u64)(s.str[i] - '0');
    }
    if (value > U32_MAX) { return 0; }
    *out = (u32)value;
    return 1;
}

static b32 plan_text_i16(String8 s, i16 *out) {
    b32 negative = (s.size != 0 && s.str[0] == '-');
    u32 magnitude = 0;
    if (!plan_text_u32(negative ? str8_skip(s, 1) : s, &magnitude)) { return 0; }
    if (magnitude > 32767) { return 0; }
    *out = (i16)(negative ? -(i32)magnitude : (i32)magnitude);
    return 1;
}

static b32 plan_text_mode(String8 s, u8 *mode, b32 *mono) {
    *mono = 0;
    if (str8_ends_with(s, str8_lit("-MONO"))) {
        *mono = 1;
        s = str8_prefix(s, s.size - 5);
    }
    for (u32 m = 0; m < PlanMode_COUNT; m += 1) {
        if (str8_eq(s, str8_cstr(plan_mode_name[m]))) {
            *mode = (u8)m;
            return 1;
        }
    }
    return 0;
}

PlanFileStatus plan_import_text(Plan *plan, String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 text = os_file_read_all(scratch.arena, path);
    if (text.size == 0) {
        scratch_end(scratch);
        return PlanFile_Missing;
    }
    String8List lines = str8_split(scratch.arena, text, '\n');

    // Built beside the live document and swapped in only once the whole file
    // parsed: a half read plan never reaches the screen.
    plan_storage_reset(plan);
    plan->disc_count = 0;
    PlanDisc *disc = 0;
    b32 ok = 1;
    for (String8Node *node = lines.first; node && ok; node = node->next) {
        String8 line = node->str;
        if (line.size != 0 && line.str[line.size - 1] == '\r') {
            line = str8_prefix(line, line.size - 1);
        }
        if (line.size == 0 || line.str[0] == '#') { continue; }
        PlanTextCursor cursor;
        cursor.line = line;
        cursor.at = 0;
        String8 kind = plan_text_field(&cursor);

        if (str8_eq(kind, str8_lit("plan"))) {
            plan->title = lib_intern(&plan->strings,
                                     str8_prefix(plan_text_field(&cursor), PLAN_TITLE_MAX));
        } else if (str8_eq(kind, str8_lit("disc"))) {
            if (plan->disc_count >= PLAN_DISC_MAX) { ok = 0; break; }
            disc = &plan->discs[plan->disc_count];
            plan->disc_count += 1;
            String8 title = plan_text_field(&cursor);
            u32 length = 0;
            u8 mode = PlanMode_SP;
            b32 mono = 0;
            ok = plan_text_u32(plan_text_field(&cursor), &length) && plan_length_valid(length) &&
                 plan_text_mode(plan_text_field(&cursor), &mode, &mono);
            if (!ok) { break; }
            disc->title = lib_intern(&plan->strings, str8_prefix(title, PLAN_TITLE_MAX));
            disc->length_min = (u16)length;
            disc->default_mode = mode;
        } else if (str8_eq(kind, str8_lit("group"))) {
            u32 g = 0;
            ok = disc && plan_text_u32(plan_text_field(&cursor), &g) && g < PLAN_GROUP_MAX;
            if (!ok) { break; }
            disc->group_live |= (1u << g);
            disc->groups[g].name =
                lib_intern(&plan->strings, str8_prefix(plan_text_field(&cursor), PLAN_TITLE_MAX));
        } else if (str8_eq(kind, str8_lit("track"))) {
            if (!disc || disc->entry_count >= PLAN_ENTRY_MAX) { ok = 0; break; }
            PlanEntry entry;
            StructZero(&entry);
            entry.track_id = LIB_TRACK_NONE;
            entry.gain_db = PLAN_GAIN_NONE;
            entry.group_id = PLAN_GROUP_NONE;
            // The id is not in the text on purpose: a plan that travelled to
            // another machine is resolved by path, and flagged Missing until it
            // is (plan_resolve).
            entry.flags = PlanEntryFlag_Missing;
            b32 mono = 0;
            u32 trim_head = 0, trim_tail = 0, fade_in = 0, fade_out = 0;
            ok = plan_text_mode(plan_text_field(&cursor), &entry.mode, &mono) &&
                 plan_text_u32(plan_text_field(&cursor), &entry.duration_ms);
            if (!ok) { break; }
            if (mono) { entry.flags |= PlanEntryFlag_Mono; }
            String8 group = plan_text_field(&cursor);
            if (!str8_eq(group, str8_lit("-"))) {
                u32 g = 0;
                ok = plan_text_u32(group, &g) && g < PLAN_GROUP_MAX &&
                     (disc->group_live & (1u << g)) != 0;
                if (!ok) { break; }
                entry.group_id = (u8)g;
            }
            String8 gain = plan_text_field(&cursor);
            if (!str8_eq(gain, str8_lit("-")) && !plan_text_i16(gain, &entry.gain_db)) {
                ok = 0;
                break;
            }
            ok = plan_text_u32(plan_text_field(&cursor), &trim_head) &&
                 plan_text_u32(plan_text_field(&cursor), &trim_tail) &&
                 plan_text_u32(plan_text_field(&cursor), &fade_in) &&
                 plan_text_u32(plan_text_field(&cursor), &fade_out);
            if (!ok || trim_head > U16_MAX || trim_tail > U16_MAX || fade_in > U16_MAX ||
                fade_out > U16_MAX) {
                ok = 0;
                break;
            }
            entry.trim_head_ms = (u16)trim_head;
            entry.trim_tail_ms = (u16)trim_tail;
            entry.fade_in_ms = (u16)fade_in;
            entry.fade_out_ms = (u16)fade_out;
            entry.title_override =
                lib_intern(&plan->strings, str8_prefix(plan_text_field(&cursor), PLAN_TITLE_MAX));
            String8 track_path = plan_text_field(&cursor);
            if (track_path.size == 0 || track_path.size > OS_PATH_MAX) { ok = 0; break; }
            entry.path_id = lib_intern(&plan->strings, track_path);
            plan_entry_store(disc, disc->entry_count, &entry);
            disc->entry_count += 1;
        }
    }
    if (!ok || plan->disc_count == 0) {
        plan_storage_reset(plan);
        scratch_end(scratch);
        return PlanFile_Corrupt;
    }
    for (u32 d = 0; d < plan->disc_count; d += 1) { plan_groups_refresh(&plan->discs[d]); }
    plan->revision += 1;
    scratch_end(scratch);
    return PlanFile_Ok;
}

// --- autosave -------------------------------------------------------------------

String8 plan_autosave_path(Arena *arena, String8 base_dir) {
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 dir = os_path_join(scratch.arena, base_dir, str8_lit("plans"));
    os_dir_create(dir);
    String8 path = os_path_join(arena, dir, str8_lit("autosave.mdplan"));
    scratch_end(scratch);
    return path;
}

// --- the save job (P-009) -------------------------------------------------

void plan_saver_init(PlanSaver *saver, Arena *arena) {
    StructZero(saver);
    saver->arena = arena;
}

static void plan_save_job(void *data, u64 begin, u64 end) {
    Unused(begin);
    Unused(end);
    PlanSaver *saver = (PlanSaver *)data;
    u64 start_us = os_time_now_us();
    PlanFileStatus status = plan_write_atomic(saver->path, saver->bytes);
    saver->disk_us = os_time_now_us() - start_us;
    saver->saves += 1;
    saver->status = (u32)status;
    // Last, and interlocked: everything above is visible to whoever sees the
    // new state, which is what lets the view read the verdict without a lock.
    os_atomic_store_u32(&saver->state,
                        (status == PlanFile_Ok) ? PlanSave_Done : PlanSave_Failed);
    // One redraw so the indicator in the header stops saying "en cours". The
    // loop is asleep by now: this is the only wake-up a save costs.
    os_request_redraw();
}

b32 plan_save_async(PlanSaver *saver, const Plan *plan, String8 path) {
    if (plan_save_state(saver) == PlanSave_Running) { return 0; }
    u64 start_us = os_time_now_us();
    // Safe only because nothing is in flight: the arena holds the snapshot the
    // previous job was reading.
    arena_clear(saver->arena);
    saver->path = str8_copy(saver->arena, path);
    saver->bytes = plan_serialize(saver->arena, plan);
    os_atomic_store_u32(&saver->state, PlanSave_Running);
    saver->counter.pending = 0;
    jobs_push(&saver->counter, plan_save_job, saver);
    saver->frame_us = os_time_now_us() - start_us;
    return 1;
}

void plan_save_wait(PlanSaver *saver) {
    if (plan_save_state(saver) != PlanSave_Running) { return; }
    jobs_wait(&saver->counter);
}

b32 plan_autosave_tick(Plan *plan, PlanSaver *saver, String8 path, u64 now_us) {
    if (!plan->dirty || now_us - plan->dirty_us < PLAN_AUTOSAVE_US) { return 0; }
    // Best effort, like the library cache: an autosave that cannot start
    // because the previous one is still on the disk comes back in five seconds,
    // and either way the frame does not wait for a platter.
    if (!plan_save_async(saver, plan, path)) { return 0; }
    plan->dirty = 0;
    plan->dirty_us = now_us;
    return 1;
}
