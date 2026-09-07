// transfer.c - see transfer.h.

// --- the pre-flight (D4) -------------------------------------------------------

// The two questions the simulation answers about the disc that is actually in
// the machine, in the units the device counts in. With no disc, the plan's own
// length stands in so the pre-flight still says something useful offline.
typedef struct TransferDiscFacts {
    u32 capacity_clusters;
    u32 used_clusters;
    u32 free_clusters;
    u64 free_ms;
    u32 track_count;
    u32 cells;
    b32 protected_disc;
    b32 present;
    b32 titled;
} TransferDiscFacts;

static void transfer_disc_facts(const PlanDisc *plan_disc, const DiscLayout *disc, u32 policy,
                                TransferDiscFacts *out) {
    StructZero(out);
    if (!disc || (disc->flags & NetmdDiscFlag_Present) == 0) {
        out->capacity_clusters = plan_clusters_capacity(plan_disc->length_min);
        out->free_clusters = out->capacity_clusters;
        out->free_ms = (u64)out->free_clusters * PLAN_CLUSTER_SP_MS;
        return;
    }
    out->present = 1;
    out->capacity_clusters = (u32)(disc->capacity.total.ms / PLAN_CLUSTER_SP_MS);
    out->track_count = disc->track_count;
    out->cells = netmd_layout_cells(disc, 0, 0, 0);
    out->protected_disc = (disc->flags & NetmdDiscFlag_WriteProtected) != 0 ||
                          (disc->flags & NetmdDiscFlag_Writable) == 0;
    out->titled = disc->title_size != 0;
    if (policy == TransferPolicy_EraseFirst) {
        // An erased disc is a whole disc again: its free time is its capacity,
        // and its TOC is empty. Nothing here erases anything - this is what the
        // user is being shown before deciding to.
        out->free_clusters = out->capacity_clusters;
        out->free_ms = disc->capacity.total.ms;
        out->used_clusters = 0;
        out->track_count = 0;
        out->cells = 0;
        out->titled = 0;
        return;
    }
    out->free_ms = disc->capacity.available.ms;
    out->free_clusters = (u32)(out->free_ms / PLAN_CLUSTER_SP_MS);
    out->used_clusters = (u32)(disc->capacity.recorded.ms / PLAN_CLUSTER_SP_MS);
}

void transfer_simulate(const Plan *plan, const Library *lib, u32 disc_index,
                       const DiscLayout *disc, u32 policy, TransferSim *out) {
    StructZero(out);
    out->policy = policy;
    const PlanDisc *plan_disc = &plan->discs[disc_index];
    out->count = plan_disc->entry_count;

    TransferDiscFacts facts;
    transfer_disc_facts(plan_disc, disc, policy, &facts);
    out->clusters_capacity = facts.capacity_clusters;
    out->clusters_before = facts.used_clusters;
    out->free_ms_before = facts.free_ms;
    out->tracks_before = facts.track_count;
    out->cells_before = facts.cells;

    if (!facts.present) { out->warnings |= TransferWarn_NoDisc; }
    if (facts.protected_disc) { out->warnings |= TransferWarn_Protected; }
    if (facts.track_count != 0) { out->warnings |= TransferWarn_DiscNotEmpty; }

    // Pass one: the titles as the TOC will hold them, and what each track costs.
    // The quota of s9.7 is only reached for when the budget says it must be.
    u32 chars[PLAN_ENTRY_MAX];
    u8 non_sp[PLAN_ENTRY_MAX];
    u32 cells_tracks = 0;
    u32 used = facts.free_clusters;  // clusters still available as the walk goes
    for (u32 i = 0; i < plan_disc->entry_count; i += 1) {
        TransferSimEntry *entry = &out->entries[i];
        entry->entry = i;
        entry->duration_ms = plan_disc->duration_ms[i];
        u32 cap_mode = plan_cap_mode_of(plan_disc, i);
        entry->clusters = plan_clusters_for(entry->duration_ms, cap_mode);
        // The plan's own path, not the library's: a plan carries the path of
        // every entry precisely so it still says something when the library has
        // been rebuilt under it (T-030). It is what re-resolves an id.
        String8 path = lib_string(&plan->strings, plan_disc->path_id[i]);
        OsFileInfo info;
        StructZero(&info);
        entry->missing = path.size == 0 || !os_file_stat(path, &info);

        PlanTitlePreview preview;
        plan_toc_preview(plan_entry_title(plan, lib, disc_index, i), 0, &preview);
        entry->title_size = (u16)Min(preview.size, (u64)PLAN_TITLE_MAX);
        mem_copy(entry->title, preview.text, entry->title_size);
        entry->shorten = preview.applied;
        chars[i] = preview.chars;
        non_sp[i] = (u8)(cap_mode == PlanCapMode_LP2 || cap_mode == PlanCapMode_LP4);
        entry->cells = plan_toc_cells_for_title(str8(entry->title, entry->title_size),
                                                non_sp[i] != 0);
        if (entry->missing) {
            out->missing_count += 1;
            out->warnings |= TransferWarn_MissingTrack;
            continue;
        }
        entry->fits = entry->clusters <= used;
        if (entry->fits) {
            used -= entry->clusters;
        } else {
            used = 0;
            out->warnings |= TransferWarn_Overflow;
        }
        out->clusters_needed += entry->clusters;
        out->audio_ms += entry->duration_ms;
        out->write_count += 1;
        cells_tracks += entry->cells;
    }

    // The disc title, and the rule that keeps the user's disc theirs: it is
    // written onto a disc that has none, or onto one this run just erased, and
    // never over an existing one - that title carries their groups.
    b32 may_title = !facts.present || policy == TransferPolicy_EraseFirst ||
                    (!facts.titled && facts.track_count == 0);
    u32 cells_disc = 0;
    if (may_title) {
        u32 budget = (PLAN_TOC_CELLS > cells_tracks) ? PLAN_TOC_CELLS - cells_tracks : 0u;
        u32 groups_kept = 0;
        u64 size = plan_toc_compile_disc_title(plan, plan_disc, budget, out->disc_title,
                                               sizeof(out->disc_title), &groups_kept);
        out->disc_title_size = (u16)size;
        out->groups = groups_kept;
        out->write_disc_title = size != 0;
        if (size != 0) { cells_disc = plan_toc_cells_for_chars((u32)size); }
    } else if (facts.titled) {
        out->warnings |= TransferWarn_TitleKept;
    }

    out->cells_after = out->cells_before + cells_tracks + cells_disc;
    if (policy == TransferPolicy_EraseFirst) { out->cells_after = cells_tracks + cells_disc; }
    if (out->cells_after > PLAN_TOC_CELLS) {
        // s9.7: one quota over every title, the largest one that fits. It is a
        // preview, not an edit - the plan is not touched.
        u32 room = (PLAN_TOC_CELLS > out->cells_before + cells_disc)
                           ? PLAN_TOC_CELLS - out->cells_before - cells_disc
                           : 0u;
        u32 quota = plan_shorten_quota(chars, non_sp, plan_disc->entry_count, room);
        if (quota != 0) {
            cells_tracks = 0;
            for (u32 i = 0; i < plan_disc->entry_count; i += 1) {
                TransferSimEntry *entry = &out->entries[i];
                PlanTitlePreview preview;
                plan_toc_preview(plan_entry_title(plan, lib, disc_index, i), quota, &preview);
                entry->title_size = (u16)Min(preview.size, (u64)PLAN_TITLE_MAX);
                mem_copy(entry->title, preview.text, entry->title_size);
                entry->shorten = preview.applied;
                entry->cells = plan_toc_cells_for_title(str8(entry->title, entry->title_size),
                                                        non_sp[i] != 0);
                if (!entry->missing) { cells_tracks += entry->cells; }
            }
            out->warnings |= TransferWarn_Shortened;
            out->cells_after = (policy == TransferPolicy_EraseFirst ? 0u : out->cells_before) +
                               cells_tracks + cells_disc;
        }
        if (out->cells_after > PLAN_TOC_CELLS) { out->warnings |= TransferWarn_TocOverflow; }
    }
    out->cells_free_after =
            (PLAN_TOC_CELLS > out->cells_after) ? PLAN_TOC_CELLS - out->cells_after : 0u;

    out->clusters_after = out->clusters_before + out->clusters_needed;
    u64 needed_ms = (u64)out->clusters_needed * PLAN_CLUSTER_SP_MS;
    out->free_ms_after = (facts.free_ms > needed_ms) ? facts.free_ms - needed_ms : 0u;
    out->tracks_after = out->tracks_before + out->write_count;
    if (out->write_count == 0) { out->warnings |= TransferWarn_Empty; }
    out->allowed = out->write_count != 0 && (out->warnings & TransferWarn_Protected) == 0 &&
                   (out->warnings & TransferWarn_Overflow) == 0 &&
                   (out->warnings & TransferWarn_TocOverflow) == 0;
}

// --- the run ---------------------------------------------------------------------

u32 transfer_expected_s(const TransferSim *sim) {
    // SP is written in real time: the device encodes ATRAC1 as it swallows the
    // PCM, so the wall clock cost is the audio itself plus the per track TOC
    // work. Announced before the run so the number on screen is never a
    // surprise (research/01 s6.1, ADR-011 D9).
    u64 ms = sim->audio_ms + (u64)sim->write_count * 3000ull;
    return (u32)(ms / 1000u);
}

void transfer_begin(Transfer *transfer, const TransferSim *sim) {
    StructZero(transfer);
    transfer->phase = TransferPhase_Preflight;
    for (u32 i = 0; i < sim->count; i += 1) {
        const TransferSimEntry *entry = &sim->entries[i];
        if (entry->missing) {
            transfer->state[transfer->count] = TransferTrack_Skipped;
            transfer->track_bytes[transfer->count] = 0;
            transfer->count += 1;
            continue;
        }
        // 44100 frames a second of four bytes, rounded up to whole 2048 byte SP
        // frames: the same arithmetic netmd_upload_run bills the run with, so
        // the bar and the device agree from the first byte.
        u64 bytes = ((u64)entry->duration_ms * (u64)44100u * 4u) / 1000u;
        bytes = ((bytes + 2047u) / 2048u) * 2048u;
        transfer->state[transfer->count] = TransferTrack_Pending;
        transfer->track_bytes[transfer->count] = bytes;
        transfer->bytes_total += bytes;
        transfer->count += 1;
    }
}

// The rate window: one sample per progress event, oldest ones aged out of the
// 30 second window on the way (MI-28).
static void transfer_sample(Transfer *transfer, u64 now_us, u64 bytes) {
    TransferRateSample *sample = &transfer->samples[transfer->sample_next];
    sample->us = now_us;
    sample->bytes = bytes;
    transfer->sample_next = (transfer->sample_next + 1u) % TRANSFER_RATE_SAMPLES;
    if (transfer->sample_count < TRANSFER_RATE_SAMPLES) { transfer->sample_count += 1; }
}

u64 transfer_rate(const Transfer *transfer) {
    if (transfer->sample_count < 2) { return 0; }
    u32 newest = (transfer->sample_next + TRANSFER_RATE_SAMPLES - 1u) % TRANSFER_RATE_SAMPLES;
    const TransferRateSample *last = &transfer->samples[newest];
    const TransferRateSample *oldest = 0;
    for (u32 i = 0; i < transfer->sample_count; i += 1) {
        u32 index = (transfer->sample_next + TRANSFER_RATE_SAMPLES - transfer->sample_count + i) %
                    TRANSFER_RATE_SAMPLES;
        const TransferRateSample *sample = &transfer->samples[index];
        if (last->us - sample->us <= TRANSFER_RATE_WINDOW_US) {
            oldest = sample;
            break;
        }
    }
    if (!oldest || oldest == last) { return 0; }
    u64 span_us = last->us - oldest->us;
    if (span_us < 1000000ull || last->bytes <= oldest->bytes) { return 0; }
    return ((last->bytes - oldest->bytes) * 1000000ull) / span_us;
}

// D9: the honest one. The measured rate when the run has moved enough to have
// one, the nominal SP rate before that, and a value that only ever goes down -
// an estimate that climbs is the one thing a progress bar must never do.
static void transfer_update_eta(Transfer *transfer, u64 now_us) {
    if (transfer->bytes_done >= transfer->bytes_total) {
        transfer->eta_s = 0;
        transfer->eta_us = now_us;
        return;
    }
    u64 left = transfer->bytes_total - transfer->bytes_done;
    u64 rate = transfer_rate(transfer);
    if (transfer->bytes_done < TRANSFER_RATE_MIN_BYTES || rate == 0) {
        rate = TRANSFER_SP_BYTES_PER_S;
    }
    u32 computed = (u32)(left / rate);
    if (transfer->eta_us != 0) {
        // What the previous estimate would have become had it been right. The
        // shown value is the smaller of the two, so it slides down and never up.
        u64 elapsed_s = (now_us - transfer->eta_us) / 1000000ull;
        u32 decayed = (transfer->eta_s > elapsed_s) ? (u32)(transfer->eta_s - elapsed_s) : 0u;
        if (computed > decayed) { computed = decayed; }
    }
    transfer->eta_s = computed;
    transfer->eta_us = now_us;

    // MI-29: a rate well under the run's own average, for long enough to be a
    // fact rather than a hiccup. One calm line, never a dialog.
    u64 average = 0;
    u64 span_us = now_us - transfer->started_us - transfer->paused_total_us;
    if (span_us > 1000000ull) { average = (transfer->bytes_done * 1000000ull) / span_us; }
    u64 measured = transfer_rate(transfer);
    b32 under = average != 0 && measured != 0 && measured * 100u < average * TRANSFER_SLOW_RATIO;
    if (!under) {
        transfer->slow_since_us = 0;
        transfer->slow = 0;
        return;
    }
    if (transfer->slow_since_us == 0) { transfer->slow_since_us = now_us; }
    transfer->slow = (now_us - transfer->slow_since_us) >= TRANSFER_SLOW_US;
}

void transfer_apply(Transfer *transfer, const TransferEvent *event) {
    u64 now = event->now_us;
    transfer->last_us = now;
    switch (event->kind) {
        case TransferEvent_Start: {
            transfer->phase = TransferPhase_Running;
            transfer->started_us = now;
            transfer->eta_us = 0;
            transfer->eta_s = 0;
            transfer->sample_count = 0;
            transfer->sample_next = 0;
            transfer_sample(transfer, now, transfer->bytes_done);
        } break;
        case TransferEvent_CacheHit: {
            if (event->entry < transfer->count &&
                transfer->state[event->entry] == TransferTrack_Pending) {
                transfer->state[event->entry] = TransferTrack_Transcoded;
                transfer->transcoded += 1;
                transfer->cache_hits += 1;
            }
        } break;
        case TransferEvent_TranscodeBegin: {
            if (event->entry < transfer->count &&
                transfer->state[event->entry] == TransferTrack_Pending) {
                transfer->state[event->entry] = TransferTrack_Transcoding;
            }
        } break;
        case TransferEvent_TranscodeDone: {
            if (event->entry < transfer->count &&
                transfer->state[event->entry] == TransferTrack_Transcoding) {
                transfer->state[event->entry] = TransferTrack_Transcoded;
                transfer->transcoded += 1;
            }
        } break;
        case TransferEvent_TranscodeFailed: {
            if (event->entry < transfer->count) {
                transfer->state[event->entry] = TransferTrack_Failed;
                transfer->result[event->entry] = event->result;
                transfer->failed_count += 1;
            }
        } break;
        case TransferEvent_Progress: {
            transfer->current = event->entry;
            if (event->bytes_total != 0) { transfer->bytes_total = event->bytes_total; }
            transfer->bytes_done = event->bytes_done;
            if (event->entry < transfer->count) {
                u8 *state = &transfer->state[event->entry];
                if (*state == TransferTrack_Pending || *state == TransferTrack_Transcoded ||
                    *state == TransferTrack_Transcoding) {
                    *state = TransferTrack_Sending;
                }
            }
            transfer_sample(transfer, now, transfer->bytes_done);
            transfer_update_eta(transfer, now);
        } break;
        case TransferEvent_TrackDone: {
            if (event->entry < transfer->count) {
                // s4.12: the title is written before the commit, so a track the
                // device has committed is a track that already carries its name.
                transfer->state[event->entry] = TransferTrack_Titled;
                transfer->done_count += 1;
            }
            transfer->bytes_done = event->bytes_done;
            transfer_sample(transfer, now, transfer->bytes_done);
            transfer_update_eta(transfer, now);
            if (transfer->phase == TransferPhase_Pausing) {
                // A pause takes effect between two tracks and never inside one:
                // a half sent track is a track the device would keep.
                transfer->phase = TransferPhase_Paused;
                transfer->paused_us = now;
            }
        } break;
        case TransferEvent_UploadDone: {
            transfer->phase = (transfer->failed_count != 0) ? TransferPhase_Failed
                                                            : TransferPhase_Done;
            transfer->last_result = NetmdResult_Ok;
            transfer->finished_us = now;
            transfer->eta_s = 0;
            for (u32 i = 0; i < transfer->count; i += 1) {
                if (transfer->state[i] == TransferTrack_Pending ||
                    transfer->state[i] == TransferTrack_Transcoded) {
                    transfer->state[i] = TransferTrack_Skipped;
                }
            }
        } break;
        case TransferEvent_UploadError: {
            transfer->last_result = event->result;
            transfer->finished_us = now;
            transfer->eta_s = 0;
            if (event->result == NetmdResult_Cancelled ||
                transfer->phase == TransferPhase_Cancelling) {
                transfer->phase = TransferPhase_Cancelled;
            } else {
                transfer->phase = TransferPhase_Failed;
                if (transfer->current < transfer->count &&
                    transfer->state[transfer->current] != TransferTrack_Titled) {
                    transfer->state[transfer->current] = TransferTrack_Failed;
                    transfer->result[transfer->current] = event->result;
                    transfer->failed_count += 1;
                }
            }
            // What was committed stays committed: those tracks are on the disc
            // and erasing them to tidy up would destroy the user's work. The
            // rest goes back to Pending, which is what a resume picks up.
            for (u32 i = 0; i < transfer->count; i += 1) {
                if (transfer->state[i] == TransferTrack_Sending) {
                    transfer->state[i] = (transfer->phase == TransferPhase_Cancelled)
                                                 ? (u8)TransferTrack_Pending
                                                 : (u8)TransferTrack_Failed;
                }
            }
        } break;
        case TransferEvent_PauseRequested: {
            if (transfer->phase == TransferPhase_Running) {
                transfer->phase = TransferPhase_Pausing;
            }
        } break;
        case TransferEvent_Resume: {
            if (transfer->paused_us != 0) {
                transfer->paused_total_us += now - transfer->paused_us;
                transfer->paused_us = 0;
            }
            transfer->phase = TransferPhase_Running;
            // A resume is a new run for the estimate: the rate of the tracks
            // that were written before the interruption says nothing about now.
            transfer->sample_count = 0;
            transfer->sample_next = 0;
            transfer->eta_us = 0;
            transfer->eta_s = 0;
            transfer->failed_count = 0;
            for (u32 i = 0; i < transfer->count; i += 1) {
                if (transfer->state[i] == TransferTrack_Failed) {
                    transfer->state[i] = TransferTrack_Pending;
                }
            }
            if (transfer->started_us == 0) { transfer->started_us = now; }
            transfer_sample(transfer, now, transfer->bytes_done);
        } break;
        case TransferEvent_CancelRequested: {
            if (transfer->phase == TransferPhase_Running ||
                transfer->phase == TransferPhase_Pausing ||
                transfer->phase == TransferPhase_Paused) {
                transfer->phase = TransferPhase_Cancelling;
            }
        } break;
        default: break;
    }
}

u32 transfer_elapsed_s(const Transfer *transfer, u64 now_us) {
    if (transfer->started_us == 0) { return 0; }
    u64 end = transfer->finished_us ? transfer->finished_us : now_us;
    if (end <= transfer->started_us) { return 0; }
    u64 span = end - transfer->started_us;
    u64 paused = transfer->paused_total_us;
    if (transfer->paused_us != 0 && end > transfer->paused_us) {
        paused += end - transfer->paused_us;
    }
    return (u32)(((span > paused) ? span - paused : 0ull) / 1000000ull);
}

u32 transfer_progress_permille(const Transfer *transfer) {
    if (transfer->bytes_total == 0) { return 0; }
    u64 done = Min(transfer->bytes_done, transfer->bytes_total);
    return (u32)((done * 1000ull) / transfer->bytes_total);
}

b32 transfer_active(const Transfer *transfer) {
    return transfer->phase == TransferPhase_Running ||
           transfer->phase == TransferPhase_Pausing ||
           transfer->phase == TransferPhase_Paused ||
           transfer->phase == TransferPhase_Cancelling;
}

void transfer_log(Transfer *transfer, u64 now_us, String8 line) {
    u64 seconds = transfer->started_us ? (now_us - transfer->started_us) / 1000000ull : 0ull;
    u8 stamp[16];
    u32 stamp_size = 0;
    u32 minutes = (u32)(seconds / 60u);
    u32 rest = (u32)(seconds % 60u);
    stamp[stamp_size++] = '[';
    stamp[stamp_size++] = (u8)('0' + (minutes / 10u) % 10u);
    stamp[stamp_size++] = (u8)('0' + minutes % 10u);
    stamp[stamp_size++] = ':';
    stamp[stamp_size++] = (u8)('0' + rest / 10u);
    stamp[stamp_size++] = (u8)('0' + rest % 10u);
    stamp[stamp_size++] = ']';
    stamp[stamp_size++] = ' ';
    u64 needed = stamp_size + line.size + 2;
    if (transfer->log_size + needed > TRANSFER_LOG_BYTES) { return; }
    mem_copy(transfer->log + transfer->log_size, stamp, stamp_size);
    transfer->log_size += stamp_size;
    mem_copy(transfer->log + transfer->log_size, line.str, line.size);
    transfer->log_size += (u32)line.size;
    transfer->log[transfer->log_size++] = '\r';
    transfer->log[transfer->log_size++] = '\n';
}
