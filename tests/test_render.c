// test_render.c - the renderer without a GPU: vertex generation, clip stack,
// layer sort, batching, skyline packing and the coverage rasterizer. The back
// end is stubbed, which is possible precisely because r_core.c is pure
// arithmetic and r_atlas.c never touches a GL type.

typedef struct TestBackend {
    u32 next_texture;
    u32 upload_count;
    u32 last_x, last_y, last_width, last_height;
} TestBackend;

global TestBackend test_backend;
global Arena *test_render_arena;  // outlives the cases: the atlas lives in it

b32 r_backend_init(void) { return 1; }
void r_backend_shutdown(void) {}
R_Vertex *r_backend_map_vertices(u32 quad_count) {
    Unused(quad_count);
    return 0;  // no mapped memory: r_core stages the vertices in the frame arena
}
void r_backend_draw(const R_Frame *frame) { Unused(frame); }

u32 r_backend_texture_r8(u32 size) {
    Unused(size);
    test_backend.next_texture += 1;
    return test_backend.next_texture;
}

void r_backend_texture_upload_r8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                 u32 width, u32 height) {
    Unused(texture);
    Unused(atlas_size);
    Unused(pixels);
    test_backend.upload_count += 1;
    test_backend.last_x = x;
    test_backend.last_y = y;
    test_backend.last_width = width;
    test_backend.last_height = height;
}

// The thumbnail atlas of T-014 has its own texture: the stub hands out ids the
// same way, so r_thumbs is exercised without a driver.
u32 r_backend_texture_rgba8(u32 size) {
    Unused(size);
    static u32 next_texture = 100;
    next_texture += 1;
    return next_texture;
}

void r_backend_texture_upload_rgba8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                    u32 width, u32 height) {
    Unused(texture); Unused(atlas_size); Unused(pixels);
    Unused(x); Unused(y); Unused(width); Unused(height);
}

static R_RectParams test_render_params(Rect dst) {
    R_RectParams params;
    StructZero(&params);
    params.dst = dst;
    params.color = r_rgb(0x804020);
    return params;
}

TEST(render_quad_geometry) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(10.0f, 20.0f, 110.0f, 70.0f));
    params.corner_radius = 6.0f;
    params.uv0 = v2(0.25f, 0.5f);
    params.uv1 = v2(0.75f, 1.0f);
    r_rect(params);
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == 1);
    const R_Vertex *v = frame->vertices;

    // The quad is grown by 1 px so the antialiasing ramp has room.
    EXPECT(v[0].dst_pos[0] == 9.0f && v[0].dst_pos[1] == 19.0f);
    EXPECT(v[1].dst_pos[0] == 111.0f && v[1].dst_pos[1] == 19.0f);
    EXPECT(v[2].dst_pos[0] == 9.0f && v[2].dst_pos[1] == 71.0f);
    EXPECT(v[3].dst_pos[0] == 111.0f && v[3].dst_pos[1] == 71.0f);

    // The logical rect is the same on the four vertices: the SDF needs it whole.
    for (u32 i = 0; i < 4; i += 1) {
        EXPECT(v[i].dst_center[0] == 60.0f && v[i].dst_center[1] == 45.0f);
        EXPECT(v[i].dst_half[0] == 50.0f && v[i].dst_half[1] == 25.0f);
        EXPECT(v[i].corner_radius == 96);  // 6 px in 1/16
        EXPECT(v[i].flags == 0);
    }

    // UV follow the corners: u from uv0.x to uv1.x, v from uv0.y to uv1.y.
    EXPECT(v[0].src_uv[0] == 0.25f && v[0].src_uv[1] == 0.5f);
    EXPECT(v[1].src_uv[0] == 0.75f && v[1].src_uv[1] == 0.5f);
    EXPECT(v[2].src_uv[0] == 0.25f && v[2].src_uv[1] == 1.0f);
    EXPECT(v[3].src_uv[0] == 0.75f && v[3].src_uv[1] == 1.0f);

    // Premultiplied RGBA8, stored little endian in the vertex.
    EXPECT(v[0].color[0] == 0x80 && v[0].color[1] == 0x40 && v[0].color[2] == 0x20);
    EXPECT(v[0].color[3] == 0xFF);
}

TEST(render_pixel_alignment) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.5f);
    // Fractional edges and a 1.5 px border, as a 150 % DPI layout produces.
    R_RectParams params = test_render_params(rect(10.4f, 20.6f, 110.2f, 70.7f));
    params.border = 1.5f;
    r_rect(params);
    r_end_frame();

    const R_Vertex *v = r_frame_state()->vertices;
    // Edges snapped to whole pixels: 10, 21, 110, 71 -> centre 60,46 half 50,25.
    EXPECT(v[0].dst_center[0] == 60.0f && v[0].dst_center[1] == 46.0f);
    EXPECT(v[0].dst_half[0] == 50.0f && v[0].dst_half[1] == 25.0f);
    EXPECT(v[0].border == 4);  // 2 px in 1/2 px units: a whole number of pixels
}

TEST(render_border_never_vanishes) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 40.0f, 40.0f));
    params.border = 0.4f;  // would round to 0 and silently become a filled rect
    r_rect(params);
    r_end_frame();
    EXPECT(r_frame_state()->vertices[0].border == 2);  // clamped to 1 px
}

TEST(render_radius_clamped_to_half_size) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 40.0f, 20.0f));
    params.corner_radius = 100.0f;  // bigger than the half size: folds the SDF
    r_rect(params);
    r_end_frame();
    EXPECT(r_frame_state()->vertices[0].corner_radius == 10 * 16);
}

TEST(render_shadow_packs_softness) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_shadow(rect(100.0f, 100.0f, 200.0f, 200.0f), r_rgb(0x000000), 0.0f, 8.0f, v2(0.0f, 0.0f));
    r_end_frame();

    const R_Vertex *v = r_frame_state()->vertices;
    EXPECT((v[0].flags & R_VertFlag_Shadow) != 0);
    EXPECT(v[0].border == 16);  // the blur radius reuses the border field
    // The quad grows by softness + 1 so the blur is not cut off.
    EXPECT(v[0].dst_pos[0] == 91.0f && v[0].dst_pos[1] == 91.0f);
    EXPECT(v[3].dst_pos[0] == 209.0f && v[3].dst_pos[1] == 209.0f);
}

TEST(render_batching_follows_clip) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f));
    r_rect(params);
    r_rect(params);

    Rect clip = rect(5.0f, 5.0f, 300.0f, 200.0f);
    r_push_clip(clip);
    r_rect(params);
    r_rect(params);
    r_pop_clip();
    // Back to the viewport clip: that is a third batch, not a return to the first.
    r_rect(params);
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->batch_count == 3);
    EXPECT(frame->batches[0].quad_first == 0 && frame->batches[0].quad_count == 2);
    EXPECT(frame->batches[1].quad_first == 2 && frame->batches[1].quad_count == 2);
    EXPECT(frame->batches[1].clip.min.x == 5.0f && frame->batches[1].clip.max.y == 200.0f);
    EXPECT(frame->batches[2].quad_first == 4 && frame->batches[2].quad_count == 1);
    EXPECT(frame->batches[2].clip.max.x == 800.0f);
}

TEST(render_batching_follows_texture) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    Rect dst = rect(0.0f, 0.0f, 10.0f, 10.0f);
    r_rect(test_render_params(dst));
    r_rect_textured(dst, 7, v2(0.0f, 0.0f), v2(1.0f, 1.0f), r_rgb(0xFFFFFF), 1);
    r_rect_textured(dst, 7, v2(0.0f, 0.0f), v2(1.0f, 1.0f), r_rgb(0xFFFFFF), 1);
    r_rect_textured(dst, 9, v2(0.0f, 0.0f), v2(1.0f, 1.0f), r_rgb(0xFFFFFF), 1);
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->batch_count == 3);
    // An untextured quad samples nothing, so it draws in whatever state is
    // bound: it belongs to the atlas batch (T-009), not to a state of its own.
    EXPECT(frame->batches[0].texture == r_atlas_texture() && frame->batches[0].quad_count == 1);
    EXPECT(frame->batches[1].texture == 7 && frame->batches[1].quad_count == 2);
    EXPECT(frame->batches[2].texture == 9 && frame->batches[2].quad_count == 1);
    // A mask is a raw quad multiplied by the R8 coverage: no distance field.
    EXPECT(frame->vertices[4].flags == (R_VertFlag_NoSdf | R_VertFlag_R8 | R_VertFlag_Texture));
    // ... and the untextured quad still carries no sampling flag at all.
    EXPECT(frame->vertices[0].flags == 0);
}

// The frame shape of the demo, in miniature: a row background with no texture
// followed by atlas text. Before T-009 that was two draw calls per row.
TEST(render_rows_and_text_share_one_batch) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    u32 atlas = r_atlas_texture();
    for (u32 row = 0; row < 40; row += 1) {
        f32 y = 10.0f + (f32)row * 12.0f;
        r_rect(test_render_params(rect(10.0f, y, 400.0f, y + 11.0f)));
        for (u32 glyph = 0; glyph < 6; glyph += 1) {
            f32 x = 14.0f + (f32)glyph * 8.0f;
            r_rect_textured(rect(x, y + 2.0f, x + 7.0f, y + 9.0f), atlas, v2(0.1f, 0.1f),
                            v2(0.2f, 0.2f), r_rgb(0xFFFFFF), 1);
        }
    }
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == 40 * 7);
    EXPECT(frame->batch_count == 1);
}

// T-071: the gauge's 45 degree hatch is an R8 pattern in the atlas, sampled as
// a repeated texture. The atlas is the texture the back end already binds for
// every untextured quad, so a hatched tail between two solid segments costs
// quads and not a draw call - which is the measurement the ticket asks for.
TEST(render_gauge_hatch_costs_no_draw_call) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    Rect bar = rect(12.0f, 40.0f, 612.0f, 54.0f);
    r_rect(test_render_params(bar));  // the free zone under everything
    u32 quads = 1;
    R_HatchTile tiles[R_HATCH_MAX_TILES];
    for (u32 segment = 0; segment < 20; segment += 1) {
        f32 x = 12.0f + (f32)segment * 30.0f;
        r_rect(test_render_params(rect(x, 40.0f, x + 30.0f, 54.0f)));
        quads += 1;
        // The hatched tail, cut on the bar's own origin.
        Rect tail = rect(x + 24.0f, 40.0f, x + 30.0f, 54.0f);
        r_rect(test_render_params(tail));  // the 40 % tint under the stripes
        quads += 1;
        u32 count = r_hatch_tiles(tail, v2(bar.min.x, bar.min.y), tiles, R_HATCH_MAX_TILES);
        EXPECT(count >= 1);
        for (u32 i = 0; i < count; i += 1) {
            r_rect_textured(tiles[i].dst, r_atlas_texture(), tiles[i].uv0, tiles[i].uv1,
                            r_rgb(0x4CAF50), 1);
            quads += 1;
        }
    }
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == quads);
    EXPECT(frame->batch_count == 1);
}

// The commands are never reordered, so a background emitted after the text of
// the row above it still covers that text. This is the case the ticket calls
// out: row n+1's background overlaps row n's descenders.
TEST(render_overlapping_rows_keep_their_order) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    u32 atlas = r_atlas_texture();
    for (u32 row = 0; row < 3; row += 1) {
        f32 y = 10.0f + (f32)row * 20.0f;
        R_RectParams params = test_render_params(rect(10.0f, y, 400.0f, y + 24.0f));
        params.color = 10 + row;  // background of row n, four pixels into row n+1
        r_rect(params);
        // The glyph overlaps the next row's background on purpose.
        r_rect_textured(rect(14.0f, y + 4.0f, 22.0f, y + 22.0f), atlas, v2(0.1f, 0.1f),
                        v2(0.2f, 0.2f), 20 + row, 1);
    }
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->batch_count == 1);  // one draw call...
    // ... and the painter's order inside it is exactly the order of the calls.
    EXPECT(frame->vertices[0].color[0] == 10);
    EXPECT(frame->vertices[4].color[0] == 20);
    EXPECT(frame->vertices[8].color[0] == 11);
    EXPECT(frame->vertices[12].color[0] == 21);
    EXPECT(frame->vertices[16].color[0] == 12);
    EXPECT(frame->vertices[20].color[0] == 22);
}

// A clip that would remove pixels is never dropped: the row crossing the
// bottom of the list keeps its own scissor, and the rows around it do not.
TEST(render_clip_kept_when_it_really_clips) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_rect(test_render_params(rect(10.0f, 10.0f, 400.0f, 40.0f)));  // outside any clip

    r_push_clip(rect(0.0f, 50.0f, 500.0f, 200.0f));
    r_rect(test_render_params(rect(10.0f, 60.0f, 400.0f, 90.0f)));    // inside the clip
    r_rect(test_render_params(rect(10.0f, 180.0f, 400.0f, 220.0f)));  // cut by the clip
    r_pop_clip();
    r_rect(test_render_params(rect(10.0f, 300.0f, 400.0f, 340.0f)));
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    // The first two quads sit inside both clips, so the change of clip costs
    // nothing. The third really is cut and opens a draw call with the list
    // scissor; the fourth is below y = 200, that scissor would erase it, so it
    // opens a third one.
    EXPECT(frame->batch_count == 3);
    EXPECT(frame->batches[0].quad_count == 2);
    EXPECT(frame->batches[0].clip.max.y == 600.0f);  // the wide clip is the kept one
    EXPECT(frame->batches[1].quad_count == 1);
    EXPECT(frame->batches[1].clip.min.y == 50.0f && frame->batches[1].clip.max.y == 200.0f);
    EXPECT(frame->batches[2].quad_count == 1);
}

// A popup is emitted while the list is still being built and must still land
// on top of it, whatever the batching does with the clips.
TEST(render_popup_stays_above_the_list) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_push_clip(rect(0.0f, 0.0f, 400.0f, 600.0f));
    for (u32 row = 0; row < 4; row += 1) {
        R_RectParams params = test_render_params(rect(10.0f, 10.0f + (f32)row * 20.0f, 390.0f,
                                                      30.0f + (f32)row * 20.0f));
        params.color = 1 + row;
        r_rect(params);
        if (row == 1) {
            // Right in the middle of the list, over the same pixels.
            r_set_layer(R_Layer_Popup);
            R_RectParams popup = test_render_params(rect(50.0f, 20.0f, 300.0f, 200.0f));
            popup.color = 99;
            r_rect(popup);
            r_set_layer(R_Layer_Content);
        }
    }
    r_pop_clip();
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == 5);
    for (u32 row = 0; row < 4; row += 1) { EXPECT(frame->vertices[row * 4].color[0] == 1 + row); }
    EXPECT(frame->vertices[16].color[0] == 99);  // the popup is last, so on top
    EXPECT(frame->batch_count == 1);
}

TEST(render_batch_splits_at_index_limit) {
    // u16 indices cap a batch at 65 536 vertices; one quad more must open a
    // second draw call even though texture and clip never changed.
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f));
    for (u32 i = 0; i < R_MAX_BATCH_QUADS + 1; i += 1) { r_rect(params); }
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->batch_count == 2);
    EXPECT(frame->batches[0].quad_count == R_MAX_BATCH_QUADS);
    EXPECT(frame->batches[1].quad_first == R_MAX_BATCH_QUADS);
    EXPECT(frame->batches[1].quad_count == 1);
}

TEST(render_clip_stack_intersects) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_push_clip(rect(100.0f, 100.0f, 400.0f, 400.0f));
    r_push_clip(rect(200.0f, 50.0f, 900.0f, 300.0f));
    Rect clip = r_clip();
    EXPECT(clip.min.x == 200.0f && clip.min.y == 100.0f);
    EXPECT(clip.max.x == 400.0f && clip.max.y == 300.0f);

    // Disjoint children collapse to an empty rect rather than going negative.
    r_push_clip(rect(0.0f, 0.0f, 50.0f, 50.0f));
    Rect empty = r_clip();
    EXPECT(rect_width(empty) == 0.0f && rect_height(empty) == 0.0f);
    r_pop_clip();
    r_pop_clip();
    EXPECT(r_clip().max.x == 400.0f);
    r_pop_clip();
    EXPECT(r_clip().max.x == 800.0f);  // back to the viewport
}

TEST(render_layer_sort_is_stable) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    // Emitted tooltip, popup, content, content: they must come out content,
    // content, popup, tooltip, and the two content rects in the order given.
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f));
    r_set_layer(R_Layer_Tooltip);
    params.color = 4;
    r_rect(params);
    r_set_layer(R_Layer_Popup);
    params.color = 3;
    r_rect(params);
    r_set_layer(R_Layer_Content);
    params.color = 1;
    r_rect(params);
    params.color = 2;
    r_rect(params);
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == 4);
    EXPECT(frame->vertices[0].color[0] == 1);
    EXPECT(frame->vertices[4].color[0] == 2);
    EXPECT(frame->vertices[8].color[0] == 3);
    EXPECT(frame->vertices[12].color[0] == 4);
    // Same texture, same clip everywhere: the sort must not cut the batch.
    EXPECT(frame->batch_count == 1);
}

TEST(render_line_1px_snaps) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_line_1px(v2(10.0f, 20.7f), v2(200.0f, 20.7f), r_rgb(0xFFFFFF));
    r_line_1px(v2(30.4f, 10.0f), v2(30.4f, 90.0f), r_rgb(0xFFFFFF));
    r_end_frame();

    const R_Frame *frame = r_frame_state();
    const R_Vertex *h = frame->vertices;
    const R_Vertex *v = frame->vertices + 4;
    // No padding on a raw quad, and the row is exactly [20, 21).
    EXPECT(h[0].dst_pos[0] == 10.0f && h[0].dst_pos[1] == 20.0f);
    EXPECT(h[3].dst_pos[0] == 200.0f && h[3].dst_pos[1] == 21.0f);
    EXPECT(h[0].flags == R_VertFlag_NoSdf);
    EXPECT(v[0].dst_pos[0] == 30.0f && v[3].dst_pos[0] == 31.0f);
}

TEST(render_frame_resets) {
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    r_rect(test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f)));
    r_push_clip(rect(0.0f, 0.0f, 10.0f, 10.0f));
    r_set_layer(R_Layer_Popup);
    r_end_frame();

    r_begin_frame(arena, 640.0f, 480.0f, 2.0f);
    const R_Frame *frame = r_frame_state();
    EXPECT(frame->cmd_count == 0 && frame->quad_count == 0 && frame->batch_count == 0);
    EXPECT(frame->viewport.x == 640.0f && frame->dpi_scale == 2.0f);
    EXPECT(frame->clip_depth == 1 && frame->layer == R_Layer_Content);
    EXPECT(r_clip().max.x == 640.0f && r_clip().max.y == 480.0f);
}

TEST(render_premultiplied_color) {
    Unused(arena);
    EXPECT(r_rgba(255, 255, 255, 0) == 0);
    EXPECT(r_rgba(255, 0, 0, 255) == 0xFF0000FFu);
    EXPECT(r_rgba(255, 255, 255, 128) == 0x80808080u);
    EXPECT(r_rgb(0x112233) == 0xFF332211u);
}

// --- atlas -----------------------------------------------------------------

static b32 test_atlas_overlap(R_AtlasRect a, R_AtlasRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

TEST(atlas_skyline_packs_without_overlap) {
    r_atlas_reset();
    u8 *pixels = push_array(arena, u8, 96 * 96);
    mem_set(pixels, 0x7F, 96 * 96);

    u32 count = 500;
    R_AtlasRect *rects = push_array(arena, R_AtlasRect, count);
    u32 area = 0;
    u32 max_y = 0;
    for (u32 i = 0; i < count; i += 1) {
        u64 noise = hash64_mix(i + 1);
        u32 w = 16 + (u32)(noise % 80);
        u32 h = 16 + (u32)((noise >> 9) % 80);
        rects[i] = r_atlas_add(w, h, pixels);
        EXPECT(rects[i].width == w && rects[i].height == h);
        area += w * h;
        max_y = Max(max_y, (u32)(rects[i].y + rects[i].height));
    }
    for (u32 i = 0; i < count; i += 1) {
        for (u32 j = i + 1; j < count; j += 1) {
            if (test_atlas_overlap(rects[i], rects[j])) {
                EXPECT(0);  // two entries would share texels
                i = count;
                break;
            }
        }
    }
    // Fill ratio over the band actually consumed, in percent.
    u32 fill = (u32)(((u64)area * 100) / ((u64)r_atlas_size() * max_y));
    EXPECT(fill > 80);
    EXPECT(area == r_atlas_used_area());
}

TEST(atlas_uv_and_padding) {
    r_atlas_reset();
    u8 pixels[4 * 4];
    mem_set(pixels, 0xFF, sizeof(pixels));
    R_AtlasRect a = r_atlas_add(4, 4, pixels);
    R_AtlasRect b = r_atlas_add(4, 4, pixels);
    f32 size = (f32)r_atlas_size();
    // One texel of padding all around: nothing sits at (0,0) and neighbours
    // are never adjacent, which is what stops bleeding under linear filtering.
    EXPECT(a.x == 1 && a.y == 1);
    EXPECT(b.x >= a.x + a.width + 2);
    EXPECT(a.uv0.x == 1.0f / size && a.uv1.x == 5.0f / size);
    EXPECT(a.uv0.y == 1.0f / size && a.uv1.y == 5.0f / size);
}

TEST(atlas_uploads_only_the_dirty_region) {
    r_atlas_reset();
    r_atlas_flush();  // the reset dirties the whole page
    u32 before = test_backend.upload_count;

    u8 pixels[8 * 8];
    mem_set(pixels, 0xFF, sizeof(pixels));
    R_AtlasRect a = r_atlas_add(8, 8, pixels);
    r_atlas_flush();
    // One upload, and it covers the entry plus its padding, not the page.
    EXPECT(test_backend.upload_count == before + 1);
    EXPECT(test_backend.last_width == 10 && test_backend.last_height == 10);
    EXPECT(test_backend.last_x == (u32)(a.x - 1) && test_backend.last_y == (u32)(a.y - 1));

    // Nothing new: no upload at all.
    before = test_backend.upload_count;
    r_atlas_flush();
    EXPECT(test_backend.upload_count == before);
}

TEST(atlas_grows_when_full) {
    r_atlas_reset();
    u32 size_before = r_atlas_size();
    u32 texture_before = r_atlas_texture();
    u8 *pixels = push_array_zero(arena, u8, 1024 * 1024);
    // 5 x 1024x1024 does not fit in 2048x2048 but fits once the page doubles.
    R_AtlasRect last;
    StructZero(&last);
    for (u32 i = 0; i < 5; i += 1) { last = r_atlas_add(1024, 1024, pixels); }
    EXPECT(last.width == 1024);
    EXPECT(r_atlas_size() == size_before * 2);
    EXPECT(r_atlas_texture() != texture_before);  // a new, bigger texture
}

// --- rasterizer ------------------------------------------------------------

TEST(raster_square_coverage_is_the_exact_area) {
    u32 size = 8;
    u8 *coverage = push_array_zero(arena, u8, size * size);
    // A square with fractional edges: every pixel it touches must receive the
    // exact area it covers, not a filtered approximation.
    V2 points[4] = {v2(1.25f, 1.25f), v2(4.75f, 1.25f), v2(4.75f, 4.75f), v2(1.25f, 4.75f)};
    u32 contour = 4;
    r_raster_fill(arena, coverage, size, size, points, &contour, 1);

    EXPECT(coverage[1 * size + 1] == (u8)(0.75f * 0.75f * 255.0f + 0.5f));  // corner: 0.5625
    EXPECT(coverage[1 * size + 2] == (u8)(0.75f * 255.0f + 0.5f));          // top edge
    EXPECT(coverage[2 * size + 1] == (u8)(0.75f * 255.0f + 0.5f));          // left edge
    EXPECT(coverage[2 * size + 2] == 255);                                  // fully inside
    EXPECT(coverage[4 * size + 4] == (u8)(0.75f * 0.75f * 255.0f + 0.5f));  // far corner
    EXPECT(coverage[0] == 0 && coverage[5 * size + 5] == 0);                // outside
}

TEST(raster_hole_is_wound_backwards) {
    u32 size = 16;
    u8 *coverage = push_array_zero(arena, u8, size * size);
    V2 points[8] = {
            v2(2.0f, 2.0f), v2(14.0f, 2.0f), v2(14.0f, 14.0f), v2(2.0f, 14.0f),   // outer
            v2(6.0f, 6.0f), v2(6.0f, 10.0f), v2(10.0f, 10.0f), v2(10.0f, 6.0f),   // hole
    };
    u32 contours[2] = {4, 4};
    r_raster_fill(arena, coverage, size, size, points, contours, 2);
    EXPECT(coverage[3 * size + 3] == 255);  // inside the ring
    EXPECT(coverage[8 * size + 8] == 0);    // inside the hole
    EXPECT(coverage[0] == 0);
}

TEST(icons_land_in_the_atlas) {
    r_atlas_reset();
    r_icons_build(arena, 24);
    for (u32 i = 0; i < R_Icon_COUNT; i += 1) {
        R_AtlasRect icon = r_icon_rect((R_Icon)i);
        EXPECT(icon.width == 24 && icon.height == 24);
        EXPECT(icon.uv1.x > icon.uv0.x && icon.uv1.y > icon.uv0.y);
    }
    // Eight distinct placements, no two icons on the same texels.
    for (u32 i = 0; i < R_Icon_COUNT; i += 1) {
        for (u32 j = i + 1; j < R_Icon_COUNT; j += 1) {
            EXPECT(!test_atlas_overlap(r_icon_rect((R_Icon)i), r_icon_rect((R_Icon)j)));
        }
    }
}

static void test_render_run_all(void) {
    test_render_arena = arena_alloc(MB(256));
    r_atlas_init(test_render_arena);

    RUN(render_quad_geometry);
    RUN(render_pixel_alignment);
    RUN(render_border_never_vanishes);
    RUN(render_radius_clamped_to_half_size);
    RUN(render_shadow_packs_softness);
    RUN(render_batching_follows_clip);
    RUN(render_batching_follows_texture);
    RUN(render_rows_and_text_share_one_batch);
    RUN(render_gauge_hatch_costs_no_draw_call);
    RUN(render_overlapping_rows_keep_their_order);
    RUN(render_clip_kept_when_it_really_clips);
    RUN(render_popup_stays_above_the_list);
    RUN(render_batch_splits_at_index_limit);
    RUN(render_clip_stack_intersects);
    RUN(render_layer_sort_is_stable);
    RUN(render_line_1px_snaps);
    RUN(render_frame_resets);
    RUN(render_premultiplied_color);
    RUN(atlas_skyline_packs_without_overlap);
    RUN(atlas_uv_and_padding);
    RUN(atlas_uploads_only_the_dirty_region);
    RUN(atlas_grows_when_full);
    RUN(raster_square_coverage_is_the_exact_area);
    RUN(raster_hole_is_wound_backwards);
    RUN(icons_land_in_the_atlas);

    arena_release(test_render_arena);
}
