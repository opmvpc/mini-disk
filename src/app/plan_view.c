// plan_view.c - the pure half of the plan view: gauge geometry and gestures.
// See plan_view.h. Nothing here builds a box or reads the clock.
#include "plan_view.h"

// The edge of the bar the running cluster total `acc` lands on. Computed from
// the total and not from a per entry width, which is what makes the segments
// sum to the used width exactly however many of them there are (s9.3).
md_inline f32 plan_gauge_edge(u64 acc, u32 capacity, f32 width) {
    f32 x = round_f32((f32)acc * width / (f32)capacity);
    return clamp_f32(x, 0.0f, width);
}

void plan_gauge_layout(const PlanCapacity *capacity, f32 width, PlanGaugeLayout *out) {
    out->width = width;
    out->used_width = 0.0f;
    out->overflow_x = width;
    out->overflow = 0;
    out->count = 0;
    // The first frame of a panel has no rect yet, so it has no bar either. That
    // is a width of zero, not an error: there is simply nothing to lay out.
    if (width <= 0.0f) { return; }

    u32 total_clusters = capacity->capacity_clusters;
    u64 acc = 0;
    u32 previous_mode = PlanCapMode_COUNT;
    u32 alternate = 0;
    u32 i = 0;
    while (i < capacity->entry_count) {
        u32 mode = capacity->entry_mode[i];
        u32 fit = capacity->fit[i];
        f32 x = plan_gauge_edge(acc, total_clusters, width);

        // A run of entries the bar cannot tell apart becomes one segment. Only
        // neighbours of the same mode and the same fit are merged: a colour
        // change is information, and merging it away would hide the overflow.
        u32 last = i;
        u64 run_clusters = 0;
        u64 padding_ms = 0;
        u64 billed_ms = 0;
        f32 end;
        for (;;) {
            run_clusters += capacity->clusters[last];
            padding_ms += capacity->entry_padding_ms[last];
            billed_ms += (u64)capacity->clusters[last] * MD_MODE_TABLE[mode].cluster_ms;
            end = plan_gauge_edge(acc + run_clusters, total_clusters, width);
            u32 next = last + 1;
            b32 mergeable = next < capacity->entry_count &&
                            capacity->entry_mode[next] == mode && capacity->fit[next] == fit;
            if (!mergeable || end - x >= PLAN_GAUGE_SEGMENT_MIN_PX) { break; }
            last = next;
        }

        // The wasted end of the last cluster, as a share of what the run bills.
        // Rounding it away would be a lie of omission: a padding that exists is
        // worth its pixel (s9.3).
        f32 segment_width = end - x;
        f32 hatch = 0.0f;
        if (padding_ms != 0 && billed_ms != 0) {
            hatch = round_f32(segment_width * (f32)padding_ms / (f32)billed_ms);
            hatch = clamp_f32(hatch, PLAN_GAUGE_HATCH_MIN_PX, segment_width);
        }

        PlanGaugeSegment *segment = &out->segments[out->count];
        segment->x = x;
        segment->width = segment_width;
        segment->hatch_x = x + segment_width - hatch;
        segment->first = i;
        segment->count = last - i + 1;
        segment->clusters = (u32)run_clusters;
        segment->mode = (u8)mode;
        segment->alternate = (u8)((mode == previous_mode) ? !alternate : 0);
        segment->fit = (u8)fit;
        alternate = segment->alternate;
        previous_mode = mode;
        out->count += 1;

        if (fit != PlanFit_Fits && !out->overflow) {
            out->overflow = 1;
            out->overflow_x = x;
        }
        acc += run_clusters;
        i = last + 1;
    }
    out->used_width = plan_gauge_edge(acc, total_clusters, width);
}

u32 plan_gauge_segment_at(const PlanGaugeLayout *layout, f32 x) {
    for (u32 i = 0; i < layout->count; i += 1) {
        const PlanGaugeSegment *segment = &layout->segments[i];
        if (x >= segment->x && x < segment->x + segment->width) { return i; }
    }
    return layout->count;
}

u32 plan_gauge_reference_mode(const PlanCapacity *capacity) {
    u64 billed[PlanCapMode_COUNT];
    mem_zero(billed, sizeof(billed));  // a zeroing loop becomes a memset under /GL
    for (u32 i = 0; i < capacity->entry_count; i += 1) {
        u32 mode = capacity->entry_mode[i];
        billed[mode] += (u64)capacity->clusters[i] * MD_MODE_TABLE[mode].cluster_ms;
    }
    u32 best = PlanCapMode_SP;
    for (u32 mode = 1; mode < PlanCapMode_COUNT; mode += 1) {
        if (billed[mode] > billed[best]) { best = mode; }
    }
    return best;
}

u64 plan_gauge_time_at(const PlanCapacity *capacity, const PlanGaugeLayout *layout, f32 x) {
    if (layout->width <= 0.0f) { return 0; }
    f32 fraction = clamp_f32(x / layout->width, 0.0f, 1.0f);
    u32 mode = plan_gauge_reference_mode(capacity);
    u64 span_ms = (u64)capacity->capacity_clusters * MD_MODE_TABLE[mode].cluster_ms;
    return (u64)((f32)span_ms * fraction);
}

// --- gestures ----------------------------------------------------------------

PlanModeStep plan_mode_cycle(u32 mode, b32 mono) {
    PlanModeStep step;
    step.mode = PlanMode_SP;
    step.mono = 0;
    if (mono) { return step; }              // SP mono -> SP, the cycle closes
    if (mode == PlanMode_SP) {
        step.mode = PlanMode_LP2;
    } else if (mode == PlanMode_LP2) {
        step.mode = PlanMode_LP4;
    } else {
        step.mono = 1;                      // LP4 -> SP mono
    }
    return step;
}

u32 plan_drop_index(u32 from, u32 insert_before) {
    // The gap just above and just below the dragged row are both "leave it
    // where it is": the entry is pulled out before it is put back.
    if (insert_before > from) { return insert_before - 1; }
    return insert_before;
}

u32 plan_fill_count(const PlanCapacity *capacity, const u32 *durations, u32 count, u32 cap_mode) {
    u32 free_clusters = capacity->free_clusters;
    u32 room = capacity->remaining_entries;
    u32 taken = 0;
    while (taken < count && taken < room) {
        u32 clusters = plan_clusters_for(durations[taken], cap_mode);
        if (clusters > free_clusters) { break; }
        free_clusters -= clusters;
        taken += 1;
    }
    return taken;
}

// What `count` titles cost once each is cut to `quota` characters. Exactly the
// arithmetic of plan_toc_cells_for_title, so what this predicts is what
// plan_toc_budget then measures.
static u32 plan_shorten_cells(const u32 *chars, const u8 *non_sp, u32 count, u32 quota) {
    u32 total = 0;
    for (u32 i = 0; i < count; i += 1) {
        u32 kept = (chars[i] < quota) ? chars[i] : quota;
        u32 cells = plan_toc_cells_for_chars(kept);
        u32 floor_cells = non_sp[i] ? 1u : 0u;
        total += (cells > floor_cells) ? cells : floor_cells;
    }
    return total;
}

u32 plan_shorten_quota(const u32 *chars, const u8 *non_sp, u32 count, u32 cells_budget) {
    u32 low = 0;
    u32 high = 0;
    for (u32 i = 0; i < count; i += 1) {
        if (chars[i] > high) { high = chars[i]; }
    }
    // "Does quota q fit" is monotonic in q, so the largest one that fits is
    // found by bisection - nine passes for a full disc - rather than by
    // shortening every title and measuring again until it works.
    if (plan_shorten_cells(chars, non_sp, count, 0) > cells_budget) { return 0; }
    while (low < high) {
        u32 mid = low + (high - low + 1) / 2;
        if (plan_shorten_cells(chars, non_sp, count, mid) <= cells_budget) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

// --- the gestures that touch more than one entry --------------------------------

u32 plan_remove_entries(Plan *plan, u32 disc, const u32 *entries, u32 count) {
    if (count == 0) { return 0; }
    u32 removed = 0;
    plan_batch_begin(plan);
    // Backwards: removing an entry shifts everything after it, and the indices
    // were taken before any of them moved.
    for (u32 i = count; i > 0; i -= 1) {
        removed += plan_remove(plan, disc, entries[i - 1]) ? 1u : 0u;
    }
    plan_batch_end(plan);
    return removed;
}

u32 plan_set_mode_entries(Plan *plan, u32 disc, const u32 *entries, u32 count,
                          PlanModeStep step) {
    if (count == 0) { return 0; }
    u32 changed = 0;
    plan_batch_begin(plan);
    for (u32 i = 0; i < count; i += 1) {
        changed += plan_set_mode(plan, disc, entries[i], step.mode, step.mono) ? 1u : 0u;
    }
    plan_batch_end(plan);
    return changed;
}

u32 plan_shorten_apply(Plan *plan, const Library *lib, u32 disc_index, u32 cells_budget) {
    PlanDisc *disc = plan_disc(plan, disc_index);
    u32 count = disc->entry_count;
    if (count == 0) { return 0; }
    u32 chars[PLAN_ENTRY_MAX];
    u8 non_sp[PLAN_ENTRY_MAX];
    PlanTitlePreview preview;
    for (u32 i = 0; i < count; i += 1) {
        plan_toc_preview(plan_entry_title(plan, lib, disc_index, i), 0, &preview);
        chars[i] = preview.chars;
        non_sp[i] = (u8)(disc->mode[i] != PlanMode_SP);
    }
    u32 quota = plan_shorten_quota(chars, non_sp, count, cells_budget);
    if (quota == 0) { return 0; }
    plan_batch_begin(plan);
    for (u32 i = 0; i < count; i += 1) {
        if (chars[i] <= quota) { continue; }
        plan_toc_preview(plan_entry_title(plan, lib, disc_index, i), quota, &preview);
        plan_set_title(plan, disc_index, i, str8(preview.text, preview.size), 0);
    }
    plan_batch_end(plan);
    return quota;
}
