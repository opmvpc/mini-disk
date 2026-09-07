// view_device.c - the device half of the Disc panel (T-020): what the panel
// says when there is no device, when there is one Windows has no driver for
// (P-001), and when the MZ-N505 is actually talking to us.
//
// It owns the device thread and is the only place that speaks to it: the panel
// drains its queue once per frame and never blocks (ADR-008).

typedef enum AppDeviceState {
    AppDeviceState_None = 0,   // nothing on the bus
    AppDeviceState_NoDriver,   // present, code 28: the guided screen
    AppDeviceState_InUse,      // another application holds it
    AppDeviceState_Connected,  // open, and it answered a ping
    AppDeviceState_Error,
    AppDeviceState_COUNT
} AppDeviceState;

typedef struct AppDevice {
    NetmdDevice thread;
    u32 state;  // AppDeviceState
    u16 vid;
    u16 pid;
    i32 error;
    u32 name_size;
    u8 name[NETMD_NAME_MAX];
    // What the acceptance criterion measures: the moment the hotplug event
    // reached us, and the moment the panel changed because of it.
    u64 change_us;
    b32 change_pending;
} AppDevice;

global AppDevice app_device;

static String8 app_device_name(void) {
    return str8(app_device.name, app_device.name_size);
}

void app_device_init(void) {
    // 4 MB of its own: an enumeration walks the bus into it and rewinds, so it
    // never grows, and no other thread ever pushes into it.
    netmd_device_start(&app_device.thread, arena_alloc(MB(4)));
    netmd_device_post(&app_device.thread, NetmdCmd_Enumerate, 0);
}

void app_device_shutdown(void) { netmd_device_stop(&app_device.thread); }

// One OsEvent_DeviceChange: the thread debounces the burst, we only time it.
void app_device_changed(void) {
    if (!app_device.change_pending) {
        app_device.change_us = os_time_now_us();
        app_device.change_pending = 1;
    }
    netmd_device_post(&app_device.thread, NetmdCmd_DeviceChanged, 0);
}

static void app_device_set_state(u32 state) {
    if (app_device.state == state) { return; }
    app_device.state = state;
    if (app_device.change_pending) {
        u64 elapsed_ms = (os_time_now_us() - app_device.change_us) / 1000;
        app_device.change_pending = 0;
        // The measurement the acceptance criterion asks for, in the debug
        // output: plug to panel, end to end. A scratch arena and not the frame
        // one - this runs before the frame is begun.
        ArenaTemp scratch = scratch_begin(0, 0);
        os_debug_print(str8f(scratch.arena, "device: panel updated %llu ms after hotplug\n",
                             elapsed_ms));
        scratch_end(scratch);
    }
}

// Drained once per frame, like the library's scan events.
void app_device_tick(void) {
    NetmdEvent event;
    while (netmd_device_next_event(&app_device.thread, &event)) {
        app_device.vid = event.vid;
        app_device.pid = event.pid;
        app_device.name_size = Min(event.name_size, (u32)NETMD_NAME_MAX);
        if (app_device.name_size != 0) {
            mem_copy(app_device.name, event.name, app_device.name_size);
        }
        switch (event.kind) {
            case NetmdEvent_Devices: {
                if (event.device_count == 0) {
                    app_device_set_state(AppDeviceState_None);
                    break;
                }
                if (event.state == OsUsbState_NoDriver) {
                    app_device_set_state(AppDeviceState_NoDriver);
                } else if (event.state == OsUsbState_InUse) {
                    app_device_set_state(AppDeviceState_InUse);
                } else {
                    // Ready is not connected yet: the handle has to answer
                    // first, which is what the ping is for.
                    netmd_device_post(&app_device.thread, NetmdCmd_Open, 0);
                }
            } break;
            case NetmdEvent_Opened: netmd_device_post(&app_device.thread, NetmdCmd_Ping, 0); break;
            case NetmdEvent_Pong: app_device_set_state(AppDeviceState_Connected); break;
            case NetmdEvent_Closed: app_device_set_state(AppDeviceState_None); break;
            case NetmdEvent_Error: {
                app_device.error = event.error;
                app_device_set_state(event.state == OsUsbState_InUse ? AppDeviceState_InUse
                                                                     : AppDeviceState_Error);
            } break;
            default: break;
        }
    }
}

// The panel header line, right of the title.
String8 app_device_subtitle(void) {
    if (app_device.state == AppDeviceState_Connected) { return app_device_name(); }
    if (app_device.state == AppDeviceState_None) { return app_str(Str_DeviceNone); }
    return app_str(Str_DeviceNoDriver);
}

md_inline b32 app_device_connected(void) {
    return app_device.state == AppDeviceState_Connected;
}

static void app_device_line(UI_FontStyle style, u32 color, String8 text) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_TextAlign(UI_TextAlign_Left)
    UI_Font(ui_font(style))
    UI_TextColor(color)
    UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

// The three steps and the one button of P-001, in the panel itself: the first
// wall every user of this app hits is the missing driver, so it is not hidden
// behind a dialog.
static void app_device_driver_help(void) {
    const UI_Theme *theme = ui_theme();
    app_device_line(UI_FontStyle_Emphasis, theme->fg_primary, app_str(Str_DeviceNoDriver));
    String8 name = app_device_name();
    if (name.size == 0) { name = str8_lit("Net MD Walkman"); }
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                    str8f(ui_frame_arena(), app_str_c(Str_DeviceNoDriverBody), name));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep1));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep2));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep3));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###zadig", app_str(Str_DeviceZadig)))
                        .clicked) {
                os_open_url(str8_lit("https://zadig.akeo.ie"));
            }
            ui_tooltip(app_str(Str_DeviceZadigHint));
        }
    }
}

// What the Disc panel shows above the capacity readout.
void app_device_status(void) {
    const UI_Theme *theme = ui_theme();
    switch (app_device.state) {
        case AppDeviceState_None: {
            app_device_line(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_DeviceNone));
            app_device_line(UI_FontStyle_Caption, theme->fg_disabled,
                            app_str(Str_DeviceNoneHint));
        } break;
        case AppDeviceState_NoDriver: app_device_driver_help(); break;
        case AppDeviceState_InUse: {
            app_device_line(UI_FontStyle_Ui, theme->fg_secondary, app_str(Str_DeviceInUse));
        } break;
        case AppDeviceState_Connected: {
            app_device_line(UI_FontStyle_Emphasis, theme->fg_primary,
                            str8f(ui_frame_arena(), app_str_c(Str_DeviceConnected),
                                  app_device_name()));
        } break;
        default: {
            app_device_line(UI_FontStyle_Ui, theme->fg_secondary,
                            str8f(ui_frame_arena(), app_str_c(Str_DeviceError),
                                  app_device.error));
        } break;
    }
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
    ui_separator();
}
