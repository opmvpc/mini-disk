// test_render.c - vertex generation of the SDF rect: geometry, UV, batching.
// The GL back end is stubbed out: r_core.c is pure arithmetic by construction.

static b32 r_gl_init(void) { return 1; }
static void r_gl_shutdown(void) {}
static void r_gl_draw(const R_Frame *frame) { Unused(frame); }

static R_RectParams test_render_params(Rect dst) {
    R_RectParams params;
    StructZero(&params);
    params.dst = dst;
    params.color = r_rgb(0x804020);
    return params;
}

TEST(render_quad_geometry) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(10.0f, 20.0f, 110.0f, 70.0f));
    params.corner_radius = 6.0f;
    params.uv0 = v2(0.25f, 0.5f);
    params.uv1 = v2(0.75f, 1.0f);
    r_rect(params);

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
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.5f);
    // Fractional edges and a 1.5 px border, as a 150 % DPI layout produces.
    R_RectParams params = test_render_params(rect(10.4f, 20.6f, 110.2f, 70.7f));
    params.border = 1.5f;
    r_rect(params);

    const R_Vertex *v = r_frame_state()->vertices;
    // Edges snapped to whole pixels: 10, 21, 110, 71 -> centre 60,46 half 50,25.
    EXPECT(v[0].dst_center[0] == 60.0f && v[0].dst_center[1] == 46.0f);
    EXPECT(v[0].dst_half[0] == 50.0f && v[0].dst_half[1] == 25.0f);
    EXPECT(v[0].border == 4);  // 2 px in 1/2 px units: a whole number of pixels
}

TEST(render_border_never_vanishes) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 40.0f, 40.0f));
    params.border = 0.4f;  // would round to 0 and silently become a filled rect
    r_rect(params);
    EXPECT(r_frame_state()->vertices[0].border == 2);  // clamped to 1 px
}

TEST(render_radius_clamped_to_half_size) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 40.0f, 20.0f));
    params.corner_radius = 100.0f;  // bigger than the half size: folds the SDF
    r_rect(params);
    EXPECT(r_frame_state()->vertices[0].corner_radius == 10 * 16);
}

TEST(render_shadow_packs_softness) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(100.0f, 100.0f, 200.0f, 200.0f));
    params.softness = 8.0f;
    r_rect(params);

    const R_Vertex *v = r_frame_state()->vertices;
    EXPECT((v[0].flags & R_VertFlag_Shadow) != 0);
    EXPECT(v[0].border == 16);  // the blur radius reuses the border field
    // The quad grows by softness + 1 so the blur is not cut off.
    EXPECT(v[0].dst_pos[0] == 91.0f && v[0].dst_pos[1] == 91.0f);
    EXPECT(v[3].dst_pos[0] == 209.0f && v[3].dst_pos[1] == 209.0f);
}

TEST(render_batching_follows_clip) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    R_RectParams params = test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f));
    r_rect(params);
    r_rect(params);
    EXPECT(r_frame_state()->batch_count == 1);
    EXPECT(r_frame_state()->batches[0].index_count == 12);

    Rect clip = rect(5.0f, 5.0f, 300.0f, 200.0f);
    r_set_clip(clip);
    r_rect(params);
    const R_Frame *frame = r_frame_state();
    EXPECT(frame->batch_count == 2);
    EXPECT(frame->batches[1].index_first == 12);
    EXPECT(frame->batches[1].index_count == 6);
    EXPECT(frame->batches[1].clip.min.x == clip.min.x && frame->batches[1].clip.max.y == clip.max.y);

    // Same clip again: the batch grows instead of opening a new draw call.
    r_set_clip(clip);
    r_rect(params);
    EXPECT(r_frame_state()->batch_count == 2);
    EXPECT(r_frame_state()->batches[1].index_count == 12);
}

TEST(render_frame_resets) {
    Unused(arena);
    r_begin_frame(800.0f, 600.0f, 1.0f);
    r_rect(test_render_params(rect(0.0f, 0.0f, 10.0f, 10.0f)));
    r_begin_frame(640.0f, 480.0f, 2.0f);
    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count == 0 && frame->batch_count == 0);
    EXPECT(frame->viewport.x == 640.0f && frame->dpi_scale == 2.0f);
    EXPECT(frame->clip.max.x == 640.0f && frame->clip.max.y == 480.0f);
}

TEST(render_premultiplied_color) {
    Unused(arena);
    EXPECT(r_rgba(255, 255, 255, 0) == 0);
    EXPECT(r_rgba(255, 0, 0, 255) == 0xFF0000FFu);
    EXPECT(r_rgba(255, 255, 255, 128) == 0x80808080u);
    EXPECT(r_rgb(0x112233) == 0xFF332211u);
}

static void test_render_run_all(void) {
    RUN(render_quad_geometry);
    RUN(render_pixel_alignment);
    RUN(render_border_never_vanishes);
    RUN(render_radius_clamped_to_half_size);
    RUN(render_shadow_packs_softness);
    RUN(render_batching_follows_clip);
    RUN(render_frame_resets);
    RUN(render_premultiplied_color);
}
