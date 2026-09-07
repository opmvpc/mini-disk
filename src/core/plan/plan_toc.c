// plan_toc.c - the 255 x 7 character budget and everything that spends from it.
// See plan_toc.h for the model; plan_charset.h holds the generated tables.

#include "plan_charset.h"

// --- sanitize ------------------------------------------------------------------

static const PlanCharsetEntry *plan_charset_find(u32 cp) {
    u32 low = 0, high = (u32)PLAN_CHARSET_COUNT;
    while (low < high) {
        u32 mid = low + (high - low) / 2;
        u32 at = plan_charset_map[mid].cp;
        if (at == cp) { return &plan_charset_map[mid]; }
        if (at < cp) { low = mid + 1; } else { high = mid; }
    }
    return 0;
}

// Half-width katakana is the one non-ASCII thing the output may hold, and it is
// three bytes of UTF-8 each: counting characters is counting lead bytes.
u32 plan_toc_halfwidth_len(String8 sanitized) {
    u32 chars = 0;
    for (u64 i = 0; i < sanitized.size; i += 1) {
        if ((sanitized.str[i] & 0xC0u) != 0x80u) { chars += 1; }
    }
    return chars;
}

// At most 7 bytes, written by hand. Not mem_copy (__movsb costs more to set up
// than the bytes cost to move, and this runs once per character of every title),
// and not a byte loop either: under /GL MSVC turns that into a call to memcpy
// the linker cannot resolve without the CRT (CONVENTIONS.md, T-004).
static u64 plan_toc_emit(u8 *out, u64 cap, u64 at, const u8 *bytes, u64 size) {
    if (at + size > cap) { return at; }
    switch (size) {
        case 7: out[at + 6] = bytes[6];  // fallthrough
        case 6: out[at + 5] = bytes[5];  // fallthrough
        case 5: out[at + 4] = bytes[4];  // fallthrough
        case 4: out[at + 3] = bytes[3];  // fallthrough
        case 3: out[at + 2] = bytes[2];  // fallthrough
        case 2: out[at + 1] = bytes[1];  // fallthrough
        case 1: out[at + 0] = bytes[0];  // fallthrough
        default: break;
    }
    return at + size;
}

u64 plan_toc_sanitize(String8 in, u8 *out, u64 cap, u32 *chars_out) {
    // `out` may be null: the budget only ever wants the character count, and
    // counting without writing is the common case (one call per track title).
    if (out == 0) { cap = U64_MAX; }
    u64 at = 0;
    u32 chars = 0;
    u64 i = 0;
    while (i < in.size) {
        // Plain ASCII, which is most of most titles, is taken in one run: the
        // decoder, the table and the per character bookkeeping are all skipped,
        // and one character is worth exactly one byte and one cell character.
        u64 run = i;
        while (run < in.size) {
            u8 byte = in.str[run];
            if (byte < 0x20 || byte >= 0x7F) { break; }
            run += 1;
        }
        if (run != i) {
            u64 size_run = run - i;
            if (at + size_run > cap) { size_run = cap - at; }
            if (out && size_run != 0) { mem_copy(out + at, in.str + i, size_run); }
            at += size_run;
            chars += (u32)size_run;
            i += size_run;
            if (i != run) { break; }  // the output buffer is full
            continue;
        }

        UnicodeDecode decoded = utf8_decode(in.str + i, in.size - i);
        i += decoded.advance;
        u32 cp = decoded.codepoint;
        if (cp == 0x09 || cp == 0x0A || cp == 0x0D) { cp = ' '; }

        u8 folded[7];
        const u8 *bytes = folded;
        u32 size;
        if (cp >= 0x20 && cp < 0x7F) {
            folded[0] = (u8)cp;
            size = 1;
        } else if (cp >= 0xFF01 && cp <= 0xFF5E) {
            // Full-width ASCII and the ideographic space fold down
            // arithmetically: 100 table entries saved (research/01 s3.11).
            folded[0] = (u8)(cp - 0xFEE0u);
            size = 1;
        } else if (cp == 0x3000) {
            folded[0] = ' ';
            size = 1;
        } else if (cp >= 0xFF61 && cp <= 0xFF9F) {
            size = utf8_encode(folded, cp);  // already half-width katakana
        } else {
            const PlanCharsetEntry *entry = plan_charset_find(cp);
            if (!entry) { continue; }  // emoji, ideographs, symbols: dropped
            bytes = entry->utf8;
            size = entry->size;
        }

        if (at + size > cap) { break; }
        if (out) {
            at = plan_toc_emit(out, cap, at, bytes, size);
        } else {
            at += size;
        }
        for (u32 k = 0; k < size; k += 1) {
            if ((bytes[k] & 0xC0u) != 0x80u) { chars += 1; }
        }
    }
    if (chars_out) { *chars_out = chars; }
    return at;
}

u32 plan_toc_cells_for_title(String8 title, b32 non_sp) {
    u32 chars = 0;
    plan_toc_sanitize(title, 0, 0, &chars);
    u32 cells = plan_toc_cells_for_chars(chars);
    u32 floor_cells = non_sp ? 1u : 0u;  // the device's own "LP: " prefix
    return cells > floor_cells ? cells : floor_cells;
}

// --- shortening -----------------------------------------------------------------

// Cuts the buffer down to `max_chars` characters without splitting one in half.
static u64 plan_toc_truncate(const u8 *buffer, u64 size, u32 max_chars) {
    u32 chars = 0;
    u64 at = 0;
    while (at < size) {
        if ((buffer[at] & 0xC0u) != 0x80u) {
            if (chars == max_chars) { return at; }
            chars += 1;
        }
        at += 1;
    }
    return size;
}

static b32 plan_toc_ascii_eq_ci(const u8 *a, const char *b, u64 size) {
    for (u64 i = 0; i < size; i += 1) {
        u8 x = a[i], y = (u8)b[i];
        if (x >= 'A' && x <= 'Z') { x = (u8)(x + 32); }
        if (y >= 'A' && y <= 'Z') { y = (u8)(y + 32); }
        if (x != y) { return 0; }
    }
    return 1;
}

static b32 plan_toc_is_feat(const u8 *buffer, u64 size, u64 at) {
    if (at + 4 <= size && plan_toc_ascii_eq_ci(buffer + at, "feat", 4)) { return 1; }
    if (at + 3 <= size && plan_toc_ascii_eq_ci(buffer + at, "ft.", 3)) { return 1; }
    return 0;
}

static u64 plan_toc_rstrip(const u8 *buffer, u64 size) {
    while (size > 0 && (buffer[size - 1] == ' ' || buffer[size - 1] == '-')) { size -= 1; }
    return size;
}

// Step 1: the featuring credit, wherever it hides. A bracketed one is cut out,
// a trailing " feat. X" is cut off; the rest of the title is untouched.
static u64 plan_toc_strip_feat(u8 *buffer, u64 size) {
    for (u64 i = 0; i < size; i += 1) {
        u8 c = buffer[i];
        if (c == '(' || c == '[') {
            u8 close = (c == '(') ? ')' : ']';
            u64 end = i + 1;
            while (end < size && buffer[end] != close) { end += 1; }
            if (!plan_toc_is_feat(buffer, size, i + 1)) { continue; }
            u64 after = (end < size) ? end + 1 : size;
            u64 start = plan_toc_rstrip(buffer, i);
            mem_move(buffer + start, buffer + after, size - after);
            return plan_toc_rstrip(buffer, start + (size - after));
        }
        if (c == ' ' && plan_toc_is_feat(buffer, size, i + 1)) {
            return plan_toc_rstrip(buffer, i);
        }
    }
    return size;
}

// Step 2: every remaining bracketed aside - "(Remastered)", "[Live]", "(2011)".
static u64 plan_toc_strip_brackets(u8 *buffer, u64 size) {
    u64 write = 0;
    u64 read = 0;
    while (read < size) {
        u8 c = buffer[read];
        if (c == '(' || c == '[') {
            u8 close = (c == '(') ? ')' : ']';
            u64 end = read + 1;
            while (end < size && buffer[end] != close) { end += 1; }
            write = plan_toc_rstrip(buffer, write);
            read = (end < size) ? end + 1 : size;
            continue;
        }
        buffer[write] = c;
        write += 1;
        read += 1;
    }
    return plan_toc_rstrip(buffer, write);
}

// Step 3: "Artist - Title". The artist is what the user can reconstruct from
// the disc title, so it gives way first, and disappears entirely rather than
// being cut down to a stub.
static u64 plan_toc_shrink_artist(u8 *buffer, u64 size, u32 max_chars) {
    u64 separator = size;
    for (u64 i = 0; i + 3 <= size; i += 1) {
        if (buffer[i] == ' ' && buffer[i + 1] == '-' && buffer[i + 2] == ' ') {
            separator = i;
            break;
        }
    }
    if (separator == size) { return size; }

    u64 rest_at = separator + 3;
    u64 rest_size = size - rest_at;
    u32 rest_chars = plan_toc_halfwidth_len(str8(buffer + rest_at, rest_size));
    if (rest_chars >= max_chars) {
        // No room for an artist at all: keep the title alone.
        mem_move(buffer, buffer + rest_at, rest_size);
        return rest_size;
    }
    // The " - " stays, and an artist of fewer than 3 characters is a stub, not
    // a name: below 6 characters of room the artist goes entirely.
    u32 room_total = max_chars - rest_chars;
    if (room_total < 6) {
        mem_move(buffer, buffer + rest_at, rest_size);
        return rest_size;
    }
    u32 room = room_total - 3;
    u64 artist_size = plan_toc_rstrip(buffer, plan_toc_truncate(buffer, separator, room));
    mem_move(buffer + artist_size, buffer + separator, rest_size + 3);
    return artist_size + 3 + rest_size;
}

void plan_toc_preview(String8 title, u32 max_chars, PlanTitlePreview *out) {
    StructZero(out);
    u32 chars = 0;
    u64 size = plan_toc_sanitize(title, out->text, sizeof(out->text), &chars);

    if (max_chars != 0 && chars > max_chars) {
        u64 stripped = plan_toc_strip_feat(out->text, size);
        if (stripped != size) {
            out->applied |= PlanShorten_Feat;
            size = stripped;
            chars = plan_toc_halfwidth_len(str8(out->text, size));
        }
    }
    if (max_chars != 0 && chars > max_chars) {
        u64 stripped = plan_toc_strip_brackets(out->text, size);
        if (stripped != size) {
            out->applied |= PlanShorten_Brackets;
            size = stripped;
            chars = plan_toc_halfwidth_len(str8(out->text, size));
        }
    }
    if (max_chars != 0 && chars > max_chars) {
        u64 shrunk = plan_toc_shrink_artist(out->text, size, max_chars);
        if (shrunk != size) {
            out->applied |= PlanShorten_Artist;
            size = shrunk;
            chars = plan_toc_halfwidth_len(str8(out->text, size));
        }
    }
    if (max_chars != 0 && chars > max_chars) {
        size = plan_toc_truncate(out->text, size, max_chars);
        size = plan_toc_rstrip(out->text, size);
        chars = plan_toc_halfwidth_len(str8(out->text, size));
        out->applied |= PlanShorten_Title;
        out->truncated = 1;
    }

    out->size = size;
    out->chars = chars;
    out->cells = plan_toc_cells_for_chars(chars);
}

// --- the disc title, group syntax included ---------------------------------------

typedef struct PlanTocWriter {
    u8 *out;
    u64 cap;
    u64 size;
} PlanTocWriter;

static void plan_toc_put(PlanTocWriter *writer, const u8 *bytes, u64 size) {
    if (writer->size + size > writer->cap) { return; }
    mem_copy(writer->out + writer->size, bytes, size);
    writer->size += size;
}
static void plan_toc_put_lit(PlanTocWriter *writer, const char *text, u64 size) {
    plan_toc_put(writer, (const u8 *)text, size);
}
static void plan_toc_put_u32(PlanTocWriter *writer, u32 value) {
    u8 digits[10];
    u32 count = 0;
    do {
        digits[count] = (u8)('0' + value % 10u);
        count += 1;
        value /= 10u;
    } while (value != 0);
    while (count != 0) {
        count -= 1;
        plan_toc_put(writer, &digits[count], 1);
    }
}

// The groups of a disc in track order. The liveness mask is sparse, the ranges
// are not: sorting by `first` is what makes the compiled string readable.
static u32 plan_toc_group_order(const PlanDisc *disc, u8 *order) {
    u32 count = 0;
    for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
        if (!(disc->group_live & (1u << g)) || disc->groups[g].count == 0) { continue; }
        u32 at = count;
        while (at != 0 && disc->groups[order[at - 1]].first > disc->groups[g].first) {
            order[at] = order[at - 1];
            at -= 1;
        }
        order[at] = (u8)g;
        count += 1;
    }
    return count;
}

u64 plan_toc_compile_disc_title(const Plan *plan, const PlanDisc *disc, u32 cells_budget, u8 *out,
                                u64 cap, u32 *groups_kept) {
    u8 title[PLAN_TITLE_MAX];
    u32 title_chars = 0;
    u64 title_size =
        plan_toc_sanitize(plan_string(plan, disc->title), title, sizeof(title), &title_chars);

    u8 order[PLAN_GROUP_MAX];
    u32 group_count = plan_toc_group_order(disc, order);
    u32 kept = 0;

    PlanTocWriter writer;
    writer.out = out;
    writer.cap = cap;
    writer.size = 0;

    // No group: the raw title, no syntax at all (research/01 s3.10).
    if (group_count == 0) {
        if (plan_toc_cells_for_chars(title_chars) <= cells_budget) {
            plan_toc_put(&writer, title, title_size);
        }
        if (groups_kept) { *groups_kept = 0; }
        return writer.size;
    }

    // "0;<title>//" first, then one group at a time, each one kept only if the
    // whole string still fits: the budget costs groups, never correctness.
    plan_toc_put_lit(&writer, "0;", 2);
    plan_toc_put(&writer, title, title_size);
    plan_toc_put_lit(&writer, "//", 2);
    if (plan_toc_cells_for_chars(plan_toc_halfwidth_len(str8(out, writer.size))) > cells_budget) {
        writer.size = 0;  // even the disc title alone does not fit
    }

    for (u32 i = 0; i < group_count; i += 1) {
        const PlanGroup *group = &disc->groups[order[i]];
        u64 restore = writer.size;
        plan_toc_put_u32(&writer, group->first + 1);
        if (group->count > 1) {
            plan_toc_put_lit(&writer, "-", 1);
            plan_toc_put_u32(&writer, group->first + group->count);
        }
        plan_toc_put_lit(&writer, ";", 1);
        u8 name[PLAN_TITLE_MAX];
        u64 name_size =
            plan_toc_sanitize(plan_string(plan, group->name), name, sizeof(name), 0);
        plan_toc_put(&writer, name, name_size);
        plan_toc_put_lit(&writer, "//", 2);
        if (plan_toc_cells_for_chars(plan_toc_halfwidth_len(str8(out, writer.size))) >
            cells_budget) {
            writer.size = restore;  // this group is dropped, the next may still fit
        } else {
            kept += 1;
        }
    }

    // Nothing but syntax left: fall back to the bare title (s7.4, step 5).
    if (kept == 0) {
        writer.size = 0;
        if (plan_toc_cells_for_chars(title_chars) <= cells_budget) {
            plan_toc_put(&writer, title, title_size);
        }
    }
    if (groups_kept) { *groups_kept = kept; }
    return writer.size;
}

void plan_toc_budget(const Plan *plan, const Library *lib, u32 disc_index, PlanTocBudget *out) {
    const PlanDisc *disc = &plan->discs[disc_index];
    StructZero(out);
    out->cells_total = PLAN_TOC_CELLS;

    // The tracks are counted first: they are the part the user cannot drop, so
    // the disc title and its groups compile into whatever is left over.
    u32 track_cells = 0;
    for (u32 i = 0; i < disc->entry_count; i += 1) {
        b32 non_sp = disc->mode[i] != PlanMode_SP;
        String8 title = plan_entry_title(plan, lib, disc_index, i);
        u32 cells = plan_toc_cells_for_title(title, non_sp);
        out->cells[i] = cells;
        track_cells += cells;
    }
    out->cells_tracks = track_cells;
    out->overflow = track_cells > PLAN_TOC_CELLS;

    u32 disc_budget = (track_cells < PLAN_TOC_CELLS) ? PLAN_TOC_CELLS - track_cells : 0;
    out->raw_size = plan_toc_compile_disc_title(plan, disc, disc_budget, out->raw,
                                                sizeof(out->raw), &out->groups_kept);
    u8 order[PLAN_GROUP_MAX];
    out->groups_total = plan_toc_group_order(disc, order);

    out->cells_disc = plan_toc_cells_for_chars(plan_toc_halfwidth_len(str8(out->raw, out->raw_size)));
    out->cells_used = track_cells + out->cells_disc;
    out->cells_free =
        (out->cells_used < PLAN_TOC_CELLS) ? PLAN_TOC_CELLS - out->cells_used : 0;
    out->chars_free = out->cells_free * PLAN_TOC_CELL_CHARS;
}

// --- automatic titling -------------------------------------------------------------

u64 plan_toc_format(u32 template_id, String8 artist, String8 title, u32 number, u8 *out, u64 cap) {
    PlanTocWriter writer;
    writer.out = out;
    writer.cap = cap;
    writer.size = 0;
    if (template_id == PlanTitleTemplate_ArtistTitle && artist.size != 0) {
        plan_toc_put(&writer, artist.str, artist.size);
        plan_toc_put_lit(&writer, " - ", 3);
    } else if (template_id == PlanTitleTemplate_NumberTitle) {
        plan_toc_put_u32(&writer, number);
        plan_toc_put_lit(&writer, ". ", 2);
    }
    plan_toc_put(&writer, title.str, title.size);
    return writer.size;
}

static u32 plan_toc_album_of(const Library *lib, const PlanDisc *disc, u32 index) {
    if (!lib) { return 0; }
    u32 id = disc->track_id[index];
    return lib_track_live(lib, id) ? lib->album_id[id] : 0;
}
static u32 plan_toc_artist_of(const Library *lib, const PlanDisc *disc, u32 index) {
    if (!lib) { return 0; }
    u32 id = disc->track_id[index];
    if (!lib_track_live(lib, id)) { return 0; }
    u32 artist = lib->album_artist_id[id];
    return artist ? artist : lib->artist_id[id];
}

String8 plan_toc_propose_disc_title(const Plan *plan, const Library *lib, u32 disc_index) {
    const PlanDisc *disc = &plan->discs[disc_index];
    String8 empty = str8(0, 0);
    if (disc->entry_count == 0 || !lib) { return empty; }

    // One pass, Boyer-Moore majority: the album that survives it is the only
    // candidate that can hold more than half the entries, and a second count
    // confirms it. No sorting, no table, no allocation.
    u32 candidate = plan_toc_album_of(lib, disc, 0);
    u32 votes = 0;
    b32 same_album = 1, same_artist = 1;
    u32 first_artist = plan_toc_artist_of(lib, disc, 0);
    u32 first_album = candidate;
    for (u32 i = 0; i < disc->entry_count; i += 1) {
        u32 album = plan_toc_album_of(lib, disc, i);
        if (album != first_album) { same_album = 0; }
        if (plan_toc_artist_of(lib, disc, i) != first_artist) { same_artist = 0; }
        if (votes == 0) {
            candidate = album;
            votes = 1;
        } else if (album == candidate) {
            votes += 1;
        } else {
            votes -= 1;
        }
    }
    if (same_album && first_album != 0) { return lib_string(&lib->strings, first_album); }
    if (same_artist && first_artist != 0) { return lib_string(&lib->strings, first_artist); }
    if (candidate == 0) { return empty; }
    u32 count = 0;
    for (u32 i = 0; i < disc->entry_count; i += 1) {
        if (plan_toc_album_of(lib, disc, i) == candidate) { count += 1; }
    }
    if (count * 2 > disc->entry_count) { return lib_string(&lib->strings, candidate); }
    return empty;
}

u32 plan_toc_propose_groups(const Plan *plan, const Library *lib, u32 disc_index,
                            PlanProposedGroup *out, u32 cap) {
    const PlanDisc *disc = &plan->discs[disc_index];
    u32 written = 0;
    u32 i = 0;
    while (i < disc->entry_count && written < cap) {
        u32 album = plan_toc_album_of(lib, disc, i);
        u32 run = 1;
        while (i + run < disc->entry_count && plan_toc_album_of(lib, disc, i + run) == album) {
            run += 1;
        }
        // A run of one is not a group: a disc of singles would end up with one
        // group per track and no budget left for the titles.
        if (album != 0 && run > 1) {
            out[written].first = i;
            out[written].count = run;
            out[written].name = lib_string(&lib->strings, album);
            written += 1;
        }
        i += run;
    }
    return written;
}
