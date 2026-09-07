// netmd_edit.c - see netmd_edit.h. Simulation first, then the writes.
#include "netmd_edit.h"

// --- text (s3.11, write direction) --------------------------------------------

u64 netmd_utf8_to_sjis(String8 in, u8 *out, u64 cap) {
    // plan_toc_sanitize is the ONE place that decides what a title may contain
    // (ADR-011 D3): after it, the string is ASCII 0x20..0x7E and half-width
    // katakana, so the encoding below is arithmetic and needs no table. A
    // sanitize step skipped here is a corrupt TOC (pitfall 39).
    u8 clean[PLAN_TITLE_MAX];
    u64 clean_size = plan_toc_sanitize(in, clean, sizeof(clean), 0);
    u64 written = 0;
    u64 at = 0;
    while (at < clean_size && written < cap) {
        UnicodeDecode decoded = utf8_decode(clean + at, clean_size - at);
        at += decoded.advance;
        u32 cp = decoded.codepoint;
        if (cp < 0x80u) {
            out[written] = (u8)cp;
            written += 1;
        } else if (cp >= 0xFF61u && cp <= 0xFF9Fu) {
            // The half-width katakana block, one Shift-JIS byte each (s3.11).
            out[written] = (u8)(0xA1u + (cp - 0xFF61u));
            written += 1;
        }
        // Nothing else can come out of the sanitizer, so there is no third case
        // to drop: a codepoint here would be a bug in plan_toc_sanitize.
    }
    return written;
}

u32 netmd_mask_count(const u32 *mask, u32 track_count) {
    u32 count = 0;
    for (u32 i = 0; i < track_count; i += 1) {
        if (netmd_mask_get(mask, i)) { count += 1; }
    }
    return count;
}

// --- the budget, borrowed whole from plan_toc (ADR-011 D3) ---------------------

static u32 netmd_layout_groups(const DiscLayout *layout, PlanTocGroup *out) {
    u32 count = Min(layout->group_count, (u32)NETMD_GROUP_MAX);
    for (u32 i = 0; i < count; i += 1) {
        out[i].first = layout->groups[i].first;
        out[i].count = layout->groups[i].count;
        out[i].name = str8((u8 *)layout->groups[i].name, layout->groups[i].name_size);
    }
    return count;
}

u32 netmd_layout_cells(const DiscLayout *layout, u8 *raw_out, u64 raw_cap, u64 *raw_size_out) {
    u32 track_cells = 0;
    for (u32 i = 0; i < layout->track_count; i += 1) {
        const NetmdTrack *track = &layout->tracks[i];
        // s7.4: a non-SP track costs a cell even untitled - the device writes
        // its own "LP: " prefix into the title space.
        b32 non_sp = (track->encoding != NetmdEncoding_SP);
        track_cells += plan_toc_cells_for_title(str8((u8 *)track->title, track->title_size),
                                                non_sp);
    }
    PlanTocGroup groups[NETMD_GROUP_MAX];
    u32 group_count = netmd_layout_groups(layout, groups);
    u8 raw[PLAN_TOC_RAW_MAX];
    // The whole pot as the budget on purpose: a compilation that silently drops
    // groups would hide the overflow the caller has to refuse on (s7.4 says to
    // drop groups, D4 says to say so first).
    u64 raw_size = plan_toc_compile_raw(str8((u8 *)layout->title, layout->title_size), groups,
                                        group_count, PLAN_TOC_CELLS, raw, sizeof(raw), 0);
    if (raw_out) {
        u64 copied = Min(raw_size, raw_cap);
        if (copied != 0) { mem_copy(raw_out, raw, copied); }
        if (raw_size_out) { *raw_size_out = copied; }
    } else if (raw_size_out) {
        *raw_size_out = raw_size;
    }
    return track_cells + plan_toc_cells_for_chars(plan_toc_halfwidth_len(str8(raw, raw_size)));
}

// --- the simulation (ADR-011 D4) ------------------------------------------------

static void netmd_diff_set_before(DiscDiff *diff, String8 text) {
    diff->before_size = (u16)Min(text.size, (u64)NETMD_DISC_TITLE_MAX);
    if (diff->before_size != 0) { mem_copy(diff->before, text.str, diff->before_size); }
}
static void netmd_diff_set_after(DiscDiff *diff, String8 text) {
    diff->after_size = (u16)Min(text.size, (u64)NETMD_DISC_TITLE_MAX);
    if (diff->after_size != 0) { mem_copy(diff->after, text.str, diff->after_size); }
}

// A title as it will really be stored: sanitized, so what the diff shows is
// what the device will show (D3).
static String8 netmd_edit_clean(String8 in, u8 *out, u64 cap) {
    return str8(out, plan_toc_sanitize(in, out, cap, 0));
}

// Rebuilds `groups` from the per-track ids after tracks moved or went away.
// Groups are ranges of positions in the TOC (s3.10), so a group is exactly the
// run of consecutive tracks carrying its id; a group that lost every track
// disappears, which is what makes "erase the last track of a group" work.
static void netmd_layout_regroup(DiscLayout *layout, const u8 *ids, const NetmdGroup *names) {
    NetmdGroup rebuilt[NETMD_GROUP_MAX];
    u32 count = 0;
    u32 ungrouped = 0;
    for (u32 i = 0; i < layout->track_count;) {
        u8 id = ids[i];
        if (id == NETMD_NO_GROUP) {
            layout->tracks[i].group = NETMD_NO_GROUP;
            ungrouped += 1;
            i += 1;
            continue;
        }
        u32 run = i;
        while (run < layout->track_count && ids[run] == id) { run += 1; }
        if (count < NETMD_GROUP_MAX) {
            NetmdGroup *group = &rebuilt[count];
            group->first = (u16)i;
            group->count = (u16)(run - i);
            group->name_size = names[id].name_size;
            if (group->name_size != 0) {
                mem_copy(group->name, names[id].name, group->name_size);
            }
            for (u32 t = i; t < run; t += 1) { layout->tracks[t].group = (u8)count; }
            count += 1;
        } else {
            for (u32 t = i; t < run; t += 1) { layout->tracks[t].group = NETMD_NO_GROUP; }
            ungrouped += run - i;
        }
        i = run;
    }
    for (u32 i = 0; i < count; i += 1) {
        NetmdGroup *dst = &layout->groups[i];
        dst->first = rebuilt[i].first;
        dst->count = rebuilt[i].count;
        dst->name_size = rebuilt[i].name_size;
        if (dst->name_size != 0) { mem_copy(dst->name, rebuilt[i].name, dst->name_size); }
    }
    layout->group_count = count;
    layout->ungrouped_count = ungrouped;
}

// The per-track group ids of a layout, which is the form every reordering works
// in: a range survives a move or an erasure, an index into `groups` does not.
static void netmd_layout_ids(const DiscLayout *layout, u8 *ids) {
    for (u32 i = 0; i < layout->track_count; i += 1) { ids[i] = layout->tracks[i].group; }
}

static void netmd_edit_refuse(DiscDiff *diff, u32 refusal) {
    diff->allowed = 0;
    diff->refusal = refusal;
}

void netmd_edit_simulate(const DiscLayout *before, const NetmdEditRequest *request,
                         DiscLayout *after, DiscDiff *out) {
    StructZero(out);
    out->kind = request->kind;
    out->allowed = 1;
    out->tracks_before = before->track_count;
    out->tracks_after = before->track_count;
    out->groups_before = before->group_count;
    out->groups_after = before->group_count;

    DiscLayout *work = after;
    u8 ids[NETMD_TRACK_MAX];
    NetmdGroup names[NETMD_GROUP_MAX];
    if (work) {
        mem_copy(work, before, sizeof(DiscLayout));
        netmd_layout_ids(before, ids);
        for (u32 i = 0; i < NETMD_GROUP_MAX; i += 1) {
            names[i].name_size = before->groups[i].name_size;
            if (names[i].name_size != 0) {
                mem_copy(names[i].name, before->groups[i].name, names[i].name_size);
            }
            names[i].first = 0;
            names[i].count = 0;
        }
    }

    u8 raw_before[PLAN_TOC_RAW_MAX];
    u64 raw_before_size = 0;
    out->cells_before = netmd_layout_cells(before, raw_before, sizeof(raw_before),
                                           &raw_before_size);
    out->cells_after = out->cells_before;

    // The two refusals that need no arithmetic at all (s7.5).
    if ((before->flags & NetmdDiscFlag_Present) == 0) {
        netmd_edit_refuse(out, NetmdEditRefusal_NoDisc);
        return;
    }
    if ((before->flags & NetmdDiscFlag_WriteProtected) != 0 ||
        (before->flags & NetmdDiscFlag_Writable) == 0) {
        netmd_edit_refuse(out, NetmdEditRefusal_Protected);
        return;
    }
    if (!work) {
        // Only a legality answer was wanted; everything below needs the copy.
        return;
    }

    u8 clean[PLAN_TITLE_MAX];
    String8 title = netmd_edit_clean(str8((u8 *)request->title, request->title_size), clean,
                                     sizeof(clean));

    switch (request->kind) {
        case NetmdEditKind_RenameDisc: {
            String8 current = str8((u8 *)before->title, before->title_size);
            netmd_diff_set_before(out, current);
            netmd_diff_set_after(out, title);
            if (str8_eq(current, title)) {
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            work->title_size = (u16)title.size;
            if (title.size != 0) { mem_copy(work->title, title.str, title.size); }
            out->changed = 1;
            out->writes = 1;
        } break;

        case NetmdEditKind_RenameTrack: {
            if (request->track >= before->track_count) {
                netmd_edit_refuse(out, NetmdEditRefusal_Range);
                return;
            }
            const NetmdTrack *track = &before->tracks[request->track];
            String8 current = str8((u8 *)track->title, track->title_size);
            netmd_diff_set_before(out, current);
            netmd_diff_set_after(out, title);
            if (str8_eq(current, title)) {
                // Pitfall 10: writing a title a machine already holds hangs some
                // of them. There is nothing to do here anyway.
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            work->tracks[request->track].title_size = (u16)title.size;
            if (title.size != 0) {
                mem_copy(work->tracks[request->track].title, title.str, title.size);
            }
            out->changed = 1;
            out->writes = 1;
        } break;

        case NetmdEditKind_MoveTrack: {
            u32 from = request->track;
            u32 to = request->dest;
            if (from >= before->track_count || to >= before->track_count) {
                netmd_edit_refuse(out, NetmdEditRefusal_Range);
                return;
            }
            if (from == to) {
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            NetmdTrack moved;
            mem_copy(&moved, &before->tracks[from], sizeof(NetmdTrack));
            u8 moved_id = ids[from];
            u32 count = before->track_count;
            // One array, one hole, one insertion. mem_move and not a loop: under
            // /GL a copy loop over a struct becomes a memcpy call the linker
            // cannot resolve (CONVENTIONS.md).
            if (from < count - 1) {
                mem_move(&work->tracks[from], &work->tracks[from + 1],
                         (count - 1 - from) * sizeof(NetmdTrack));
                mem_move(&ids[from], &ids[from + 1], (count - 1 - from));
            }
            if (to < count - 1) {
                mem_move(&work->tracks[to + 1], &work->tracks[to],
                         (count - 1 - to) * sizeof(NetmdTrack));
                mem_move(&ids[to + 1], &ids[to], (count - 1 - to));
            }
            mem_copy(&work->tracks[to], &moved, sizeof(NetmdTrack));
            // Groups are position ranges (s3.10), so a track dropped inside one
            // joins it; a track that stays inside its own keeps it. The
            // neighbour on the right is what the drop line points at.
            u8 left = (to > 0) ? ids[to - 1] : NETMD_NO_GROUP;
            u8 right = (to + 1 < count) ? ids[to + 1] : NETMD_NO_GROUP;
            u8 adopted = right;
            if (moved_id != NETMD_NO_GROUP && (moved_id == left || moved_id == right)) {
                adopted = moved_id;
            } else if (right == NETMD_NO_GROUP) {
                adopted = left;
            }
            ids[to] = adopted;
            netmd_layout_regroup(work, ids, names);
            out->changed = 1;
            out->writes = 1;
        } break;

        case NetmdEditKind_EraseTracks: {
            u32 selected = netmd_mask_count(request->mask, before->track_count);
            if (selected == 0) {
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            for (u32 i = 0; i < before->track_count; i += 1) {
                // s7.5: a track checked out by SonicStage refuses eraseTrack.
                if (netmd_mask_get(request->mask, i) && before->tracks[i].protect) {
                    netmd_edit_refuse(out, NetmdEditRefusal_TrackProtected);
                    return;
                }
            }
            u32 kept = 0;
            for (u32 i = 0; i < before->track_count; i += 1) {
                if (netmd_mask_get(request->mask, i)) { continue; }
                if (kept != i) {
                    mem_copy(&work->tracks[kept], &before->tracks[i], sizeof(NetmdTrack));
                    ids[kept] = ids[i];
                }
                kept += 1;
            }
            work->track_count = kept;
            netmd_layout_regroup(work, ids, names);
            out->changed = selected;
            out->writes = selected;
        } break;

        case NetmdEditKind_EraseDisc: {
            if (before->track_count == 0 && before->title_size == 0) {
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            work->track_count = 0;
            work->group_count = 0;
            work->ungrouped_count = 0;
            work->title_size = 0;
            work->title_full_size = 0;
            work->raw_title_size = 0;
            work->flags |= NetmdDiscFlag_Empty;
            out->changed = before->track_count;
            out->writes = 1;
        } break;

        case NetmdEditKind_CreateGroup: {
            u32 selected = netmd_mask_count(request->mask, before->track_count);
            if (selected == 0) {
                netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
                return;
            }
            u32 first = before->track_count;
            u32 last = 0;
            for (u32 i = 0; i < before->track_count; i += 1) {
                if (!netmd_mask_get(request->mask, i)) { continue; }
                if (i < first) { first = i; }
                last = i;
                // s3.10: one group per track. Taking a track out of the group it
                // is in would split that group's range, so it is refused with a
                // sentence rather than silently corrupting the syntax.
                if (before->tracks[i].group != NETMD_NO_GROUP) {
                    netmd_edit_refuse(out, NetmdEditRefusal_Grouped);
                    return;
                }
            }
            if (last - first + 1 != selected || before->group_count >= NETMD_GROUP_MAX) {
                netmd_edit_refuse(out, NetmdEditRefusal_Range);
                return;
            }
            // A free id in `names`: the run gets it, regroup gives it a rank.
            u8 id = (u8)Min(before->group_count, (u32)(NETMD_GROUP_MAX - 1));
            for (u32 i = 0; i < NETMD_GROUP_MAX; i += 1) {
                b32 used = 0;
                for (u32 t = 0; t < before->track_count; t += 1) {
                    if (ids[t] == (u8)i) { used = 1; }
                }
                if (!used) {
                    id = (u8)i;
                    break;
                }
            }
            names[id].name_size = (u16)Min(title.size, (u64)NETMD_TITLE_MAX);
            if (names[id].name_size != 0) { mem_copy(names[id].name, title.str, names[id].name_size); }
            // mem_set and not a loop: under /GL MSVC turns a byte fill into a
            // memset call the linker cannot resolve (CONVENTIONS.md).
            mem_set(&ids[first], id, last - first + 1u);
            netmd_layout_regroup(work, ids, names);
            // What changes here is the compiled disc title, not one name: the
            // fallback below puts the whole "0;...//1-4;...//" side by side.
            out->changed = selected;
            out->writes = 1;
        } break;

        case NetmdEditKind_DissolveGroup: {
            if (request->track >= before->group_count) {
                netmd_edit_refuse(out, NetmdEditRefusal_Range);
                return;
            }
            const NetmdGroup *group = &before->groups[request->track];
            for (u32 i = 0; i < before->track_count; i += 1) {
                if (ids[i] == (u8)request->track) { ids[i] = NETMD_NO_GROUP; }
            }
            netmd_layout_regroup(work, ids, names);
            out->changed = group->count;
            out->writes = 1;
        } break;

        default: {
            netmd_edit_refuse(out, NetmdEditRefusal_Nothing);
            return;
        }
    }

    u8 raw_after[PLAN_TOC_RAW_MAX];
    u64 raw_after_size = 0;
    out->cells_after = netmd_layout_cells(work, raw_after, sizeof(raw_after), &raw_after_size);
    out->tracks_after = work->track_count;
    out->groups_after = work->group_count;
    // For everything that is not a rename, what changes and what the panel puts
    // side by side is the compiled disc title: the groups are in it (s3.10).
    if (out->before_size == 0 && out->after_size == 0) {
        netmd_diff_set_before(out, str8(raw_before, raw_before_size));
        netmd_diff_set_after(out, str8(raw_after, raw_after_size));
    }
    // A move or an erasure renumbers the ranges, so the disc title has to be
    // rewritten as well - and only then (s6.5: one write, not one per track).
    if (request->kind != NetmdEditKind_RenameDisc && request->kind != NetmdEditKind_RenameTrack &&
        request->kind != NetmdEditKind_EraseDisc &&
        !(raw_after_size == raw_before_size &&
          mem_cmp(raw_after, raw_before, raw_after_size) == 0)) {
        out->writes += 1;
    }
    if (out->cells_after > PLAN_TOC_CELLS) {
        netmd_edit_refuse(out, NetmdEditRefusal_Budget);
        return;
    }
    out->cells_free_after = PLAN_TOC_CELLS - out->cells_after;
    out->chars_free_after = out->cells_free_after * PLAN_TOC_CELL_CHARS;
}

// --- the writes (s3.9, s3.12) ---------------------------------------------------

// s3.9 pt 3: close, open for writing, write, close, open for reading, close.
// The double round trip is what makes some machines commit their TOC cache; the
// open is advisory and the close is not (s3.4).
static u32 netmd_write_title(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc,
                             u32 wchar, u32 track, b32 is_disc, String8 sjis, u32 old_len) {
    netmd_descriptor_close(session, arena, desc);
    netmd_descriptor_open(session, arena, desc, NETMD_DESC_OPEN_WRITE);
    String8 reply;
    u32 result;
    if (is_disc) {
        result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1807 02201801 00%b 3000 0a00 5000 %w 0000 %w %*", wchar,
                               (u32)sjis.size, old_len, sjis.str, (u32)sjis.size);
    } else {
        result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1807 022018%b %w 3000 0a00 5000 %w 0000 %w %*", wchar, track,
                               (u32)sjis.size, old_len, sjis.str, (u32)sjis.size);
    }
    netmd_descriptor_close(session, arena, desc);
    netmd_descriptor_open(session, arena, desc, NETMD_DESC_OPEN_READ);
    netmd_descriptor_close(session, arena, desc);
    // s6.2: between two TOC edits the device is writing. Do not interrupt it.
    netmd_session_hold(session, NETMD_EDIT_HOLD_MS);
    return result;
}

u32 netmd_set_disc_title(NetmdSession *session, Arena *arena, String8 title, b32 *out_written) {
    *out_written = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    u8 *sjis = push_array(arena, u8, NETMD_DISC_TITLE_MAX);
    u64 sjis_size = netmd_utf8_to_sjis(title, sjis, NETMD_DISC_TITLE_MAX);

    String8 old_text = str8(0, 0);
    String8 old_sjis = str8(0, 0);
    u32 result = netmd_get_disc_title_ex(session, arena, 0, &old_text, &old_sjis);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    // Pitfall 9: `oldLen` is the length of what the TOC holds *right now*, in
    // Shift-JIS bytes, read back a moment ago. Anything else corrupts it.
    if (old_sjis.size == sjis_size && mem_cmp(old_sjis.str, sjis, sjis_size) == 0) {
        arena_temp_end(scratch);
        return NetmdResult_Ok;  // pitfall 10: an identical title is not written
    }

    // s3.9 pt 2: Sharp renames the disc through the track title descriptor.
    const NetmdDescriptor *desc = (session->vid == NETMD_VID_SHARP) ? &netmd_desc_utoc1
                                                                   : &netmd_desc_disc_title;
    result = netmd_write_title(session, arena, desc, NETMD_WCHAR_DISC_HALF, 0, 1,
                               str8(sjis, sjis_size), (u32)old_sjis.size);
    if (result == NetmdResult_Ok) { *out_written = 1; }

    // Every title this application writes is half-width (D3 sanitizes to it), so
    // a full-width title left over from another program would be what the device
    // displays instead of what we just wrote. It is cleared, not rewritten: one
    // TOC cell freed and no second syntax to keep in step (s3.10).
    if (result == NetmdResult_Ok) {
        String8 full_text = str8(0, 0);
        String8 full_sjis = str8(0, 0);
        if (netmd_get_disc_title_ex(session, arena, 1, &full_text, &full_sjis) ==
                    NetmdResult_Ok &&
            full_sjis.size != 0) {
            netmd_write_title(session, arena, desc, NETMD_WCHAR_DISC_FULL, 0, 1, str8(sjis, 0),
                              (u32)full_sjis.size);
        }
    }
    arena_temp_end(scratch);
    return result;
}

u32 netmd_set_track_title(NetmdSession *session, Arena *arena, u32 track, String8 title,
                          b32 *out_written) {
    *out_written = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    u8 *sjis = push_array(arena, u8, NETMD_TITLE_MAX);
    u64 sjis_size = netmd_utf8_to_sjis(title, sjis, NETMD_TITLE_MAX);

    String8 old_text = str8(0, 0);
    String8 old_sjis = str8(0, 0);
    // Pitfall 9 again, and s3.8.2: an untitled track answers REJECTED, which is
    // an empty title and therefore oldLen = 0.
    u32 result = netmd_get_track_title_ex(session, arena, track, 0, &old_text, &old_sjis);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    if (old_sjis.size == sjis_size && mem_cmp(old_sjis.str, sjis, sjis_size) == 0) {
        arena_temp_end(scratch);
        return NetmdResult_Ok;
    }
    result = netmd_write_title(session, arena, &netmd_desc_utoc1, NETMD_WCHAR_TRACK_HALF, track,
                               0, str8(sjis, sjis_size), (u32)old_sjis.size);
    if (result == NetmdResult_Ok) { *out_written = 1; }
    if (result == NetmdResult_Ok) {
        String8 full_text = str8(0, 0);
        String8 full_sjis = str8(0, 0);
        if (netmd_get_track_title_ex(session, arena, track, 1, &full_text, &full_sjis) ==
                    NetmdResult_Ok &&
            full_sjis.size != 0) {
            netmd_write_title(session, arena, &netmd_desc_utoc4, NETMD_WCHAR_TRACK_FULL, track, 0,
                              str8(sjis, 0), (u32)full_sjis.size);
        }
    }
    arena_temp_end(scratch);
    return result;
}

u32 netmd_move_track(NetmdSession *session, Arena *arena, u32 from, u32 to) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_TRANSPORT_MS, &reply,
                               "00 1843 ff00 00 201001 %w 201001 %w", from, to);
    // s6.2: the TOC is being rewritten; nothing may be listed for half a second.
    netmd_session_hold(session, NETMD_EDIT_RELIST_HOLD_MS);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_erase_track(NetmdSession *session, Arena *arena, u32 track) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_TRANSPORT_MS, &reply,
                               "00 1840 ff01 00 201001 %w", track);
    netmd_session_hold(session, NETMD_EDIT_RELIST_HOLD_MS);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_erase_disc(NetmdSession *session, Arena *arena) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_TRANSPORT_MS, &reply,
                               "00 1840 ff 0000");
    netmd_session_hold(session, NETMD_EDIT_RELIST_HOLD_MS);
    arena_temp_end(scratch);
    return result;
}

// --- one edit, end to end (s6.5) -------------------------------------------------

// The disc title of `after`, compiled with its groups, written once. This is the
// anti-pattern of s6.5 turned around: never once per track, always once at the
// end of everything else.
static u32 netmd_write_compiled_disc_title(NetmdSession *session, Arena *arena,
                                           const DiscLayout *after, u32 *writes) {
    ArenaTemp scratch = arena_temp_begin(arena);
    u8 *raw = push_array(arena, u8, PLAN_TOC_RAW_MAX);
    u64 raw_size = 0;
    netmd_layout_cells(after, raw, PLAN_TOC_RAW_MAX, &raw_size);
    b32 written = 0;
    u32 result = netmd_set_disc_title(session, arena, str8(raw, raw_size), &written);
    if (written) { *writes += 1; }
    arena_temp_end(scratch);
    return result;
}

u32 netmd_edit_apply(NetmdSession *session, Arena *arena, const DiscLayout *after,
                     const NetmdEditRequest *request, u32 *out_writes) {
    u32 writes = 0;
    // s3.5: the player's own buttons are silenced for the length of the edit,
    // and the release below happens on every path - an acquire without one
    // leaves the machine showing "PC --> MD" until it is unplugged.
    u32 result = netmd_acquire(session, arena);
    if (result == NetmdResult_NotImplemented || result == NetmdResult_Rejected) {
        result = NetmdResult_Ok;  // not every machine has the command; s3.5
    }

    if (result == NetmdResult_Ok) {
        switch (request->kind) {
            case NetmdEditKind_RenameDisc:
            case NetmdEditKind_CreateGroup:
            case NetmdEditKind_DissolveGroup: {
                result = netmd_write_compiled_disc_title(session, arena, after, &writes);
            } break;

            case NetmdEditKind_RenameTrack: {
                b32 written = 0;
                String8 title = str8((u8 *)after->tracks[request->track].title,
                                     after->tracks[request->track].title_size);
                result = netmd_set_track_title(session, arena, request->track, title, &written);
                if (written) { writes += 1; }
            } break;

            case NetmdEditKind_MoveTrack: {
                result = netmd_move_track(session, arena, request->track, request->dest);
                if (result == NetmdResult_Ok) { writes += 1; }
                if (result == NetmdResult_Ok) {
                    // s3.10: the ranges in the disc title are not renumbered by
                    // the device. They are ours to rewrite, once.
                    result = netmd_write_compiled_disc_title(session, arena, after, &writes);
                }
            } break;

            case NetmdEditKind_EraseTracks: {
                // Pitfall 17: erasing shifts every higher index down, so the
                // selection is erased from the end backwards and the indices
                // stay the ones the simulation was made with.
                u32 index = after->track_count + netmd_mask_count(request->mask, NETMD_TRACK_MAX);
                while (index != 0 && result == NetmdResult_Ok) {
                    index -= 1;
                    if (!netmd_mask_get(request->mask, index)) { continue; }
                    result = netmd_erase_track(session, arena, index);
                    if (result == NetmdResult_Ok) { writes += 1; }
                }
                if (result == NetmdResult_Ok) {
                    result = netmd_write_compiled_disc_title(session, arena, after, &writes);
                }
            } break;

            case NetmdEditKind_EraseDisc: {
                result = netmd_erase_disc(session, arena);
                if (result == NetmdResult_Ok) { writes += 1; }
            } break;

            default: result = NetmdResult_Rejected; break;
        }
    }

    u32 released = netmd_release(session, arena);
    if (result == NetmdResult_Ok && released == NetmdResult_Usb) { result = released; }
    if (out_writes) { *out_writes = writes; }
    return result;
}
