// plan_cmd.c - the only way to change a plan. Every command carries what it
// takes to undo it, so the stack holds no snapshots and no diff of the whole
// document: 256 records of 80 bytes, and an exact inverse for each (B-25).
//
// The rule that keeps this honest: plan_apply is the single mutator, and each
// kind has exactly one do and one undo below. A command that cannot apply -
// a full disc, a group that straddles a cut - returns 0 and changes nothing;
// it is a refusal from the boundary, not an error to be propagated.
#include "plan_model.h"

// --- small helpers ------------------------------------------------------------

static void plan_entry_insert(PlanDisc *disc, u32 index, const PlanEntry *entry) {
    Assert(disc->entry_count < PLAN_ENTRY_MAX && index <= disc->entry_count);
    for (u32 i = disc->entry_count; i > index; i -= 1) {
        PlanEntry moved = plan_entry(disc, i - 1);
        plan_entry_store(disc, i, &moved);
    }
    disc->entry_count += 1;
    plan_entry_store(disc, index, entry);
}

static PlanEntry plan_entry_erase(PlanDisc *disc, u32 index) {
    Assert(index < disc->entry_count);
    PlanEntry entry = plan_entry(disc, index);
    for (u32 i = index + 1; i < disc->entry_count; i += 1) {
        PlanEntry moved = plan_entry(disc, i);
        plan_entry_store(disc, i - 1, &moved);
    }
    disc->entry_count -= 1;
    return entry;
}

static void plan_entry_relocate(PlanDisc *disc, u32 from, u32 to) {
    PlanEntry entry = plan_entry_erase(disc, from);
    plan_entry_insert(disc, to, &entry);
}

// A group is contiguous by construction and stays so under add / remove, which
// carry the membership with the entry. A move can break it; Ungroup, which
// restores a range, checks rather than assumes (ADR-012: this is a boundary).
static b32 plan_group_contiguous(const PlanDisc *disc, u32 group) {
    const PlanGroup *g = &disc->groups[group];
    if (g->count == 0) { return 1; }
    if ((u32)g->first + g->count > disc->entry_count) { return 0; }
    for (u32 i = 0; i < g->count; i += 1) {
        if (disc->group_id[g->first + i] != group) { return 0; }
    }
    return 1;
}

static void plan_disc_reset(PlanDisc *disc) {
    StructZero(disc);
    disc->length_min = PLAN_LENGTH_DEFAULT;
    mem_set(disc->group_id, PLAN_GROUP_NONE, sizeof(disc->group_id));
}

// Drops the groups a split left without a single member on this side. Only a
// split does this: everywhere else an empty group keeps its name, so undoing
// the removal of its last entry finds the group where it left it.
static void plan_groups_drop_empty(PlanDisc *disc) {
    plan_groups_refresh(disc);
    for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
        if ((disc->group_live & (1u << g)) && disc->groups[g].count == 0) {
            disc->group_live &= ~(1u << g);
            disc->groups[g].name = LIB_STRING_NONE;
        }
    }
}

// --- do / undo -----------------------------------------------------------------

static b32 plan_cmd_do(Plan *plan, PlanCmd *cmd) {
    if (cmd->disc >= plan->disc_count) { return 0; }
    PlanDisc *disc = &plan->discs[cmd->disc];
    switch (cmd->kind) {
        case PlanCmd_Add: {
            if (disc->entry_count >= PLAN_ENTRY_MAX) { return 0; }  // B-28
            if (cmd->index > disc->entry_count) { return 0; }
            plan_entry_insert(disc, cmd->index, &cmd->entry);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Remove: {
            if (cmd->index >= disc->entry_count) { return 0; }
            cmd->entry = plan_entry_erase(disc, cmd->index);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Move: {
            if (cmd->index >= disc->entry_count || cmd->aux >= disc->entry_count) { return 0; }
            if (cmd->index == cmd->aux) { return 0; }
            plan_entry_relocate(disc, cmd->index, cmd->aux);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_SetMode: {
            if (cmd->index >= disc->entry_count) { return 0; }
            u32 mode = cmd->new_value & 0xFF;
            if (mode >= PlanMode_COUNT) { return 0; }
            cmd->old_value = disc->mode[cmd->index] |
                             (u32)((disc->flags[cmd->index] & PlanEntryFlag_Mono) ? 0x100u : 0u);
            if (cmd->old_value == cmd->new_value) { return 0; }
            disc->mode[cmd->index] = (u8)mode;
            disc->flags[cmd->index] =
                (u8)((disc->flags[cmd->index] & ~(u32)PlanEntryFlag_Mono) |
                     ((cmd->new_value & 0x100u) ? PlanEntryFlag_Mono : 0u));
        } break;

        case PlanCmd_SetTitle: {
            if (cmd->index >= disc->entry_count) { return 0; }
            cmd->old_value = disc->title_override[cmd->index];
            disc->title_override[cmd->index] = cmd->new_value;
        } break;

        case PlanCmd_SetDiscTitle: {
            cmd->old_value = disc->title;
            disc->title = cmd->new_value;
        } break;

        case PlanCmd_Group: {
            u32 first = cmd->index, count = cmd->aux;
            if (count == 0 || first + count > disc->entry_count) { return 0; }
            for (u32 i = 0; i < count; i += 1) {
                if (disc->group_id[first + i] != PLAN_GROUP_NONE) { return 0; }
            }
            u32 slot = PLAN_GROUP_MAX;
            for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
                if (!(disc->group_live & (1u << g))) { slot = g; break; }
            }
            if (slot == PLAN_GROUP_MAX) { return 0; }
            cmd->group = (u8)slot;
            disc->group_live |= (1u << slot);
            disc->groups[slot].name = cmd->new_value;
            mem_set(disc->group_id + first, (u8)slot, count);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Ungroup: {
            u32 g = cmd->group;
            if (g >= PLAN_GROUP_MAX || !(disc->group_live & (1u << g))) { return 0; }
            if (!plan_group_contiguous(disc, g)) { return 0; }
            cmd->index = disc->groups[g].first;
            cmd->aux = disc->groups[g].count;
            cmd->new_value = disc->groups[g].name;
            mem_set(disc->group_id + cmd->index, PLAN_GROUP_NONE, cmd->aux);
            disc->group_live &= ~(1u << g);
            disc->groups[g].name = LIB_STRING_NONE;
            disc->groups[g].first = 0;
            disc->groups[g].count = 0;
        } break;

        case PlanCmd_SetDiscLength: {
            if (!plan_length_valid(cmd->new_value)) { return 0; }
            cmd->old_value = disc->length_min;
            if (cmd->old_value == cmd->new_value) { return 0; }
            disc->length_min = (u16)cmd->new_value;
        } break;

        case PlanCmd_SplitDisc: {
            u32 cut = cmd->index;
            if (plan->disc_count >= PLAN_DISC_MAX) { return 0; }
            if (cut == 0 || cut >= disc->entry_count) { return 0; }
            cmd->old_value = disc->group_live;
            for (u32 d = plan->disc_count; d > cmd->disc + 1u; d -= 1) {
                mem_copy(&plan->discs[d], &plan->discs[d - 1], sizeof(PlanDisc));
            }
            plan->disc_count += 1;
            disc = &plan->discs[cmd->disc];
            PlanDisc *dst = &plan->discs[cmd->disc + 1];
            plan_disc_reset(dst);
            dst->length_min = disc->length_min;
            dst->default_mode = disc->default_mode;
            dst->group_live = disc->group_live;
            mem_copy(dst->groups, disc->groups, sizeof(dst->groups));
            u32 moved = disc->entry_count - cut;
            for (u32 i = 0; i < moved; i += 1) {
                PlanEntry entry = plan_entry(disc, cut + i);
                plan_entry_store(dst, i, &entry);
            }
            dst->entry_count = moved;
            disc->entry_count = cut;
            plan_groups_drop_empty(disc);
            plan_groups_drop_empty(dst);
        } break;

        default: { Assert(!"unknown command"); return 0; }
    }
    return 1;
}

static void plan_cmd_undo_one(Plan *plan, const PlanCmd *cmd) {
    PlanDisc *disc = &plan->discs[cmd->disc];
    switch (cmd->kind) {
        case PlanCmd_Add: {
            plan_entry_erase(disc, cmd->index);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Remove: {
            plan_entry_insert(disc, cmd->index, &cmd->entry);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Move: {
            plan_entry_relocate(disc, cmd->aux, cmd->index);
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_SetMode: {
            disc->mode[cmd->index] = (u8)(cmd->old_value & 0xFF);
            disc->flags[cmd->index] =
                (u8)((disc->flags[cmd->index] & ~(u32)PlanEntryFlag_Mono) |
                     ((cmd->old_value & 0x100u) ? PlanEntryFlag_Mono : 0u));
        } break;

        case PlanCmd_SetTitle: { disc->title_override[cmd->index] = cmd->old_value; } break;
        case PlanCmd_SetDiscTitle: { disc->title = cmd->old_value; } break;

        case PlanCmd_Group: {
            mem_set(disc->group_id + cmd->index, PLAN_GROUP_NONE, cmd->aux);
            disc->group_live &= ~(1u << cmd->group);
            disc->groups[cmd->group].name = LIB_STRING_NONE;
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_Ungroup: {
            mem_set(disc->group_id + cmd->index, (u8)cmd->group, cmd->aux);
            disc->group_live |= (1u << cmd->group);
            disc->groups[cmd->group].name = cmd->new_value;
            plan_groups_refresh(disc);
        } break;

        case PlanCmd_SetDiscLength: { disc->length_min = (u16)cmd->old_value; } break;

        case PlanCmd_SplitDisc: {
            PlanDisc *src = &plan->discs[cmd->disc + 1];
            for (u32 i = 0; i < src->entry_count; i += 1) {
                PlanEntry entry = plan_entry(src, i);
                plan_entry_store(disc, disc->entry_count + i, &entry);
            }
            disc->entry_count += src->entry_count;
            disc->group_live = cmd->old_value;
            for (u32 d = cmd->disc + 1u; d + 1 < plan->disc_count; d += 1) {
                mem_copy(&plan->discs[d], &plan->discs[d + 1], sizeof(PlanDisc));
            }
            plan->disc_count -= 1;
            plan_disc_reset(&plan->discs[plan->disc_count]);
            plan_groups_refresh(disc);
        } break;

        default: { Assert(!"unknown command"); } break;
    }
}

// --- the stack -------------------------------------------------------------------

static void plan_event_push(Plan *plan, u32 kind, const PlanCmd *cmd, u32 direction) {
    PlanEventQueue *queue = &plan->events;
    if (queue->write - queue->read >= PLAN_EVENT_CAPACITY) { queue->read += 1; }
    PlanEvent *event = &queue->items[queue->write & (PLAN_EVENT_CAPACITY - 1)];
    event->kind = kind;
    event->cmd = cmd ? cmd->kind : PlanCmd_None;
    event->disc = cmd ? cmd->disc : 0;
    event->index = cmd ? cmd->index : 0;
    event->direction = direction;
    queue->write += 1;
}

static void plan_touch(Plan *plan) {
    plan->revision += 1;
    if (!plan->dirty) {
        plan->dirty = 1;
        plan->dirty_us = os_time_now_us();
    }
}

md_inline b32 plan_cmd_is_text(u32 kind) {
    return kind == PlanCmd_SetTitle || kind == PlanCmd_SetDiscTitle;
}

// Typing in a title is one undo step while the keystrokes keep coming: the
// window is time based, and any other command - or plan_coalesce_break, which
// the field calls when it loses focus - closes it.
static void plan_push(Plan *plan, const PlanCmd *cmd) {
    if (plan_cmd_is_text(cmd->kind) && plan->coalesce_open && plan->done > plan->first &&
        plan->done == plan->last) {
        PlanCmd *prev = &plan->undo[(plan->done - 1) % PLAN_UNDO_MAX];
        if (prev->kind == cmd->kind && prev->disc == cmd->disc && prev->index == cmd->index &&
            cmd->time_us - prev->time_us <= PLAN_COALESCE_US) {
            prev->new_value = cmd->new_value;
            prev->time_us = cmd->time_us;
            return;
        }
    }
    plan->last = plan->done;  // a new command drops the redo tail
    if (plan->done - plan->first >= PLAN_UNDO_MAX) { plan->first += 1; }
    mem_copy(&plan->undo[plan->done % PLAN_UNDO_MAX], cmd, sizeof(PlanCmd));
    plan->done += 1;
    plan->last = plan->done;
    plan->coalesce_open = plan_cmd_is_text(cmd->kind);
}

b32 plan_apply(Plan *plan, PlanCmd cmd) {
    if (!plan_cmd_do(plan, &cmd)) { return 0; }
    plan_push(plan, &cmd);
    plan_touch(plan);
    plan_event_push(plan, PlanEvent_Changed, &cmd, 0);
    return 1;
}

b32 plan_can_undo(const Plan *plan) { return plan->done > plan->first; }
b32 plan_can_redo(const Plan *plan) { return plan->done < plan->last; }
void plan_coalesce_break(Plan *plan) { plan->coalesce_open = 0; }

b32 plan_undo(Plan *plan) {
    if (!plan_can_undo(plan)) { return 0; }
    plan->done -= 1;
    PlanCmd *cmd = &plan->undo[plan->done % PLAN_UNDO_MAX];
    plan_cmd_undo_one(plan, cmd);
    plan->coalesce_open = 0;
    plan_touch(plan);
    plan_event_push(plan, PlanEvent_Changed, cmd, 1);
    return 1;
}

b32 plan_redo(Plan *plan) {
    if (!plan_can_redo(plan)) { return 0; }
    PlanCmd *cmd = &plan->undo[plan->done % PLAN_UNDO_MAX];
    // The state is the one the command was recorded against, so it cannot be
    // refused now: if it is, the inverse above lost something.
    b32 ok = plan_cmd_do(plan, cmd);
    AssertAlways(ok);
    plan->done += 1;
    plan->coalesce_open = 0;
    plan_touch(plan);
    plan_event_push(plan, PlanEvent_Changed, cmd, 2);
    return 1;
}

// --- the vocabulary ---------------------------------------------------------------

md_inline PlanCmd plan_cmd_make(u32 kind, u32 disc) {
    PlanCmd cmd;
    StructZero(&cmd);
    cmd.kind = (u8)kind;
    cmd.disc = (u8)disc;
    cmd.group = PLAN_GROUP_NONE;
    return cmd;
}

b32 plan_add(Plan *plan, u32 disc, u32 index, PlanEntry entry) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_Add, disc);
    cmd.index = index;
    cmd.entry = entry;
    return plan_apply(plan, cmd);
}

b32 plan_add_track(Plan *plan, const Library *lib, u32 disc_index, TrackId id) {
    if (disc_index >= plan->disc_count || !lib_track_live(lib, id)) { return 0; }
    PlanDisc *disc = &plan->discs[disc_index];
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = id;
    entry.path_id = lib_intern(&plan->strings, lib_track_path(lib, id));
    entry.duration_ms = lib->duration_ms[id];
    entry.gain_db = PLAN_GAIN_NONE;
    entry.mode = disc->default_mode;
    entry.group_id = PLAN_GROUP_NONE;
    return plan_add(plan, disc_index, disc->entry_count, entry);
}

b32 plan_remove(Plan *plan, u32 disc, u32 index) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_Remove, disc);
    cmd.index = index;
    return plan_apply(plan, cmd);
}

b32 plan_move(Plan *plan, u32 disc, u32 from, u32 to) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_Move, disc);
    cmd.index = from;
    cmd.aux = to;
    return plan_apply(plan, cmd);
}

b32 plan_set_mode(Plan *plan, u32 disc, u32 index, u32 mode, b32 mono) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_SetMode, disc);
    cmd.index = index;
    cmd.new_value = (mode & 0xFF) | (mono ? 0x100u : 0u);
    return plan_apply(plan, cmd);
}

b32 plan_set_title(Plan *plan, u32 disc, u32 index, String8 title, u64 now_us) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_SetTitle, disc);
    cmd.index = index;
    cmd.new_value = lib_intern(&plan->strings, str8_prefix(title, PLAN_TITLE_MAX));
    cmd.time_us = now_us;
    return plan_apply(plan, cmd);
}

b32 plan_set_disc_title(Plan *plan, u32 disc, String8 title, u64 now_us) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_SetDiscTitle, disc);
    cmd.new_value = lib_intern(&plan->strings, str8_prefix(title, PLAN_TITLE_MAX));
    cmd.time_us = now_us;
    return plan_apply(plan, cmd);
}

b32 plan_group(Plan *plan, u32 disc, u32 first, u32 count, String8 name) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_Group, disc);
    cmd.index = first;
    cmd.aux = count;
    cmd.new_value = lib_intern(&plan->strings, str8_prefix(name, PLAN_TITLE_MAX));
    return plan_apply(plan, cmd);
}

b32 plan_ungroup(Plan *plan, u32 disc, u32 group) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_Ungroup, disc);
    cmd.group = (u8)group;
    return plan_apply(plan, cmd);
}

b32 plan_set_disc_length(Plan *plan, u32 disc, u32 minutes) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_SetDiscLength, disc);
    cmd.new_value = minutes;
    return plan_apply(plan, cmd);
}

b32 plan_split_disc(Plan *plan, u32 disc, u32 index) {
    PlanCmd cmd = plan_cmd_make(PlanCmd_SplitDisc, disc);
    cmd.index = index;
    return plan_apply(plan, cmd);
}
