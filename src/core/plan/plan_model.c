#include "plan_model.h"

// --- the document ------------------------------------------------------------

static void plan_disc_init(PlanDisc *disc) {
    StructZero(disc);
    disc->length_min = PLAN_LENGTH_DEFAULT;
    disc->default_mode = PlanMode_SP;
    mem_set(disc->group_id, PLAN_GROUP_NONE, sizeof(disc->group_id));
}

void plan_init(Plan *plan, Arena *arena, Arena *text) {
    StructZero(plan);
    plan->arena = arena;
    plan->text = text;
    lib_strings_init(&plan->strings, arena, text);
    plan->disc_count = 1;
    plan_disc_init(&plan->discs[0]);
}

void plan_clear(Plan *plan) {
    // The intern table is kept: clearing it would mean clearing arenas the
    // caller owns, and a plan the user emptied is about to be filled again.
    plan->title = LIB_STRING_NONE;
    plan->disc_count = 1;
    for (u32 i = 0; i < PLAN_DISC_MAX; i += 1) { plan_disc_init(&plan->discs[i]); }
    plan->first = plan->done = plan->last = 0;
    plan->coalesce_open = 0;
    plan->revision += 1;
    plan->dirty = 0;
    plan->dirty_us = 0;
}

// --- entries -----------------------------------------------------------------

PlanEntry plan_entry(const PlanDisc *disc, u32 index) {
    Assert(index < disc->entry_count);
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = disc->track_id[index];
    entry.path_id = disc->path_id[index];
    entry.title_override = disc->title_override[index];
    entry.duration_ms = disc->duration_ms[index];
    entry.gain_db = disc->gain_db[index];
    entry.fade_in_ms = disc->fade_in_ms[index];
    entry.fade_out_ms = disc->fade_out_ms[index];
    entry.trim_head_ms = disc->trim_head_ms[index];
    entry.trim_tail_ms = disc->trim_tail_ms[index];
    entry.mode = disc->mode[index];
    entry.flags = disc->flags[index];
    entry.group_id = disc->group_id[index];
    return entry;
}

void plan_entry_store(PlanDisc *disc, u32 index, const PlanEntry *entry) {
    Assert(index < PLAN_ENTRY_MAX);
    disc->track_id[index] = entry->track_id;
    disc->path_id[index] = entry->path_id;
    disc->title_override[index] = entry->title_override;
    disc->duration_ms[index] = entry->duration_ms;
    disc->gain_db[index] = entry->gain_db;
    disc->fade_in_ms[index] = entry->fade_in_ms;
    disc->fade_out_ms[index] = entry->fade_out_ms;
    disc->trim_head_ms[index] = entry->trim_head_ms;
    disc->trim_tail_ms[index] = entry->trim_tail_ms;
    disc->mode[index] = entry->mode;
    disc->flags[index] = entry->flags;
    disc->group_id[index] = entry->group_id;
}

// Membership lives in the column; the range is a view of it, recomputed after
// every structural change. An empty live group keeps its name: only Ungroup
// destroys a group, so undoing the removal of its last entry finds it intact.
void plan_groups_refresh(PlanDisc *disc) {
    for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
        if (!(disc->group_live & (1u << g))) { continue; }
        u32 first = disc->entry_count;
        u32 count = 0;
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            if (disc->group_id[i] != g) { continue; }
            if (count == 0) { first = i; }
            count += 1;
        }
        disc->groups[g].first = (u16)(count ? first : 0);
        disc->groups[g].count = (u16)count;
    }
}

u64 plan_disc_duration_ms(const PlanDisc *disc) {
    u64 total = 0;
    for (u32 i = 0; i < disc->entry_count; i += 1) { total += disc->duration_ms[i]; }
    return total;
}

u64 plan_duration_ms(const Plan *plan) {
    u64 total = 0;
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        total += plan_disc_duration_ms(&plan->discs[d]);
    }
    return total;
}

u32 plan_entry_count(const Plan *plan) {
    u32 total = 0;
    for (u32 d = 0; d < plan->disc_count; d += 1) { total += plan->discs[d].entry_count; }
    return total;
}

String8 plan_entry_title(const Plan *plan, const Library *lib, u32 disc_index, u32 index) {
    const PlanDisc *disc = &plan->discs[disc_index];
    Assert(index < disc->entry_count);
    if (disc->title_override[index] != LIB_STRING_NONE) {
        return plan_string(plan, disc->title_override[index]);
    }
    TrackId id = disc->track_id[index];
    if (lib && lib_track_live(lib, id)) {
        String8 title = lib_string(&lib->strings, lib->title_id[id]);
        if (title.size != 0) { return title; }
    }
    return os_path_filename(plan_string(plan, disc->path_id[index]));
}

// --- resolution ---------------------------------------------------------------

u32 plan_resolve(Plan *plan, const Library *lib) {
    u32 missing = 0;
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        PlanDisc *disc = &plan->discs[d];
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            String8 path = plan_string(plan, disc->path_id[i]);
            TrackId id = disc->track_id[i];
            // The id is a bet on the library not having been rebuilt; the path
            // is the truth. Trust the id only while it still names that path.
            b32 ok = lib_track_live(lib, id) && str8_eq(lib_track_path(lib, id), path);
            if (!ok) {
                id = lib_find_by_path(lib, path);
                ok = (id != LIB_TRACK_NONE);
            }
            if (ok) {
                disc->track_id[i] = id;
                disc->flags[i] = (u8)(disc->flags[i] & ~(u32)PlanEntryFlag_Missing);
                if (disc->duration_ms[i] == 0) { disc->duration_ms[i] = lib->duration_ms[id]; }
            } else {
                disc->track_id[i] = LIB_TRACK_NONE;
                disc->flags[i] = (u8)(disc->flags[i] | PlanEntryFlag_Missing);
                missing += 1;
            }
        }
    }
    plan->revision += 1;
    return missing;
}

// --- events --------------------------------------------------------------------

b32 plan_events_next(PlanEventQueue *queue, PlanEvent *out) {
    if (queue->read == queue->write) { return 0; }
    mem_copy(out, &queue->items[queue->read & (PLAN_EVENT_CAPACITY - 1)], sizeof(PlanEvent));
    queue->read += 1;
    return 1;
}
