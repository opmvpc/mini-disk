// view_plan.c - the plan panel: the header of the disc being built, the
// capacity gauge of research/02 s9 at the pixel, the TOC budget bar, and the
// reorderable list of what will be written (T-032).
//
// The panel owns no data. Every row reads the SoA of the disc it is on, every
// number comes from the PlanCapacity and the PlanTocBudget that were computed
// once for this revision, and every gesture ends in a plan command - which is
// what makes Ctrl+Z take back the last thing that happened, whatever it was.
//
// The geometry of the gauge and the mapping from a gesture to a command are in
// plan_view.c, which has no UI in it and is therefore what the tests drive.
#include "app_state.h"

// The mode colours are indexed by PlanCapMode, and UI_Mode is the same order:
// nothing translates between them (plan_capacity.h says so on purpose).
StaticAssert((u32)UI_Mode_SP == (u32)PlanCapMode_SP, app_mode_sp_aligned);
StaticAssert((u32)UI_Mode_Mono == (u32)PlanCapMode_Mono, app_mode_mono_aligned);
StaticAssert((u32)UI_Mode_LP2 == (u32)PlanCapMode_LP2, app_mode_lp2_aligned);
StaticAssert((u32)UI_Mode_LP4 == (u32)PlanCapMode_LP4, app_mode_lp4_aligned);


#define APP_PLAN_ROW_MAX (PLAN_ENTRY_MAX + PLAN_GROUP_MAX)
#define APP_PLAN_ROW_HEADER U16_MAX  // this row is a group header, not an entry

// The flattened list: group headers and entries in the order they are drawn,
// rebuilt at the top of every frame. A folded group contributes its header and
// nothing else. Selection and cursor are indices into *this*, and the commands
// take the entry it points at.
typedef struct AppPlanRow {
    u16 entry;
    u16 group;
} AppPlanRow;

global AppPlanRow app_plan_rows_buffer[APP_PLAN_ROW_MAX];
global u32 app_plan_row_count;
// An inline editor does not exist yet when it is asked for: it is built later
// in the frame, and this flag is what hands it the keyboard when it appears.
global b32 app_plan_focus_editor;

md_inline String8 app_ms_duration(u64 ms) { return app_duration((u32)(ms / 1000)); }

md_inline u32 app_plan_cap_mode(const PlanDisc *disc, u32 index) {
    return plan_cap_mode_of(disc, index);
}

// --- colours -------------------------------------------------------------------
// Premultiplied RGBA8 in, premultiplied out. The theme's mode colours are
// opaque, which is the only case these two are asked for.

static u32 app_color_alpha(u32 color, f32 alpha) {
    u32 a = (u32)(((f32)((color >> 24) & 0xFF)) * alpha);
    return r_rgba((u8)(color & 0xFF), (u8)((color >> 8) & 0xFF), (u8)((color >> 16) & 0xFF),
                  (u8)a);
}

// One absolutely placed rectangle inside the current parent: the gauge is drawn
// in pixels, not in a layout, because its geometry is the specification.
static UI_Box *app_gauge_rect(f32 x, f32 y, f32 width, f32 height, u32 color, f32 radius,
                              UI_Flags flags) {
    UI_Box *box = 0;
    UI_FixedX(x)
    UI_FixedY(y)
    UI_PrefWidth(ui_px(max_f32(width, 0.0f), 1.0f))
    UI_PrefHeight(ui_px(max_f32(height, 0.0f), 1.0f))
    UI_BgColor(color)
    UI_CornerRadius(radius) {
        box = ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground | flags, 0);
    }
    return box;
}

// The 45 degree hatching of s9.3, drawn with the one primitive the renderer
// has: an axis aligned rectangle. What survives of the intention is the reading
// - "this part is paid for and carries no audio" - so the area is tinted at
// 40 % and striped every four pixels. A real diagonal would need a second
// shader path, which is not worth the kilobytes it would cost (see Livraison).
#define APP_HATCH_STEP_PX 4.0f
#define APP_HATCH_MAX 64

static void app_gauge_hatch(f32 x, f32 y, f32 width, f32 height, u32 color) {
    if (width <= 0.0f) { return; }
    app_gauge_rect(x, y, width, height, app_color_alpha(color, 0.40f), 0.0f, 0);
    u32 stripes = (u32)(width / APP_HATCH_STEP_PX);
    if (stripes > APP_HATCH_MAX) { stripes = APP_HATCH_MAX; }
    for (u32 i = 0; i < stripes; i += 1) {
        app_gauge_rect(x + (f32)i * APP_HATCH_STEP_PX, y, 1.0f, height, color, 0.0f, 0);
    }
}

// --- the row model --------------------------------------------------------------
static void app_plan_rows_build(void) {
    const PlanDisc *disc = app_plan_disc();
    app_plan_row_count = 0;
    u32 i = 0;
    while (i < disc->entry_count) {
        u32 group = disc->group_id[i];
        if (group != PLAN_GROUP_NONE) {
            AppPlanRow *header = &app_plan_rows_buffer[app_plan_row_count];
            header->entry = APP_PLAN_ROW_HEADER;
            header->group = (u16)group;
            app_plan_row_count += 1;
            if (app.plan_group_collapsed & (1u << group)) {
                // The whole run belongs to the group: the ranges are derived
                // from this very column, so walking it is walking the group.
                while (i < disc->entry_count && disc->group_id[i] == group) { i += 1; }
                continue;
            }
        }
        AppPlanRow *row = &app_plan_rows_buffer[app_plan_row_count];
        row->entry = (u16)i;
        row->group = (u16)group;
        app_plan_row_count += 1;
        i += 1;
        // A group's entries are contiguous, so the header is emitted once, when
        // the run starts, and never again inside it.
        while (i < disc->entry_count && group != PLAN_GROUP_NONE &&
               disc->group_id[i] == group) {
            AppPlanRow *next = &app_plan_rows_buffer[app_plan_row_count];
            next->entry = (u16)i;
            next->group = (u16)group;
            app_plan_row_count += 1;
            i += 1;
        }
    }
}

md_inline u32 app_plan_row_entry(u64 row) {
    if (row >= app_plan_row_count) { return PLAN_ENTRY_MAX; }
    u32 entry = app_plan_rows_buffer[row].entry;
    return (entry == APP_PLAN_ROW_HEADER) ? PLAN_ENTRY_MAX : entry;
}

// The entries the selection covers, in plan order, written into `out`.
static u32 app_plan_selected_entries(u32 *out) {
    u32 count = 0;
    for (u32 row = 0; row < app_plan_row_count; row += 1) {
        if (!ui_list_selected(&app.plan_list, row)) { continue; }
        u32 entry = app_plan_row_entry(row);
        if (entry != PLAN_ENTRY_MAX) {
            out[count] = entry;
            count += 1;
        }
    }
    // A group header stands for the entries under it, folded or not.
    for (u32 row = 0; row < app_plan_row_count && count == 0; row += 1) {
        if (!ui_list_selected(&app.plan_list, row)) { continue; }
        if (app_plan_rows_buffer[row].entry != APP_PLAN_ROW_HEADER) { continue; }
        const PlanGroup *group = &app_plan_disc()->groups[app_plan_rows_buffer[row].group];
        for (u32 i = 0; i < group->count; i += 1) {
            out[count] = group->first + i;
            count += 1;
        }
    }
    return count;
}

// --- the gauge ------------------------------------------------------------------
// The layout is recomputed when the plan changed or the panel was resized, and
// the previous one is kept: the segments then slide from one to the other over
// 120 ms rather than jumping (MI-09, MI-11).
static void app_gauge_sync(f32 width) {
    b32 resized = width != app.gauge_width;
    if (app.plan_revision == app.gauge_revision && !resized) { return; }
    mem_copy(&app.gauge_from, &app.gauge, sizeof(PlanGaugeLayout));
    plan_gauge_layout(&app.capacity, width, &app.gauge);
    app.gauge_revision = app.plan_revision;
    app.gauge_width = width;
    // A resize is not a change of plan: it must not animate, or dragging the
    // splitter would drag a trail of segments behind it.
    app.gauge_t = resized ? 1.0f : 0.0f;
}

// Where segment `i` sits this frame: between the layout it came from and the
// one it is going to. Only a plan whose segments still line up one to one can
// be interpolated; anything else lands straight away.
static void app_gauge_segment_rect(u32 i, f32 *out_x, f32 *out_width, f32 *out_hatch) {
    const PlanGaugeSegment *to = &app.gauge.segments[i];
    *out_x = to->x;
    *out_width = to->width;
    *out_hatch = to->hatch_x;
    if (app.gauge_t >= 1.0f || app.gauge_from.count != app.gauge.count ||
        app.gauge_from.width != app.gauge.width) {
        return;
    }
    const PlanGaugeSegment *from = &app.gauge_from.segments[i];
    f32 t = app.gauge_t;
    *out_x = from->x + (to->x - from->x) * t;
    *out_width = from->width + (to->width - from->width) * t;
    *out_hatch = from->hatch_x + (to->hatch_x - from->hatch_x) * t;
}

static String8 app_gauge_segment_tooltip(u32 index) {
    const PlanDisc *disc = app_plan_disc();
    const PlanGaugeSegment *segment = &app.gauge.segments[index];
    u64 duration_ms = 0;
    for (u32 i = 0; i < segment->count; i += 1) {
        duration_ms += disc->duration_ms[segment->first + i];
    }
    u64 billed_ms = (u64)segment->clusters * MD_MODE_TABLE[segment->mode].cluster_ms;
    if (segment->count > 1) {
        return str8f(ui_frame_arena(), app_str_c(Str_GaugeMergedTip), segment->count,
                     app_ms_duration(duration_ms), app_mode_names[segment->mode]);
    }
    return str8f(ui_frame_arena(), app_str_c(Str_GaugeSegmentTip), segment->first + 1,
                 plan_entry_title(&app.plan, &app.library, app.plan_disc, segment->first),
                 app_ms_duration(duration_ms), app_mode_names[segment->mode],
                 app_ms_duration(billed_ms), app_ms_duration(billed_ms - duration_ms));
}

// The readout of s9.6, in its five states.
static void app_gauge_readout(f32 width) {
    const UI_Theme *theme = ui_theme();
    const PlanCapacity *capacity = &app.capacity;
    u64 used_ms = (u64)capacity->used_clusters * PLAN_CLUSTER_SP_MS;
    u64 total_ms = (u64)capacity->capacity_clusters * PLAN_CLUSTER_SP_MS;
    b32 over = capacity->overflow_clusters != 0;
    b32 full = !over && capacity->free_clusters == 0 && capacity->entry_count != 0;
    b32 nearly = !over && !full &&
                 capacity->used_clusters * 10 >= capacity->capacity_clusters * 9;
    u32 color = over ? theme->danger
                     : (full ? theme->success : (nearly ? theme->warning : theme->fg_primary));

    String8 total = str8f(ui_frame_arena(), app_str_c(Str_GaugeReadout),
                          app_ms_duration(used_ms), app_ms_duration(total_ms));
    String8 rest;
    if (over) {
        rest = str8f(ui_frame_arena(), app_str_c(Str_GaugeOverflow),
                     app_ms_duration((u64)capacity->overflow_clusters * PLAN_CLUSTER_SP_MS));
    } else if (full) {
        rest = app_str(Str_GaugeFull);
    } else if (capacity->entry_count == 0) {
        rest = str8f(ui_frame_arena(), app_str_c(Str_GaugeEmpty), capacity->length_min);
    } else {
        rest = str8f(ui_frame_arena(), app_str_c(Str_GaugeRemaining),
                     app_ms_duration(capacity->remaining_ms[PlanCapMode_SP]),
                     app_ms_duration(capacity->remaining_ms[PlanCapMode_LP2]),
                     app_ms_duration(capacity->remaining_ms[PlanCapMode_LP4]));
    }
    // The playhead readout replaces the residuals while the pointer is on the
    // bar: one line, never two, so nothing below it ever moves.
    if (app.gauge_hover != 0 || app.gauge_hover_free) {
        u64 at_ms = plan_gauge_time_at(capacity, &app.gauge, app.gauge_hover_x);
        rest = str8f(ui_frame_arena(), app_str_c(Str_GaugePlayheadTip), app_ms_duration(at_ms),
                     app_ms_duration(total_ms));
    }

    f32 height = ui_dp(PLAN_GAUGE_READOUT_DP);
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(height, 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_TextPadding(0.0f) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            UI_Font(ui_font(UI_FontStyle_Emphasis)) {
                // The size kind measures the text; the cell then draws it with a
                // padding of its own on each side, so the box has to allow for
                // both or the counter comes back ellipsised.
                app_cell(ui_text_size(app_cell_padding() * 2.0f, 1.0f), total, color,
                         UI_TextFlag_TabularNumbers, UI_TextAlign_Left);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            app_cell(ui_pct(1.0f, 0.0f), rest, theme->fg_secondary, UI_TextFlag_TabularNumbers,
                     UI_TextAlign_Left);
        }
    }
}

// The graduations of s9.4: majors every ten minutes with a label, minors every
// two, and the last major carrying the capacity of the media whatever it is.
static void app_gauge_scale(f32 width) {
    const UI_Theme *theme = ui_theme();
    u32 reference = plan_gauge_reference_mode(&app.capacity);
    u32 span_min = (u32)(((u64)app.capacity.capacity_clusters *
                          MD_MODE_TABLE[reference].cluster_ms) /
                         60000u);
    if (span_min == 0) { span_min = app.capacity.length_min; }
    f32 tick_height = ui_dp(PLAN_GAUGE_TICK_DP);

    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(tick_height, 1.0f)) {
        UI_Box *ticks = ui_build_box_from_key(0, 0);
        UI_Parent(ticks) {
            if (width >= PLAN_GAUGE_MINOR_MIN_PX) {
                for (u32 minute = 2; minute < span_min; minute += 2) {
                    if (minute % 10 == 0) { continue; }
                    f32 x = round_f32(width * (f32)minute / (f32)span_min);
                    app_gauge_rect(x, 0.0f, 1.0f, tick_height * 0.5f, theme->border_subtle, 0.0f,
                                   0);
                }
            }
            for (u32 minute = 0; minute + 10 <= span_min; minute += 10) {
                f32 x = round_f32(width * (f32)minute / (f32)span_min);
                app_gauge_rect(x, 0.0f, 1.0f, tick_height, theme->border_control, 0.0f, 0);
            }
            // s9.4: the last graduation carries the capacity of the media, 74
            // or 80, even when it is not a multiple of ten.
            app_gauge_rect(width - 1.0f, 0.0f, 1.0f, tick_height, theme->border_control, 0.0f,
                           0);
        }
    }

    // The labels, on their own band: a floating text box per major tick.
    f32 label_height = ui_dp(PLAN_GAUGE_SCALE_DP);
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(label_height, 1.0f))
    UI_Font(ui_font(UI_FontStyle_Caption))
    UI_TextColor(theme->fg_disabled)
    UI_TextFlags(UI_TextFlag_TabularNumbers)
    UI_TextPadding(0.0f) {
        UI_Box *labels = ui_build_box_from_key(0, 0);
        UI_Parent(labels) {
            f32 cell = ui_dp(28.0f);
            for (u32 minute = 0; minute <= span_min; minute += 10) {
                b32 last = (minute + 10 > span_min);
                if (last) { minute = span_min; }
                f32 x = width * (f32)minute / (f32)span_min;
                // The last label carries the media and the mode, so it is wide:
                // the graduation before it gives up its number rather than
                // being written over.
                if (!last && x + cell * 0.5f > width - cell * 3.0f) { continue; }
                String8 text = last ? str8f(ui_frame_arena(), app_str_c(Str_GaugeScaleUnit),
                                            minute, app_mode_names[reference])
                                    : str8f(ui_frame_arena(), "%u", minute);
                f32 label_width = last ? cell * 3.0f : cell;
                UI_Box *box = app_gauge_rect(clamp_f32(x - label_width * 0.5f, 0.0f,
                                                       width - label_width),
                                             0.0f, label_width, label_height, 0, 0.0f,
                                             UI_DrawText);
                box->flags &= ~(UI_Flags)UI_DrawBackground;
                box->text_align = UI_TextAlign_Center;
                box->display_string = text;
                if (last) { break; }
            }
        }
    }
}

// The TOC bar of s9.7: four pixels that say how much of the 255 cells the disc
// title, its groups and the track titles have already spent.
static void app_gauge_toc_bar(f32 width) {
    const UI_Theme *theme = ui_theme();
    const PlanTocBudget *toc = &app.toc;
    f32 height = ui_dp(PLAN_GAUGE_TOC_DP);
    u32 used = toc->cells_used;
    b32 over = used > PLAN_TOC_CELLS;
    u32 fill = over ? theme->danger
                    : ((used * 10 >= PLAN_TOC_CELLS * 8) ? theme->warning : theme->border_hover);
    f32 ratio = (f32)Min(used, PLAN_TOC_CELLS) / (f32)PLAN_TOC_CELLS;
    f32 disc_ratio = (f32)Min(toc->cells_disc, PLAN_TOC_CELLS) / (f32)PLAN_TOC_CELLS;

    UI_Box *bar = 0;
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(height, 1.0f))
    UI_BgColor(theme->control)
    UI_CornerRadius(height * 0.5f) {
        bar = ui_build_box(UI_DrawBackground | UI_Clip | UI_Clickable, str8_lit("###tocbar"));
    }
    UI_Parent(bar) {
        // The tracks first and the disc title after it, because that is the
        // order the budget is spent in (plan_toc_budget).
        f32 tracks_width = round_f32(width * (ratio - disc_ratio));
        app_gauge_rect(0.0f, 0.0f, tracks_width, height, fill, height * 0.5f, 0);
        app_gauge_rect(tracks_width, 0.0f, round_f32(width * disc_ratio), height,
                       app_color_lighten(fill, 0.25f), 0.0f, 0);
    }
    ui_tooltip_box(bar, str8f(ui_frame_arena(), app_str_c(Str_TocBreakdown), toc->cells_disc,
                              toc->groups_kept, toc->cells_tracks));
}

// "Shorten automatically": one quota for every title that is over it, applied
// as title overrides in a single gesture (s9.7, B-20).
static void app_plan_shorten_titles(void) {
    // What the tracks may spend: everything the disc title does not need. The
    // disc title is what gives way last, so it keeps what it already costs.
    u32 budget = (app.toc.cells_disc < PLAN_TOC_CELLS) ? PLAN_TOC_CELLS - app.toc.cells_disc : 0;
    plan_shorten_apply(&app.plan, &app.library, app.plan_disc, budget);
}

// The whole gauge: 56 dp of bands, in the order of s9.2.
static void app_plan_gauge(f32 panel_width) {
    const UI_Theme *theme = ui_theme();
    const PlanCapacity *capacity = &app.capacity;
    f32 side = ui_dp(PLAN_GAUGE_SIDE_DP);
    f32 width = max_f32(panel_width - side * 2.0f, 1.0f);
    f32 bar_height = ui_dp(PLAN_GAUGE_BAR_DP);
    f32 radius = ui_dp(theme->space[UI_Space_2]);
    app_gauge_sync(width);
    if (app.gauge_t < 1.0f) { app.gauge_t = ui_animate(app.gauge_t, 1.0f, UI_ANIM_RATE_FAST); }

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(PLAN_GAUGE_HEIGHT_DP), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->surface) {
        UI_Box *frame = ui_build_box(UI_DrawBackground, str8_lit("###gauge"));
        UI_Parent(frame) {
            ui_spacer(ui_px(side, 1.0f));
            UI_PrefWidth(ui_px(width, 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f))
            UI_ChildLayoutAxis(Axis2_Y) {
                UI_Box *column = ui_build_box_from_key(0, 0);
                UI_Parent(column) {
                    ui_spacer(ui_px(ui_dp(PLAN_GAUGE_MARGIN_DP), 1.0f));

                    // --- the bar ------------------------------------------
                    UI_Box *bar = 0;
                    UI_PrefWidth(ui_px(width, 1.0f))
                    UI_PrefHeight(ui_px(bar_height, 1.0f))
                    UI_BgColor(theme->control)
                    UI_BorderColor(app.gauge.overflow ? theme->danger : theme->border_subtle)
                    UI_BorderThickness(ui_dp(theme->border))
                    UI_CornerRadius(radius) {
                        bar = ui_build_box(UI_DrawBackground | UI_DrawBorder | UI_Clip |
                                               UI_Clickable,
                                           str8_lit("###gaugebar"));
                    }

                    UI_Signal signal = ui_signal(bar);
                    app.gauge_hover = 0;
                    app.gauge_hover_free = 0;
                    if (signal.hovering) {
                        app.gauge_hover_x = signal.mouse.x;
                        u32 index = plan_gauge_segment_at(&app.gauge, signal.mouse.x);
                        if (index < app.gauge.count) {
                            app.gauge_hover = index + 1;
                        } else {
                            app.gauge_hover_free = 1;
                        }
                    }

                    UI_Parent(bar) {
                        for (u32 i = 0; i < app.gauge.count; i += 1) {
                            const PlanGaugeSegment *segment = &app.gauge.segments[i];
                            f32 x, segment_width, hatch_x;
                            app_gauge_segment_rect(i, &x, &segment_width, &hatch_x);
                            u32 color = theme->mode[segment->mode];
                            if (segment->alternate) { color = app_color_lighten(color, 0.06f); }
                            if (app.gauge_hover == i + 1) { color = app_color_lighten(color, 0.12f); }
                            app_gauge_rect(x, 0.0f, segment_width, bar_height, color, 0.0f, 0);
                            app_gauge_hatch(hatch_x, 0.0f, x + segment_width - hatch_x,
                                            bar_height, color);
                            // s9.9: wide enough to carry its mode in a letter.
                            if (segment_width >= PLAN_GAUGE_INITIAL_MIN_PX) {
                                UI_Box *initial = 0;
                                UI_Font(ui_font(UI_FontStyle_Caption))
                                UI_TextColor(theme->canvas)
                                UI_TextAlign(UI_TextAlign_Center)
                                UI_TextPadding(0.0f) {
                                    initial = app_gauge_rect(x, 0.0f, segment_width, bar_height,
                                                             0, 0.0f, UI_DrawText);
                                }
                                initial->flags &= ~(UI_Flags)UI_DrawBackground;
                                initial->display_string =
                                    str8_cstr(app_mode_initials[segment->mode]);
                            }
                        }
                        // The overflow zone: the bar keeps its width and says
                        // what does not fit, rather than compressing the scale
                        // and lying about the capacity (s9.3).
                        if (app.gauge.overflow) {
                            app_gauge_hatch(app.gauge.overflow_x, 0.0f,
                                            width - app.gauge.overflow_x, bar_height,
                                            theme->danger);
                        }
                        if (app.gauge_hover != 0 || app.gauge_hover_free) {
                            app_gauge_rect(round_f32(app.gauge_hover_x), 0.0f, 1.0f, bar_height,
                                           theme->fg_primary, 0.0f, 0);
                        }
                    }
                    if (app.gauge_hover != 0) {
                        ui_tooltip_box(bar, app_gauge_segment_tooltip(app.gauge_hover - 1));
                    } else if (app.gauge_hover_free) {
                        ui_tooltip_box(
                            bar, str8f(ui_frame_arena(), app_str_c(Str_GaugeFreeTip),
                                       app_ms_duration(capacity->remaining_ms[PlanCapMode_SP]),
                                       app_ms_duration(capacity->remaining_ms[PlanCapMode_LP2]),
                                       app_ms_duration(capacity->remaining_ms[PlanCapMode_LP4])));
                    }
                    // A click on a segment selects its track and scrolls to it
                    // (s9.8): the gauge and the list are two views of one list.
                    if (signal.clicked && app.gauge_hover != 0) {
                        u32 entry = app.gauge.segments[app.gauge_hover - 1].first;
                        for (u32 row = 0; row < app_plan_row_count; row += 1) {
                            if (app_plan_row_entry(row) != entry) { continue; }
                            ui_list_select_only(&app.plan_list, row);
                            app.plan_list.cursor = row;
                            app.plan_list.has_cursor = 1;
                            ui_list_ensure_visible(&app.plan_list, row);
                            break;
                        }
                    }

                    ui_spacer(ui_px(ui_dp(PLAN_GAUGE_BAR_GAP_DP), 1.0f));
                    app_gauge_scale(width);
                    ui_spacer(ui_px(ui_dp(PLAN_GAUGE_SCALE_GAP_DP), 1.0f));
                    app_gauge_readout(width);
                    ui_spacer(ui_px(ui_dp(PLAN_GAUGE_TOC_GAP_DP), 1.0f));
                    app_gauge_toc_bar(width);
                }
            }
        }
    }

    // The TOC label and its way out, on the line under the bar.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->surface) {
        UI_Box *row = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(side, 1.0f));
            u32 chars_used = PLAN_TOC_CHARS_MAX - app.toc.chars_free;
            u32 color = app.toc.cells_used > PLAN_TOC_CELLS ? theme->danger : theme->fg_muted;
            app_cell(ui_text_size(app_cell_padding() * 2.0f, 1.0f),
                     str8f(ui_frame_arena(), app_str_c(Str_TocLabel), chars_used,
                           PLAN_TOC_CHARS_MAX),
                     color, UI_TextFlag_TabularNumbers, UI_TextAlign_Left);
            ui_spacer(ui_pct(1.0f, 0.0f));
            if (app.toc.cells_used > PLAN_TOC_CELLS) {
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
                    if (ui_button(str8f(ui_frame_arena(), "%S###shorten",
                                        app_str(Str_TocShorten)))
                            .clicked) {
                        app_plan_shorten_titles();
                    }
                    ui_tooltip(app_str(Str_TocShortenHint));
                }
            }
            ui_spacer(ui_px(side, 1.0f));
        }
    }
}

// The 12 dp variant of s9.10: one bar and the counter beside it, for the status
// bar and for the panel when it is too narrow for the real thing.
void app_plan_gauge_compact(f32 width) {
    const UI_Theme *theme = ui_theme();
    app_plan_sync();
    const PlanCapacity *capacity = &app.capacity;
    f32 bar_width = max_f32(width, 1.0f);
    f32 bar_height = ui_dp(PLAN_GAUGE_COMPACT_BAR_DP);
    PlanGaugeLayout *layout = push_struct(ui_frame_arena(), PlanGaugeLayout);
    plan_gauge_layout(capacity, bar_width, layout);

    UI_PrefWidth(ui_px(bar_width, 1.0f))
    UI_PrefHeight(ui_px(ui_dp(PLAN_GAUGE_COMPACT_DP), 1.0f))
    UI_ChildLayoutAxis(Axis2_Y) {
        UI_Box *holder = ui_build_box_from_key(0, 0);
        UI_Parent(holder) {
            UI_Box *bar = 0;
            UI_FixedY(ui_dp(2.0f))
            UI_PrefWidth(ui_px(bar_width, 1.0f))
            UI_PrefHeight(ui_px(bar_height, 1.0f))
            UI_BgColor(theme->control)
            UI_CornerRadius(bar_height * 0.5f) {
                bar = ui_build_box(UI_FloatingY | UI_DrawBackground | UI_Clip,
                                   str8_lit("###gaugemini"));
            }
            UI_Parent(bar) {
                for (u32 i = 0; i < layout->count; i += 1) {
                    const PlanGaugeSegment *segment = &layout->segments[i];
                    u32 color = theme->mode[segment->mode];
                    if (segment->alternate) { color = app_color_lighten(color, 0.06f); }
                    app_gauge_rect(segment->x, 0.0f, segment->width, bar_height, color, 0.0f, 0);
                }
                if (layout->overflow) {
                    app_gauge_rect(layout->overflow_x, 0.0f, bar_width - layout->overflow_x,
                                   bar_height, theme->danger, 0.0f, 0);
                }
            }
            ui_tooltip_box(bar, str8f(ui_frame_arena(), app_str_c(Str_StatusPlan),
                                      capacity->entry_count,
                                      app_ms_duration((u64)capacity->used_clusters *
                                                      PLAN_CLUSTER_SP_MS),
                                      app_ms_duration((u64)capacity->capacity_clusters *
                                                      PLAN_CLUSTER_SP_MS)));
        }
    }
}

// --- the rows ---------------------------------------------------------------------
// The mode badge: the one place in a row where colour carries meaning. Clicking
// it walks the cycle, on the whole selection when the row is part of it.
static void app_plan_mode_badge(u32 entry, u32 row) {
    const UI_Theme *theme = ui_theme();
    PlanDisc *disc = app_plan_disc();
    u32 mode = app_plan_cap_mode(disc, entry);
    f32 width = ui_dp(40.0f);
    f32 height = ui_dp(15.0f);
    f32 row_height = ui_dp(theme->row_compact);

    UI_Box *badge = 0;
    UI_PrefWidth(ui_px(width + ui_dp(theme->space[UI_Space_8]), 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
        UI_Box *cell = ui_build_box_from_key(0, 0);
        UI_Parent(cell)
        UI_Font(ui_font(UI_FontStyle_Caption))
        UI_FixedX(ui_dp(theme->space[UI_Space_4]))
        UI_FixedY(round_f32((row_height - height) * 0.5f))
        UI_PrefWidth(ui_px(width, 1.0f))
        UI_PrefHeight(ui_px(height, 1.0f))
        UI_BgColor(theme->mode[mode])
        UI_TextColor(theme->canvas)
        UI_TextAlign(UI_TextAlign_Center)
        UI_TextPadding(0.0f)
        UI_CornerRadius(ui_dp(3.0f)) {
            badge = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBackground | UI_DrawText |
                                     UI_Clickable,
                                 str8_lit("###badge"));
            badge->display_string = str8_cstr(app_mode_names[mode]);
        }
    }
    if (!ui_signal(badge).clicked) { return; }

    PlanModeStep step = plan_mode_cycle(disc->mode[entry],
                                        (disc->flags[entry] & PlanEntryFlag_Mono) != 0);
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = ui_list_selected(&app.plan_list, row) ? app_plan_selected_entries(entries) : 0;
    if (count <= 1) {
        plan_set_mode(&app.plan, app.plan_disc, entry, step.mode, step.mono);
        return;
    }
    // A badge clicked inside a selection moves the whole selection, in one undo
    // step (MI-12).
    plan_set_mode_entries(&app.plan, app.plan_disc, entries, count, step);
}

// The pictogram of a title the TOC will not write as it stands, and the tooltip
// that says which step of the cascade fired.
static void app_plan_title_cell(u32 entry, UI_Size width, u32 color) {
    const UI_Theme *theme = ui_theme();
    String8 source = plan_entry_title(&app.plan, &app.library, app.plan_disc, entry);
    PlanTitlePreview *preview = push_struct(ui_frame_arena(), PlanTitlePreview);
    plan_toc_preview(source, 0, preview);
    String8 written = str8(preview->text, preview->size);
    b32 changed = !str8_eq(written, source);

    app_cell(width, written, color, 0, UI_TextAlign_Left);
    if (!changed) { return; }
    UI_Box *mark = 0;
    UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_TextColor(theme->warning)
    UI_TextAlign(UI_TextAlign_Center)
    UI_TextPadding(0.0f)
    UI_Font(ui_font(UI_FontStyle_Caption)) {
        mark = ui_build_box(UI_DrawText, str8_lit("###short"));
        mark->display_string = str8_lit("\xE2\x9C\x82");  // scissors
    }
    String8 reason = str8_lit("");
    if (preview->applied & PlanShorten_Feat) { reason = app_str(Str_PlanShortenFeat); }
    if (preview->applied & PlanShorten_Brackets) {
        reason = str8f(ui_frame_arena(), "%S%S", reason, app_str(Str_PlanShortenBrackets));
    }
    if (preview->applied & PlanShorten_Artist) {
        reason = str8f(ui_frame_arena(), "%S%S", reason, app_str(Str_PlanShortenArtist));
    }
    if (preview->truncated) {
        reason = str8f(ui_frame_arena(), "%S%S", reason, app_str(Str_PlanShortenTitle));
    }
    if (reason.size == 0) { reason = app_str(Str_PlanShortenFold); }
    ui_tooltip_box(mark, str8f(ui_frame_arena(), app_str_c(Str_PlanShortenedTip), reason));
}

static void app_plan_entry_row(u32 entry, u32 row, b32 selected) {
    const UI_Theme *theme = ui_theme();
    PlanDisc *disc = app_plan_disc();
    b32 missing = (disc->flags[entry] & PlanEntryFlag_Missing) != 0;
    u32 secondary = selected ? theme->fg_primary : theme->fg_secondary;
    u32 fit = app.capacity.fit[entry];

    app_cell_number(ui_dp(30.0f), str8f(ui_frame_arena(), "%u", entry + 1),
                    (fit == PlanFit_Fits) ? theme->fg_muted : theme->danger);
    app_plan_mode_badge(entry, row);

    if (app.plan_rename_row == entry + 1) {
        // The inline editor takes the title cell whole; Enter commits, Escape
        // puts back what was there (MI-17).
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
            UI_Box *slot = ui_build_box_from_key(0, 0);
            UI_Parent(slot) {
                UI_Signal field = ui_text_input(&app.plan_rename, str8_lit(""));
                if (app_plan_focus_editor) {
                    ui_set_focus(field.box->key, 1);
                    app_plan_focus_editor = 0;
                }
            }
        }
    } else {
        app_plan_title_cell(entry, ui_pct(1.0f, 0.0f),
                            missing ? theme->danger : theme->fg_primary);
    }

    // What it came from: the artist and album of the library, or the path that
    // no longer resolves - a plan never drops an entry behind the user's back.
    String8 source;
    if (missing || !lib_track_live(&app.library, disc->track_id[entry])) {
        source = missing ? app_str(Str_PlanMissing)
                         : os_path_filename(plan_string(&app.plan, disc->path_id[entry]));
    } else {
        AppTrack track = app_track(disc->track_id[entry]);
        source = str8f(ui_frame_arena(), "%S \xC2\xB7 %S", track.artist, track.album);
    }
    app_cell(ui_px(ui_dp(110.0f), 0.0f), source, missing ? theme->danger : secondary, 0,
             UI_TextAlign_Left);
    app_cell_number(ui_dp(52.0f), app_ms_duration(disc->duration_ms[entry]), secondary);
    app_cell_number(ui_dp(48.0f), str8f(ui_frame_arena(), "%u", app.capacity.clusters[entry]),
                    theme->fg_muted);
}

// A group header: a chevron that folds it, its name, and what is inside it.
static void app_plan_group_row(u32 group, u32 row) {
    const UI_Theme *theme = ui_theme();
    PlanDisc *disc = app_plan_disc();
    const PlanGroup *info = &disc->groups[group];
    b32 collapsed = (app.plan_group_collapsed & (1u << group)) != 0;
    u64 duration_ms = 0;
    for (u32 i = 0; i < info->count; i += 1) { duration_ms += disc->duration_ms[info->first + i]; }

    UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_24]), 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
        UI_Box *cell = ui_build_box_from_key(0, 0);
        UI_Parent(cell) UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
            R_Icon icon = collapsed ? R_Icon_ChevronRight : R_Icon_ChevronDown;
            if (ui_button_icon(icon, str8_lit("###fold")).clicked) {
                app.plan_group_collapsed ^= (1u << group);
                // The rows below shift: a selection over them would no longer
                // mean the same entries.
                ui_list_select_clear(&app.plan_list);
            }
        }
    }
    if (app.plan_group_editing == group + 1) {
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
            UI_Box *slot = ui_build_box_from_key(0, 0);
            UI_Parent(slot) {
                UI_Signal field = ui_text_input(&app.plan_group_name, str8_lit(""));
                if (app_plan_focus_editor) {
                    ui_set_focus(field.box->key, 1);
                    app_plan_focus_editor = 0;
                }
            }
        }
        return;
    }
    String8 name = plan_string(&app.plan, info->name);
    if (name.size == 0) { name = app_str(Str_PlanGroupUnnamed); }
    app_cell(ui_pct(1.0f, 0.0f),
             str8f(ui_frame_arena(), app_str_c(Str_PlanGroupHeader), name, info->count,
                   app_ms_duration(duration_ms)),
             ui_list_selected(&app.plan_list, row) ? theme->fg_primary : theme->fg_secondary, 0,
             UI_TextAlign_Left);
}

// --- editing --------------------------------------------------------------------
static void app_plan_rename_open(u32 entry) {
    app.plan_rename_row = entry + 1;
    ui_text_input_init(&app.plan_rename,
                       plan_entry_title(&app.plan, &app.library, app.plan_disc, entry));
    ui_text_input_select_all(&app.plan_rename);
    app_plan_focus_editor = 1;
}

static void app_plan_rename_commit(b32 keep) {
    if (app.plan_rename_row == 0) { return; }
    u32 entry = app.plan_rename_row - 1;
    app.plan_rename_row = 0;
    if (keep && entry < app_plan_count()) {
        plan_set_title(&app.plan, app.plan_disc, entry,
                       ui_text_input_string(&app.plan_rename), os_time_now_us());
    }
    plan_coalesce_break(&app.plan);
}

static void app_plan_group_commit(b32 keep) {
    if (app.plan_group_editing == 0) { return; }
    u32 group = app.plan_group_editing - 1;
    app.plan_group_editing = 0;
    PlanDisc *disc = app_plan_disc();
    if (!keep || !(disc->group_live & (1u << group))) { return; }
    // There is no "rename a group" command: a group is its range and its name,
    // so renaming it is dissolving it and forming it again, in one gesture.
    u32 first = disc->groups[group].first;
    u32 count = disc->groups[group].count;
    plan_batch_begin(&app.plan);
    plan_ungroup(&app.plan, app.plan_disc, group);
    plan_group(&app.plan, app.plan_disc, first, count,
               ui_text_input_string(&app.plan_group_name));
    plan_batch_end(&app.plan);
}

static void app_plan_remove_selection(void) {
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = app_plan_selected_entries(entries);
    plan_remove_entries(&app.plan, app.plan_disc, entries, count);
    ui_list_select_clear(&app.plan_list);
}

static void app_plan_set_mode_selection(u32 mode, b32 mono) {
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = app_plan_selected_entries(entries);
    PlanModeStep step;
    step.mode = mode;
    step.mono = mono;
    plan_set_mode_entries(&app.plan, app.plan_disc, entries, count, step);
}

static void app_plan_move_selection(i32 direction) {
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = app_plan_selected_entries(entries);
    if (count == 0) { return; }
    u32 first = entries[0];
    u32 last = entries[count - 1];
    if ((direction < 0 && first == 0) || (direction > 0 && last + 1 >= app_plan_count())) {
        return;
    }
    plan_batch_begin(&app.plan);
    if (direction < 0) {
        for (u32 i = 0; i < count; i += 1) {
            plan_move(&app.plan, app.plan_disc, entries[i], entries[i] - 1);
        }
    } else {
        for (u32 i = count; i > 0; i -= 1) {
            plan_move(&app.plan, app.plan_disc, entries[i - 1], entries[i - 1] + 1);
        }
    }
    plan_batch_end(&app.plan);
    // The rows moved with the entries, so the selection goes there too: holding
    // Alt+Down moves the same tracks again rather than a different set.
    for (u32 i = 0; i < count; i += 1) { entries[i] = (u32)((i32)entries[i] + direction); }
    app_plan_rows_build();
    ui_list_select_clear(&app.plan_list);
    for (u32 row = 0; row < app_plan_row_count; row += 1) {
        u32 entry = app_plan_row_entry(row);
        for (u32 i = 0; i < count; i += 1) {
            if (entries[i] != entry) { continue; }
            ui_list_select_toggle(&app.plan_list, row);
            app.plan_list.cursor = row;
            app.plan_list.has_cursor = 1;
            break;
        }
    }
    ui_list_ensure_visible(&app.plan_list, app.plan_list.cursor);
}

static void app_plan_group_selection(void) {
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = app_plan_selected_entries(entries);
    if (count == 0) { return; }
    PlanDisc *disc = app_plan_disc();
    // A group is a contiguous run (B-17, and the device has no other idea of
    // one): a scattered selection is gathered first, which is what MI-19 shows.
    plan_batch_begin(&app.plan);
    u32 target = entries[0];
    for (u32 i = 0; i < count; i += 1) {
        u32 from = entries[i];
        // Everything already placed sits before `from`, so the walk is stable.
        for (u32 k = i; k < count; k += 1) {
            if (entries[k] == from) { entries[k] = target + i; }
        }
        if (from != target + i) { plan_move(&app.plan, app.plan_disc, from, target + i); }
    }
    for (u32 i = 0; i < count; i += 1) {
        if (disc->group_id[target + i] != PLAN_GROUP_NONE) {
            plan_ungroup(&app.plan, app.plan_disc, disc->group_id[target + i]);
        }
    }
    plan_group(&app.plan, app.plan_disc, target, count, str8_lit(""));
    plan_batch_end(&app.plan);
}

static void app_plan_ungroup_selection(void) {
    PlanDisc *disc = app_plan_disc();
    u32 entries[PLAN_ENTRY_MAX];
    u32 count = app_plan_selected_entries(entries);
    u32 done = 0;
    plan_batch_begin(&app.plan);
    for (u32 i = 0; i < count; i += 1) {
        u32 group = disc->group_id[entries[i]];
        if (group == PLAN_GROUP_NONE || (done & (1u << group))) { continue; }
        done |= (1u << group);
        plan_ungroup(&app.plan, app.plan_disc, group);
    }
    plan_batch_end(&app.plan);
}

// B-08: walk the library selection in order and add while it still fits.
static void app_plan_fill_remaining(void) {
    app_plan_sync();
    const u32 *rows = app_rows();
    u32 row_count = app_row_count();
    PlanDisc *disc = app_plan_disc();
    // The selection in the order it is on screen, then the walk that says where
    // it stops - the same one the tests drive.
    ArenaTemp scratch = scratch_begin(0, 0);
    u32 *durations = push_array(scratch.arena, u32, PLAN_ENTRY_MAX);
    TrackId *ids = push_array(scratch.arena, TrackId, PLAN_ENTRY_MAX);
    u32 count = 0;
    for (u32 i = 0; i < row_count && count < PLAN_ENTRY_MAX; i += 1) {
        if (!ui_list_selected(&app.list, i)) { continue; }
        ids[count] = rows[i];
        durations[count] = app.library.duration_ms[rows[i]];
        count += 1;
    }
    u32 taken = plan_fill_count(&app.capacity, durations, count,
                                plan_cap_mode(disc->default_mode, 0));
    plan_batch_begin(&app.plan);
    for (u32 i = 0; i < taken; i += 1) { app_plan_add(ids[i]); }
    plan_batch_end(&app.plan);
    scratch_end(scratch);
}

// "New disc": what does not fit moves onto one of its own, and the view follows
// it there. Nothing is dropped and nothing is reordered.
static void app_plan_new_disc(void) {
    app_plan_sync();
    u32 cut = app.capacity.first_overflow;
    if (cut >= app_plan_count()) { cut = app_plan_count(); }
    if (cut == 0) { return; }
    if (plan_split_disc(&app.plan, app.plan_disc, cut)) { app.plan_disc += 1; }
}

// The two policies of plan_capacity_split, applied as cuts. The entries keep
// the order the user gave them: a disc boundary is a cut, never a reshuffle.
static void app_plan_split_auto(u32 policy) {
    PlanDisc *disc = app_plan_disc();
    ArenaTemp scratch = scratch_begin(0, 0);
    PlanSplit *split = push_struct(scratch.arena, PlanSplit);
    plan_capacity_split(disc, &app.library, policy, disc->length_min, split);
    u32 cuts[PLAN_DISC_MAX];
    u32 cut_count = 0;
    for (u32 i = 1; i < split->entry_count && cut_count < PLAN_DISC_MAX; i += 1) {
        if (split->disc_of[i] != split->disc_of[i - 1]) {
            cuts[cut_count] = i;
            cut_count += 1;
        }
    }
    plan_batch_begin(&app.plan);
    // From the last cut backwards: an earlier cut would renumber the later ones.
    for (u32 i = cut_count; i > 0; i -= 1) {
        plan_split_disc(&app.plan, app.plan_disc, cuts[i - 1]);
    }
    plan_batch_end(&app.plan);
    scratch_end(scratch);
}

// --- files ------------------------------------------------------------------------
static void app_plan_open_file(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 path = os_dialog_open_file(scratch.arena, app_str(Str_PlanOpenTitle),
                                       app_str(Str_PlanFileFilter), str8_lit("*.mdplan"));
    if (path.size != 0 && plan_load(&app.plan, path) == PlanFile_Ok) {
        plan_resolve(&app.plan, &app.library);
        app.plan_path = str8_copy(app.permanent, path);
        app.plan_disc = 0;
        ui_list_select_clear(&app.plan_list);
    }
    scratch_end(scratch);
}

static void app_plan_save_file(b32 pick) {
    if (app.plan_path.size == 0 || pick) {
        ArenaTemp scratch = scratch_begin(0, 0);
        String8 path = os_dialog_save_file(scratch.arena, app_str(Str_PlanSaveTitle),
                                           app_str(Str_PlanFileFilter), str8_lit("*.mdplan"),
                                           app_str(Str_PlanDefaultName));
        if (path.size != 0) { app.plan_path = str8_copy(app.permanent, path); }
        scratch_end(scratch);
    }
    if (app.plan_path.size == 0) { return; }
    if (plan_save(&app.plan, app.plan_path) == PlanFile_Ok) { app.plan.dirty = 0; }
}

// --- the keyboard (research/02 s8.8) ------------------------------------------------
static void app_plan_keys(void) {
    if (app.plan_rename_row != 0 || app.plan_group_editing != 0 || ui_popup_active()) { return; }
    PlanDisc *disc = app_plan_disc();
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        b32 ctrl = (event.modifiers & OsMod_Ctrl) != 0;
        b32 shift = (event.modifiers & OsMod_Shift) != 0;
        b32 alt = (event.modifiers & OsMod_Alt) != 0;
        u32 cursor_entry = app_plan_row_entry(app.plan_list.cursor);
        switch (event.key) {
            case OsKey_Z: {
                if (ctrl) {
                    if (shift) { plan_redo_step(&app.plan); } else { plan_undo_step(&app.plan); }
                }
            } break;
            case OsKey_Y: {
                if (ctrl) { plan_redo_step(&app.plan); }
            } break;
            case OsKey_Delete: {
                app_plan_remove_selection();
            } break;
            case OsKey_F2: {
                if (cursor_entry != PLAN_ENTRY_MAX) {
                    app_plan_rename_open(cursor_entry);
                } else if (app.plan_list.cursor < app_plan_row_count) {
                    u32 group = app_plan_rows_buffer[app.plan_list.cursor].group;
                    app.plan_group_editing = group + 1;
                    ui_text_input_init(&app.plan_group_name,
                                       plan_string(&app.plan, disc->groups[group].name));
                    app_plan_focus_editor = 1;
                }
            } break;
            case OsKey_Up: {
                if (alt) { app_plan_move_selection(-1); }
            } break;
            case OsKey_Down: {
                if (alt) { app_plan_move_selection(+1); }
            } break;
            case OsKey_G: {
                if (ctrl && shift) {
                    app_plan_ungroup_selection();
                } else if (ctrl) {
                    app_plan_group_selection();
                }
            } break;
            case OsKey_M: {
                if (ctrl) {
                    ui_context_menu_open(&app.plan_menu, ui_mouse(), app.plan_list.cursor);
                }
            } break;
            case OsKey_1: {
                if (ctrl && alt) { app_plan_set_mode_selection(PlanMode_SP, 0); }
            } break;
            case OsKey_2: {
                if (ctrl && alt) { app_plan_set_mode_selection(PlanMode_SP, 1); }
            } break;
            case OsKey_3: {
                if (ctrl && alt) { app_plan_set_mode_selection(PlanMode_LP2, 0); }
            } break;
            case OsKey_4: {
                if (ctrl && alt) { app_plan_set_mode_selection(PlanMode_LP4, 0); }
            } break;
            case OsKey_S: {
                if (ctrl) { app_plan_save_file(shift); }
            } break;
            case OsKey_O: {
                if (ctrl) { app_plan_open_file(); }
            } break;
            default: break;
        }
    }
}

// --- the drag that reorders (MI-09) -------------------------------------------------
#define APP_PLAN_DRAG_THRESHOLD_PX 4.0f
#define APP_PLAN_AUTOSCROLL_EDGE_PX 24.0f
#define APP_PLAN_AUTOSCROLL_SPEED 600.0f  // pixels per second at the very edge

// The gap the pointer is in, 0 to the number of rows: rows are of one height,
// so the gap is the position rounded rather than searched for.
static u32 app_plan_drop_gap(f32 mouse_y) {
    UI_List *list = &app.plan_list;
    if (!list->viewport || list->row_height <= 0.0f) { return 0; }
    f32 local = mouse_y - list->viewport->rect.min.y + list->scroll;
    i64 gap = (i64)round_f32(local / list->row_height);
    if (gap < 0) { gap = 0; }
    if (gap > (i64)app_plan_row_count) { gap = (i64)app_plan_row_count; }
    return (u32)gap;
}

static void app_plan_autoscroll(f32 mouse_y) {
    UI_List *list = &app.plan_list;
    if (!list->viewport) { return; }
    f32 top = list->viewport->rect.min.y;
    f32 bottom = list->viewport->rect.max.y;
    f32 delta = 0.0f;
    if (mouse_y < top + APP_PLAN_AUTOSCROLL_EDGE_PX) {
        delta = -(APP_PLAN_AUTOSCROLL_EDGE_PX - (mouse_y - top));
    } else if (mouse_y > bottom - APP_PLAN_AUTOSCROLL_EDGE_PX) {
        delta = APP_PLAN_AUTOSCROLL_EDGE_PX - (bottom - mouse_y);
    }
    if (delta == 0.0f) { return; }
    f32 speed = clamp_f32(delta / APP_PLAN_AUTOSCROLL_EDGE_PX, -1.0f, 1.0f);
    f32 content = (f32)list->row_count * list->row_height;
    f32 max_scroll = max_f32(content - list->view_height, 0.0f);
    list->scroll = clamp_f32(list->scroll + speed * APP_PLAN_AUTOSCROLL_SPEED * ui_dt(), 0.0f,
                             max_scroll);
    ui_request_animation();
}

// The insertion line and the ghost, drawn inside the viewport after the rows so
// they land on top of them.
static void app_plan_drag_overlay(void) {
    const UI_Theme *theme = ui_theme();
    UI_List *list = &app.plan_list;
    if (!app.plan_drag || !list->viewport) { return; }
    f32 y = (f32)app.plan_drag_target * list->row_height - list->scroll;
    UI_Parent(list->viewport) {
        app_gauge_rect(0.0f, y - 1.0f, rect_width(list->viewport->rect), ui_dp(2.0f),
                       theme->accent, 0.0f, 0);
        // The ghost: the row that is being carried, at the pointer.
        f32 ghost_y = app.plan_drag_pos.y - list->viewport->rect.min.y - list->row_height * 0.5f;
        UI_Box *ghost = app_gauge_rect(ui_dp(theme->space[UI_Space_8]), ghost_y,
                                       rect_width(list->viewport->rect) * 0.6f, list->row_height,
                                       app_color_alpha(theme->row_selected, 0.9f),
                                       ui_dp(theme->radius), UI_DrawText);
        u32 entry = app_plan_row_entry(app.plan_drag_row);
        ghost->display_string =
            (entry != PLAN_ENTRY_MAX)
                ? plan_entry_title(&app.plan, &app.library, app.plan_disc, entry)
                : str8_lit("");
        ghost->text_color = theme->fg_primary;
        ghost->text_padding = ui_dp(theme->space[UI_Space_8]);
    }
    ui_request_animation();
}

// The drop itself: one Move, whatever the distance travelled.
static void app_plan_drag_commit(void) {
    u32 from = app_plan_row_entry(app.plan_drag_row);
    u32 target = app.plan_drag_target;
    app.plan_drag = 0;
    if (from == PLAN_ENTRY_MAX) { return; }
    // The gap is a row index; the entry it lands before is what Move takes.
    u32 before = app_plan_count();
    for (u32 row = target; row < app_plan_row_count; row += 1) {
        u32 entry = app_plan_row_entry(row);
        if (entry != PLAN_ENTRY_MAX) {
            before = entry;
            break;
        }
    }
    u32 to = plan_drop_index(from, before);
    if (to != from) { plan_move(&app.plan, app.plan_disc, from, to); }
}

// --- the panel ---------------------------------------------------------------------
b32 app_plan_hovered(V2 pos) {
    UI_List *list = &app.plan_list;
    return list->viewport != 0 && rect_contains(list->viewport->rect, pos);
}

void app_plan_drop_rows(u32 row) {
    Unused(row);  // the plan's default mode decides how, the end decides where
    app_plan_add_selection();
}

// The library drag, seen from this side: an insertion line while it is over the
// list, so letting go is never a guess (MI-04).
static void app_plan_library_drop_overlay(void) {
    const UI_Theme *theme = ui_theme();
    UI_List *list = &app.plan_list;
    if (!app.lib_drag || !list->viewport || !app_plan_hovered(app.lib_drag_pos)) { return; }
    UI_Parent(list->viewport) {
        app_gauge_rect(0.0f, (f32)app_plan_row_count * list->row_height - list->scroll - 1.0f,
                       rect_width(list->viewport->rect), ui_dp(2.0f), theme->accent, 0.0f, 0);
    }
}

static void app_plan_header(void) {
    const UI_Theme *theme = ui_theme();
    Plan *plan = &app.plan;
    PlanDisc *disc = app_plan_disc();

    // The disc tabs, when the plan holds more than one.
    if (plan->disc_count > 1) {
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
        UI_ChildLayoutAxis(Axis2_X)
        UI_BgColor(theme->panel) {
            UI_Box *tabs = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
            UI_Parent(tabs) UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
                for (u32 d = 0; d < plan->disc_count; d += 1) {
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
                    String8 label = str8f(ui_frame_arena(), "%S###tab%u",
                                          str8f(ui_frame_arena(), app_str_c(Str_PlanDiscTab),
                                                d + 1),
                                          d);
                    UI_Signal signal =
                        (d == app.plan_disc) ? ui_button_primary(label) : ui_button(label);
                    if (signal.clicked) {
                        app.plan_disc = d;
                        ui_list_select_clear(&app.plan_list);
                        app_plan_recompute();
                    }
                }
            }
        }
    }

    // The disc title, editable in place, and the autosave dot beside it.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                UI_Box *slot = ui_build_box_from_key(0, 0);
                UI_Parent(slot) {
                    if (!app.plan_disc_title_open) {
                        ui_text_input_init(&app.plan_disc_title,
                                           plan_string(plan, disc->title));
                    }
                    UI_Signal signal = ui_text_input(&app.plan_disc_title,
                                                     app_str(Str_PlanDiscTitlePlaceholder));
                    b32 focused = ui_focus_key() == signal.box->key;
                    app.plan_disc_title_open = focused;
                    if (app.plan_disc_title.changed) {
                        plan_set_disc_title(plan, app.plan_disc,
                                            ui_text_input_string(&app.plan_disc_title),
                                            os_time_now_us());
                    }
                    if (!focused) { plan_coalesce_break(plan); }
                }
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            // MI-42: the dot is there while something is unsaved, and goes away
            // without a word when it is not.
            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                UI_Box *cell = ui_build_box_from_key(0, 0);
                UI_Parent(cell)
                UI_FixedY(ui_dp(17.0f))
                UI_PrefWidth(ui_px(ui_dp(6.0f), 1.0f))
                UI_PrefHeight(ui_px(ui_dp(6.0f), 1.0f))
                UI_CornerRadius(ui_dp(3.0f))
                UI_BgColor(plan->dirty ? theme->warning : theme->success) {
                    UI_Box *dot = ui_build_box(UI_FloatingY | UI_DrawBackground,
                                               str8_lit("###dirty"));
                    ui_tooltip_box(dot, app_str(plan->dirty ? Str_PlanUnsavedHint
                                                            : Str_PlanSavedHint));
                }
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
        }
    }

    // Length, default mode, and the three file actions.
    static const u32 lengths[3] = {60, 74, 80};
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            for (u32 i = 0; i < ArrayCount(lengths); i += 1) {
                String8 label = str8f(ui_frame_arena(), "%u###len%u", lengths[i], i);
                UI_Signal signal = (disc->length_min == lengths[i]) ? ui_button_primary(label)
                                                                    : ui_button(label);
                if (signal.clicked) {
                    plan_set_disc_length(plan, app.plan_disc, lengths[i]);
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_2]), 1.0f));
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            {
                u32 mode = plan_cap_mode(disc->default_mode, 0);
                String8 label = str8f(ui_frame_arena(), "%s###defmode", app_mode_names[mode]);
                if (ui_button(label).clicked) {
                    PlanModeStep step = plan_mode_cycle(disc->default_mode, 0);
                    // The default mode is not part of the document's history:
                    // it decides what the *next* add looks like, and undoing an
                    // add must not put a mode back that nothing used.
                    disc->default_mode = (u8)(step.mono ? PlanMode_SP : step.mode);
                }
                ui_tooltip(app_str(Str_PlanDefaultMode));
            }
            ui_spacer(ui_pct(1.0f, 0.0f));
            if (ui_button_icon(R_Icon_Disc, str8_lit("###planopen")).clicked) {
                app_plan_open_file();
            }
            ui_tooltip(app_str(Str_PlanOpen));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_2]), 1.0f));
            if (ui_button_icon(R_Icon_Check, str8_lit("###plansave")).clicked) {
                app_plan_save_file(0);
            }
            ui_tooltip(app_str(Str_PlanSave));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_2]), 1.0f));
            if (ui_button_icon(R_Icon_ChevronRight, str8_lit("###plansaveas")).clicked) {
                app_plan_save_file(1);
            }
            ui_tooltip(app_str(Str_PlanSaveAs));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
        }
    }
}

static void app_plan_footer(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *row = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(row) UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###planadd", app_str(Str_ToolbarAddToPlan)))
                    .clicked) {
                app_plan_add_selection();
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###planfill", app_str(Str_PlanFill)))
                    .clicked) {
                app_plan_fill_remaining();
            }
            ui_tooltip(app_str(Str_PlanFillHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (app.capacity.overflow_clusters != 0 && app.plan.disc_count < PLAN_DISC_MAX) {
                if (ui_button(str8f(ui_frame_arena(), "%S###newdisc",
                                    app_str(Str_PlanNewDisc)))
                        .clicked) {
                    app_plan_new_disc();
                }
                ui_tooltip(app_str(Str_PlanNewDiscHint));
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###splitff",
                                    app_str(Str_PlanSplitFirstFit)))
                        .clicked) {
                    app_plan_split_auto(PlanSplit_FirstFit);
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###splitka",
                                    app_str(Str_PlanSplitKeepAlbums)))
                        .clicked) {
                    app_plan_split_auto(PlanSplit_KeepAlbums);
                }
            }
        }
    }
}

void app_plan_panel(void) {
    const UI_Theme *theme = ui_theme();
    UI_List *list = &app.plan_list;
    app_plan_sync();
    app_plan_rows_build();

    String8 subtitle = str8f(ui_frame_arena(), app_str_c(Str_PlanSubtitle), app_plan_count(),
                             app_ms_duration(app.capacity.billed_ms));
    UI_Box *panel = app_panel_begin(str8_lit("###plan"), ui_pct(1.0f, 0.0f),
                                    app_str(Str_PlanTitle), subtitle);
    app_plan_header();
    app_plan_gauge(rect_width(panel->rect));
    ui_separator();

    // The column header row: the plan's columns are fixed, so this is a label
    // row and nothing more.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel)
    UI_Font(ui_font(UI_FontStyle_Caption)) {
        UI_Box *header = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(header) {
            app_cell_number(ui_dp(30.0f), app_str(Str_ColumnIndex), theme->fg_disabled);
            app_cell(ui_px(ui_dp(48.0f), 1.0f), app_str(Str_PlanColumnMode), theme->fg_disabled,
                     0, UI_TextAlign_Left);
            app_cell(ui_pct(1.0f, 0.0f), app_str(Str_PlanColumnTitle), theme->fg_disabled, 0,
                     UI_TextAlign_Left);
            app_cell(ui_px(ui_dp(110.0f), 0.0f), app_str(Str_PlanColumnSource),
                     theme->fg_disabled, 0, UI_TextAlign_Left);
            app_cell_number(ui_dp(52.0f), app_str(Str_ColumnDuration), theme->fg_disabled);
            app_cell_number(ui_dp(48.0f), app_str(Str_PlanColumnClusters), theme->fg_disabled);
        }
    }
    ui_separator();

    if (app_plan_count() == 0) {
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_pct(1.0f, 0.0f))
        UI_ChildLayoutAxis(Axis2_Y)
        UI_BgColor(theme->surface) {
            UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
            UI_Parent(body) {
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_24]), 1.0f));
                app_centered_line(UI_FontStyle_Ui, theme->fg_muted,
                                  app_str(Str_PlanEmptyBody));
            }
        }
        app_plan_footer();
        app_panel_end();
        return;
    }

    // The list. Rows are entries and group headers; the selection is over rows
    // and every command takes the entries they point at.
    ui_list_begin(list, app_plan_row_count, ui_dp(theme->row_compact));
    if (list->focused) { app_plan_keys(); }
    UI_ListEachRow(list, row) {
        UI_Signal signal = ui_list_row_begin(list, row);
        b32 selected = ui_list_selected(list, row);
        u32 entry = app_plan_row_entry(row);
        if (entry == PLAN_ENTRY_MAX) {
            app_plan_group_row(app_plan_rows_buffer[row].group, (u32)row);
        } else {
            app_plan_entry_row(entry, (u32)row, selected);
        }
        ui_list_row_end(list);

        // A press that travels four pixels is a reorder, not a click.
        if (signal.dragging && !app.plan_drag &&
            abs_f32(signal.drag_delta.y) > APP_PLAN_DRAG_THRESHOLD_PX) {
            app.plan_drag = 1;
            app.plan_drag_row = (u32)row;
        }
    }
    if (app.plan_drag) {
        app.plan_drag_pos = ui_mouse();
        app.plan_drag_target = app_plan_drop_gap(app.plan_drag_pos.y);
        app_plan_autoscroll(app.plan_drag_pos.y);
        app_plan_drag_overlay();
        // The mouse went up: no box is active any more, wherever the row it
        // started on has scrolled to.
        if (ui_active_key() == 0) { app_plan_drag_commit(); }
    }
    app_plan_library_drop_overlay();
    ui_list_end(list);

    if (list->context) { ui_context_menu_open(&app.plan_menu, list->context_pos, list->context_row); }
    // Enter on a row edits it (s8.8); a double click does the same.
    if (list->activated) {
        u32 entry = app_plan_row_entry(list->activated_row);
        if (entry != PLAN_ENTRY_MAX) { app_plan_rename_open(entry); }
    }
    // The two inline editors commit on Enter and give up on Escape.
    if (app.plan_rename_row != 0 || app.plan_group_editing != 0) {
        b32 escape = ui_escape_pressed();
        b32 enter = 0;
        for (u32 i = 0; i < ui_key_event_count(); i += 1) {
            if (ui_key_event(i).key == OsKey_Enter) { enter = 1; }
        }
        if (enter || escape) {
            app_plan_rename_commit(enter);
            app_plan_group_commit(enter);
            ui_set_focus(list->viewport->key, 1);
        }
    }
    app_plan_footer();
    app_panel_end();
}

// --- the context menu (PP-11) ---------------------------------------------------
void app_plan_context_menu(void) {
    if (!ui_context_menu_begin(&app.plan_menu)) { return; }
    u32 entry = app_plan_row_entry(app.plan_menu.payload);
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanRename))) {
        if (entry != PLAN_ENTRY_MAX) { app_plan_rename_open(entry); }
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanRemove))) {
        app_plan_remove_selection();
    }
    ui_context_menu_separator(&app.plan_menu);
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanMoveUp))) {
        app_plan_move_selection(-1);
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanMoveDown))) {
        app_plan_move_selection(+1);
    }
    ui_context_menu_separator(&app.plan_menu);
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanModeSp))) {
        app_plan_set_mode_selection(PlanMode_SP, 0);
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanModeMono))) {
        app_plan_set_mode_selection(PlanMode_SP, 1);
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanModeLp2))) {
        app_plan_set_mode_selection(PlanMode_LP2, 0);
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanModeLp4))) {
        app_plan_set_mode_selection(PlanMode_LP4, 0);
    }
    ui_context_menu_separator(&app.plan_menu);
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanGroup))) {
        app_plan_group_selection();
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanUngroup))) {
        app_plan_ungroup_selection();
    }
    if (ui_context_menu_item(&app.plan_menu, app_str(Str_MenuPlanSplit))) {
        if (entry != PLAN_ENTRY_MAX && plan_split_disc(&app.plan, app.plan_disc, entry)) {
            app.plan_disc += 1;
        }
    }
    ui_context_menu_end(&app.plan_menu);
}

// --- the disc panel ------------------------------------------------------------
// What the gauge does not say: the legend of the four modes, and the action the
// whole plan is for. The numbers moved to the gauge, where they belong.
void app_disc_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    app_plan_sync();
    // The subtitle is the device itself, not a placeholder model name: "Aucun
    // appareil" until one answers, then its name from the PID table (T-020).
    app_panel_begin(str8_lit("###disc"), ui_px(width, 1.0f), app_str(Str_DiscTitle),
                    app_device_subtitle());

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(body) UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            // The device state comes first: with no driver bound, the guided
            // screen is the only thing in this panel worth reading (T-020).
            app_device_status();
            // The disc that is actually in the bay, when there is one (T-021):
            // its title, its groups, its tracks and its own capacity. Below it,
            // what the *plan* would still fit, which is a different question.
            app_device_disc_panel();
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            for (u32 mode = 0; mode < PlanCapMode_COUNT; mode += 1) {
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
                UI_ChildLayoutAxis(Axis2_X) {
                    UI_Box *row = ui_build_box_from_key(0, 0);
                    UI_Parent(row) {
                        UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_24]), 1.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                            UI_Box *cell = ui_build_box_from_key(0, 0);
                            UI_Parent(cell)
                            UI_FixedX(ui_dp(theme->space[UI_Space_12]))
                            UI_FixedY(ui_dp(7.0f))
                            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_CornerRadius(ui_dp(4.0f))
                            UI_BgColor(theme->mode[mode]) {
                                ui_build_box_from_key(UI_FloatingX | UI_FloatingY |
                                                          UI_DrawBackground,
                                                      0);
                            }
                        }
                        app_cell(ui_pct(1.0f, 0.0f),
                                 str8f(ui_frame_arena(), "%s \xC2\xB7 %S",
                                       app_mode_names[mode],
                                       app_ms_duration(app.capacity.remaining_ms[mode])),
                                 theme->fg_secondary, UI_TextFlag_TabularNumbers,
                                 UI_TextAlign_Left);
                    }
                }
            }

            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
            UI_ChildLayoutAxis(Axis2_X) {
                UI_Box *row = ui_build_box_from_key(0, 0);
                UI_Parent(row) {
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                    if (ui_button_primary(str8f(ui_frame_arena(), "%S###burn",
                                                app_str(Str_DiscBurn)))
                            .clicked) {
                        app_plan_burn();
                    }
                    ui_tooltip(app_str(Str_DiscBurnHint));
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                    if (ui_button(str8f(ui_frame_arena(), "%S###clear", app_str(Str_DiscClear)))
                            .clicked) {
                        app_plan_clear();
                    }
                    ui_tooltip(app_str(Str_DiscClearHint));
                }
            }
        }
    }
    app_panel_end();
}
