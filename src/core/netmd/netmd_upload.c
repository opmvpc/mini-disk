// netmd_upload.c - see netmd_upload.h.
#include "netmd_upload.h"

#include "../codecs/codec.h"

// --- titles ------------------------------------------------------------------

u64 netmd_sjis_from_utf8(String8 in, u8 *out, u64 capacity) {
    u64 written = 0;
    for (u64 at = 0; at < in.size && written < capacity;) {
        u8 byte = in.str[at];
        if (byte < 0x80u) {
            out[written] = byte;
            written += 1;
            at += 1;
            continue;
        }
        // Half-width katakana, U+FF61..U+FF9F, always EF BD A1 .. EF BE 9F in
        // UTF-8 and 0xA1..0xDF in Shift-JIS - a straight offset, no table.
        if (at + 2 < in.size && byte == 0xEFu) {
            u32 code = 0xF000u | ((u32)(in.str[at + 1] & 0x3Fu) << 6) | (in.str[at + 2] & 0x3Fu);
            if (code >= 0xFF61u && code <= 0xFF9Fu) {
                out[written] = (u8)(code - 0xFF61u + 0xA1u);
                written += 1;
                at += 3;
                continue;
            }
        }
        // plan_toc_sanitize already dropped everything else, so reaching here
        // means a caller skipped it. Drop the byte rather than write a byte the
        // TOC would show as garbage.
        at += 1;
    }
    return written;
}

// s3.9: newLen and oldLen are Shift-JIS byte counts, and a wrong oldLen
// corrupts the TOC on some machines - which is why the current title is read
// back first, every time, instead of assumed.
static u32 netmd_upload_write_title(NetmdSession *session, Arena *arena,
                                    const NetmdDescriptor *descriptor, String8 request,
                                    b32 verify) {
    u32 result = netmd_descriptor_open(session, arena, descriptor, NETMD_DESC_OPEN_WRITE);
    Unused(result);  // s3.4: an open that fails is advisory, the close is not
    String8 reply;
    u32 write_result = netmd_exchange(session, arena, request, NETMD_BUDGET_DESCRIPTOR_MS, &reply);
    netmd_descriptor_close(session, arena, descriptor);
    if (verify && write_result == NetmdResult_Ok) {
        // s3.9: the read-open/close round trip that makes some machines commit
        // their TOC cache. Free, and the failure it avoids is a lost title.
        netmd_descriptor_open(session, arena, descriptor, NETMD_DESC_OPEN_READ);
        netmd_descriptor_close(session, arena, descriptor);
    }
    return write_result;
}

u32 netmd_upload_set_track_title(NetmdSession *session, Arena *arena, u32 track, String8 title) {
    ArenaTemp scratch = arena_temp_begin(arena);
    u8 sjis[NETMD_TITLE_MAX];
    u64 sjis_size = netmd_sjis_from_utf8(title, sjis, sizeof(sjis));

    String8 current = str8(0, 0);
    (void)netmd_get_track_title(session, arena, track, 0, &current);
    u8 old_sjis[NETMD_TITLE_MAX];
    u64 old_size = netmd_sjis_from_utf8(current, old_sjis, sizeof(old_sjis));
    // s3.9 point 1: the LAM series hangs when a title is rewritten with the
    // value it already holds. Nothing to do is nothing to send.
    if (old_size == sjis_size && mem_cmp(old_sjis, sjis, sjis_size) == 0) {
        arena_temp_end(scratch);
        return NetmdResult_Ok;
    }

    String8 request = netmd_query(arena, "00 1807 022018 02 %w 3000 0a00 5000 %w 0000 %w %*",
                                  track, (u32)sjis_size, (u32)old_size, sjis, (u32)sjis_size);
    u32 result = netmd_upload_write_title(session, arena, &netmd_desc_utoc1, request, 0);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_upload_set_disc_title(NetmdSession *session, Arena *arena, String8 title) {
    ArenaTemp scratch = arena_temp_begin(arena);
    u8 sjis[NETMD_DISC_TITLE_MAX];
    u64 sjis_size = netmd_sjis_from_utf8(title, sjis, sizeof(sjis));

    String8 current = str8(0, 0);
    (void)netmd_get_disc_title(session, arena, 0, &current);
    u8 old_sjis[NETMD_DISC_TITLE_MAX];
    u64 old_size = netmd_sjis_from_utf8(current, old_sjis, sizeof(old_sjis));
    if (old_size == sjis_size && mem_cmp(old_sjis, sjis, sjis_size) == 0) {
        arena_temp_end(scratch);
        return NetmdResult_Ok;
    }

    String8 request = netmd_query(arena, "00 1807 02201801 0000 3000 0a00 5000 %w 0000 %w %*",
                                  (u32)sjis_size, (u32)old_size, sjis, (u32)sjis_size);
    // s3.9 point 2: a Sharp renames the disc through audioUTOC1TD, not through
    // discTitleTD. The VID is the only way to know.
    const NetmdDescriptor *descriptor =
        (session->vid == 0x04DDu) ? &netmd_desc_utoc1 : &netmd_desc_disc_title;
    u32 result = netmd_upload_write_title(session, arena, descriptor, request, 1);
    arena_temp_end(scratch);
    return result;
}

// --- rendering one track into memory ------------------------------------------
// T-043 turns this into a cache file. Until then the SP bytes of one track live
// in an arena of their own: 10.6 MB per minute, released the moment the track
// is committed, and never more than one track resident.

typedef struct NetmdRenderSink {
    Arena *arena;
    u8 *base;
    u64 size;
} NetmdRenderSink;

static b32 netmd_render_write(void *user, const u8 *bytes, u64 size) {
    NetmdRenderSink *sink = (NetmdRenderSink *)user;
    u8 *dst = push_array(sink->arena, u8, size);
    if (sink->base == 0) { sink->base = dst; }
    mem_copy(dst, bytes, size);
    sink->size += size;
    return 1;
}

typedef struct NetmdMemorySource {
    const u8 *bytes;
    u64 size;
    u64 at;
} NetmdMemorySource;

static u64 netmd_memory_read(void *user, u8 *dst, u64 size) {
    NetmdMemorySource *source = (NetmdMemorySource *)user;
    u64 take = Min(size, source->size - source->at);
    mem_copy(dst, source->bytes + source->at, take);
    source->at += take;
    return take;
}

// --- the run ------------------------------------------------------------------

u32 netmd_upload_eta_s(const NetmdUploadState *state) {
    u64 total = (u64)os_atomic_load_u64((volatile u64 *)&state->bytes_total);
    u64 done = (u64)os_atomic_load_u64((volatile u64 *)&state->bytes_done) +
               (u64)os_atomic_load_u64((volatile u64 *)&state->track_bytes);
    if (done >= total) { return 0; }
    u64 left = total - done;
    u64 rate = (u64)44100u * 4u;  // s4.9: SP is real time, and PCM is 4 bytes a frame
    if (done > MB(1) && state->started_us != 0) {
        u64 elapsed_us = os_time_now_us() - state->started_us;
        if (elapsed_us > 1000000u) { rate = (done * 1000000u) / elapsed_us; }
    }
    if (rate == 0) { rate = 1; }
    return (u32)(left / rate);
}

static void netmd_upload_notify(NetmdUploadProgressFn *progress, void *user,
                                const NetmdUploadPlan *plan, const NetmdUploadState *state,
                                u32 event) {
    if (progress) { progress(user, plan, state, event); }
}

// The capacity check of s7.2, against what the *device* says is free rather
// than against what the plan believes. They should agree; that they do is the
// acceptance criterion of MD_MODE_TABLE.
static u32 netmd_upload_check_capacity(NetmdSession *session, Arena *arena,
                                       const NetmdUploadPlan *plan) {
    ArenaTemp scratch = arena_temp_begin(arena);
    NetmdCapacity capacity;
    StructZero(&capacity);
    u32 result = netmd_get_disc_capacity(session, arena, &capacity);
    arena_temp_end(scratch);
    if (result != NetmdResult_Ok) { return result; }
    u64 needed_ms = 0;
    for (u32 i = 0; i < plan->count; i += 1) {
        const NetmdUploadEntry *entry = &plan->entries[i];
        if (entry->status == NetmdUploadStatus_Done) { continue; }
        u64 clusters = (entry->duration_ms + NETMD_SP_CLUSTER_MS - 1u) / NETMD_SP_CLUSTER_MS;
        if (clusters == 0) { clusters = 1; }  // a track always costs one
        needed_ms += clusters * NETMD_SP_CLUSTER_MS;
    }
    return (needed_ms > capacity.available.ms) ? NetmdResult_NoSpace : NetmdResult_Ok;
}

typedef struct NetmdUploadPacketCtx {
    NetmdUploadPlan *plan;
    NetmdUploadState *state;
    NetmdUploadProgressFn *progress;
    void *user;
    u64 last_us;
} NetmdUploadPacketCtx;

// One notification every 250 ms rather than one per packet: the event ring is
// 64 deep and a burn is 20 000 packets.
static void netmd_upload_packet(void *user, u64 bytes_sent) {
    NetmdUploadPacketCtx *ctx = (NetmdUploadPacketCtx *)user;
    Unused(bytes_sent);
    u64 now = os_time_now_us();
    if (now - ctx->last_us < 250000u) { return; }
    ctx->last_us = now;
    netmd_upload_notify(ctx->progress, ctx->user, ctx->plan, ctx->state,
                        NetmdUploadEvent_Progress);
}

static u32 netmd_upload_one(NetmdSecure *secure, NetmdUploadPlan *plan, NetmdUploadEntry *entry,
                            NetmdUploadState *state, NetmdUploadProgressFn *progress, void *user,
                            u64 bytes_before) {
    Arena *arena = secure->arena;
    Arena *render = 0;
    Decoder *decoder = 0;
    NetmdMemorySource memory;
    StructZero(&memory);
    NetmdTrackSend send;
    StructZero(&send);
    send.wireformat = NETMD_WIREFORMAT_PCM;
    send.discformat = NETMD_DISCFORMAT_SP;
    send.cancel = &state->cancel;

    // T-043: the entry's audio may still be being rendered by a job. This is
    // where the device thread waits for it, one track at a time, which is why
    // the rendering of the next tracks can overlap the sending of this one.
    if (plan->prepare && entry->data.total_bytes == 0) {
        if (!plan->prepare(plan->prepare_user, entry, (u32)(entry - plan->entries))) {
            return NetmdResult_Malformed;
        }
    }
    if (entry->data.total_bytes != 0) {
        send.source = entry->data;
    } else {
        // A track of its own, in an arena of its own. GB(1) is reserve, not
        // commit: an 80 minute disc's worth of SP is 846 MB and this is the one
        // place that could see it.
        render = arena_alloc(GB(1));
        CodecStatus status = codec_open(&decoder, str8(entry->path, entry->path_size));
        if (status != CODEC_OK) {
            arena_release(render);
            return NetmdResult_Malformed;
        }
        PipelineSource source;
        if (!pipeline_source_from_decoder(&source, decoder)) {
            codec_close(decoder);
            arena_release(render);
            return NetmdResult_Malformed;
        }
        NetmdRenderSink sink;
        StructZero(&sink);
        sink.arena = render;
        PipelineTask *task = push_struct_zero(arena, PipelineTask);
        task->source = source;
        task->config = entry->config;
        task->config.format = PIPELINE_FORMAT_SP_BE;
        task->write = netmd_render_write;
        task->write_user = &sink;
        // The task's own scratch cannot be `render`: the rendered bytes have to
        // come out contiguous, and anything else pushing between two frames
        // would cut the block in half.
        task->arena = arena_alloc(MB(64));
        task->cancel = &state->cancel;
        pipeline_run(task);
        arena_release(task->arena);
        codec_close(decoder);
        if (task->result.status == PIPELINE_CANCELLED) {
            arena_release(render);
            return NetmdResult_Cancelled;
        }
        if (sink.size == 0) {
            arena_release(render);
            return NetmdResult_Malformed;
        }
        memory.bytes = sink.base;
        memory.size = sink.size;
        send.source.read = netmd_memory_read;
        send.source.user = &memory;
        send.source.total_bytes = sink.size;
    }

    u32 result = netmd_secure_wait_ready(secure);
    if (result == NetmdResult_Ok) { result = netmd_secure_setup_download(secure); }
    if (result == NetmdResult_Ok) {
        NetmdUploadPacketCtx ctx;
        StructZero(&ctx);
        ctx.plan = plan;
        ctx.state = state;
        ctx.progress = progress;
        ctx.user = user;
        os_atomic_store_u64((volatile u64 *)&state->track_bytes, 0);
        send.progress = &state->track_bytes;
        send.on_packet = netmd_upload_packet;
        send.packet_user = &ctx;
        result = netmd_secure_send_track(secure, &send);
        Unused(bytes_before);
        netmd_upload_notify(progress, user, plan, state, NetmdUploadEvent_Progress);
    }
    if (result == NetmdResult_Ok) {
        entry->track = send.track;
        entry->bytes = send.bytes_sent;
        // s4.12: title first, commit second. The other order costs a second TOC
        // write on some machines and loses the title on others.
        if (entry->title_size != 0) {
            (void)netmd_upload_set_track_title(secure->session, arena, send.track,
                                               str8(entry->title, entry->title_size));
        }
        result = netmd_secure_commit_track(secure, send.track);
    }
    if (render) { arena_release(render); }
    return result;
}

u32 netmd_upload_run(NetmdSession *session, Arena *arena, NetmdUploadPlan *plan,
                     NetmdUploadState *state, NetmdUploadProgressFn *progress, void *user) {
    os_atomic_store_u32(&state->active, 1);
    // `cancel` is deliberately not cleared: the user may have hit stop between
    // posting the burn and this thread reaching it, and that click must count.
    os_atomic_store_u32(&state->done, 0);
    os_atomic_store_u64((volatile u64 *)&state->bytes_done, 0);
    state->started_us = os_time_now_us();
    // The idle timer would suspend the machine in the middle of a track: a SP
    // album is 45 minutes of a progress bar and no input.
    os_power_keep_awake(1);

    u64 total = 0;
    for (u32 i = 0; i < plan->count; i += 1) {
        const NetmdUploadEntry *entry = &plan->entries[i];
        if (entry->status == NetmdUploadStatus_Done) { continue; }
        u64 bytes = entry->data.total_bytes;
        if (bytes == 0) {
            // 44100 * 4 bytes a second, rounded up to whole SP frames - the
            // same arithmetic the sender will do once the audio is rendered.
            bytes = (entry->duration_ms * (u64)44100u * 4u) / 1000u;
            bytes = ((bytes + 2047u) / 2048u) * 2048u;
        }
        total += bytes;
    }
    os_atomic_store_u64((volatile u64 *)&state->bytes_total, total);

    NetmdSecure secure;
    StructZero(&secure);
    u32 result = netmd_upload_check_capacity(session, arena, plan);
    if (result == NetmdResult_Ok) {
        netmd_secure_init(&secure, session, arena);
        if (plan->random) { netmd_secure_set_random(&secure, plan->random, plan->random_user); }
        result = netmd_secure_begin(&secure);
    }

    if (result == NetmdResult_Ok) {
        u64 bytes_before = 0;
        for (u32 i = 0; i < plan->count; i += 1) {
            NetmdUploadEntry *entry = &plan->entries[i];
            if (entry->status == NetmdUploadStatus_Done) { continue; }
            os_atomic_store_u32(&state->entry, i);
            if (os_atomic_load_u32(&state->cancel)) {
                entry->status = NetmdUploadStatus_Cancelled;
                result = NetmdResult_Cancelled;
                break;
            }
            u32 one = netmd_upload_one(&secure, plan, entry, state, progress, user, bytes_before);
            entry->result = one;
            if (one == NetmdResult_Ok) {
                entry->status = NetmdUploadStatus_Done;
                bytes_before += entry->bytes;
                os_atomic_store_u64((volatile u64 *)&state->bytes_done, bytes_before);
                os_atomic_store_u64((volatile u64 *)&state->track_bytes, 0);
                os_atomic_store_u32(&state->done, os_atomic_load_u32(&state->done) + 1);
                netmd_upload_notify(progress, user, plan, state, NetmdUploadEvent_TrackDone);
                continue;
            }
            entry->status = (one == NetmdResult_Cancelled) ? NetmdUploadStatus_Cancelled
                                                           : NetmdUploadStatus_Failed;
            result = one;
            break;
        }
    }

    // s6.5: the disc title carries the group syntax, so it is written once, at
    // the end, after every track that will ever be in a group exists. Writing
    // it per track would be one TOC cycle per track for nothing.
    if (result == NetmdResult_Ok && plan->write_disc_title) {
        (void)netmd_upload_set_disc_title(session, arena,
                                          str8(plan->disc_title, plan->disc_title_size));
    }

    // s4.14: whatever happened above, the session is closed and the device is
    // handed back. This is the only exit.
    if (secure.session == session) { netmd_secure_end(&secure); }
    os_power_keep_awake(0);
    state->result = result;
    os_atomic_store_u32(&state->active, 0);
    netmd_upload_notify(progress, user, plan, state,
                        (result == NetmdResult_Ok) ? NetmdUploadEvent_Done
                                                   : NetmdUploadEvent_Error);
    return result;
}
