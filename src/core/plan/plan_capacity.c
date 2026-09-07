// plan_capacity.c - clusters, not seconds (ADR-011 D2). See plan_capacity.h for
// the model and for what still has to be validated on the device (T-045).

const PlanModeSpec MD_MODE_TABLE[PlanCapMode_COUNT] = {
    {2000, 292, "SP"},    // ATRAC SP stereo: the cluster itself, ~2 s
    {4000, 146, "Mono"},  // half the channels, twice the running time
    {4000, 132, "LP2"},   // ATRAC3 132 kbit/s
    {8000, 66, "LP4"},    // ATRAC3 joint stereo 66 kbit/s
};

u32 plan_clusters_for(u32 duration_ms, u32 cap_mode) {
    Assert(cap_mode < PlanCapMode_COUNT);
    u32 cluster_ms = MD_MODE_TABLE[cap_mode].cluster_ms;
    u32 clusters = (duration_ms + cluster_ms - 1) / cluster_ms;
    return clusters ? clusters : 1;  // a track always takes a cluster
}

u32 plan_padding_ms(u32 duration_ms, u32 cap_mode) {
    u32 cluster_ms = MD_MODE_TABLE[cap_mode].cluster_ms;
    return plan_clusters_for(duration_ms, cap_mode) * cluster_ms - duration_ms;
}

void plan_capacity_compute(const PlanDisc *disc, PlanCapacity *out) {
    u32 count = disc->entry_count;
    Assert(count <= PLAN_ENTRY_MAX);

    u32 capacity = plan_clusters_capacity(disc->length_min);
    u32 used = 0;
    u64 audio_ms = 0, billed_ms = 0;
    u32 first_overflow = count;

    // One pass over three columns. The running total is what decides the per
    // entry state: the first track whose cluster range crosses the end of the
    // disc is Partial, everything after it Overflow.
    for (u32 i = 0; i < count; i += 1) {
        u32 mode = plan_cap_mode_of(disc, i);
        u32 duration_ms = disc->duration_ms[i];
        u32 clusters = plan_clusters_for(duration_ms, mode);
        u32 start = used;
        used += clusters;

        u32 entry_billed_ms = clusters * MD_MODE_TABLE[mode].cluster_ms;
        out->clusters[i] = clusters;
        out->entry_mode[i] = (u8)mode;
        out->entry_padding_ms[i] = entry_billed_ms - duration_ms;
        audio_ms += duration_ms;
        billed_ms += entry_billed_ms;

        u8 fit = PlanFit_Fits;
        if (start >= capacity) {
            fit = PlanFit_Overflow;
        } else if (used > capacity) {
            fit = PlanFit_Partial;
        }
        if (fit != PlanFit_Fits && first_overflow == count) { first_overflow = i; }
        out->fit[i] = fit;
    }

    out->length_min = disc->length_min;
    out->capacity_clusters = capacity;
    out->used_clusters = used;
    out->free_clusters = (used < capacity) ? capacity - used : 0;
    out->overflow_clusters = (used > capacity) ? used - capacity : 0;
    out->entry_count = count;
    out->first_overflow = first_overflow;
    out->audio_ms = audio_ms;
    out->billed_ms = billed_ms;
    out->padding_ms = billed_ms - audio_ms;
    out->remaining_entries = PLAN_ENTRY_MAX - count;
    for (u32 mode = 0; mode < PlanCapMode_COUNT; mode += 1) {
        out->remaining_ms[mode] = out->free_clusters * MD_MODE_TABLE[mode].cluster_ms;
    }
}

// --- multi disc auto split ---------------------------------------------------

// The album an entry belongs to, as an interned id. Entries whose track is gone
// from the library each get their own run, which is the conservative answer:
// nothing unrelated is ever glued together.
static u32 plan_split_album(const PlanDisc *disc, const Library *lib, u32 index) {
    if (!lib) { return 0; }
    u32 id = disc->track_id[index];
    if (!lib_track_live(lib, id)) { return 0; }
    return lib->album_id[id];
}

// First fit over the discs opened so far, in order. Returns PLAN_DISC_MAX when
// the run fits nowhere and no disc is left to open.
static u32 plan_split_place(PlanSplit *split, u32 clusters, u32 entries, u32 capacity) {
    for (u32 d = 0; d < split->disc_count; d += 1) {
        if (split->clusters[d] + clusters <= capacity &&
            split->counts[d] + entries <= PLAN_ENTRY_MAX) {
            return d;
        }
        // A lone track longer than a whole disc still has to go somewhere: the
        // first empty disc takes it rather than a new one being opened for it.
        if (split->counts[d] == 0 && entries == 1) { return d; }
    }
    // A run that does not even fit on an empty disc is refused, so the caller
    // can break it up; a single entry longer than a disc is placed all the same,
    // because refusing it would only hide it.
    if (split->disc_count < PLAN_DISC_MAX && (clusters <= capacity || entries == 1)) {
        u32 d = split->disc_count;
        split->disc_count += 1;
        return d;
    }
    return PLAN_DISC_MAX;
}

void plan_capacity_split(const PlanDisc *disc, const Library *lib, u32 policy, u32 minutes,
                         PlanSplit *out) {
    Assert(plan_length_valid(minutes));
    StructZero(out);
    u32 count = disc->entry_count;
    u32 capacity = plan_clusters_capacity(minutes);
    out->entry_count = count;
    if (count == 0) { return; }
    out->disc_count = 1;

    u32 i = 0;
    while (i < count) {
        // The unit being placed: one entry, or the whole album run starting here.
        u32 run = 1;
        if (policy == PlanSplit_KeepAlbums) {
            u32 album = plan_split_album(disc, lib, i);
            if (album != 0) {
                while (i + run < count && plan_split_album(disc, lib, i + run) == album) {
                    run += 1;
                }
            }
        }

        u32 clusters = 0;
        u32 target = PLAN_DISC_MAX;
        for (;;) {
            clusters = 0;
            for (u32 k = 0; k < run; k += 1) {
                clusters +=
                    plan_clusters_for(disc->duration_ms[i + k], plan_cap_mode_of(disc, i + k));
            }
            target = plan_split_place(out, clusters, run, capacity);
            if (target != PLAN_DISC_MAX) { break; }
            // An album that fits on no disc as a block is placed track by track:
            // keeping it together was a preference, not a promise.
            if (run > 1) { run = 1; continue; }
            out->unplaced = count - i;
            // mem_set and not a loop: under /GL MSVC recognises the loop and
            // emits a call to the CRT's memset, which we do not link (T-004).
            mem_set(out->disc_of + i, (u8)PLAN_DISC_MAX, count - i);
            return;
        }
        mem_set(out->disc_of + i, (u8)target, run);  // C2268 again: see above
        out->clusters[target] += clusters;
        out->counts[target] += run;
        i += run;
    }
}
