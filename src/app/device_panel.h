// device_panel.h - the one state the Disc panel is in, and the two strings that
// say it (T-071).
//
// Before this file the header line and the body of the panel each decided for
// themselves what the device was doing, out of the same AppDeviceState but with
// two different sets of ifs. On 2026-09-07 that produced a header reading
// "pilote manquant" over a body reading "utilise par une autre application" -
// both true of some state, neither true of the same one.
//
// So: one enum, one function that computes it, and two lookups that turn it
// into strings. No UI, no globals, nothing to draw: a test can walk the whole
// table of inputs and check both strings, which is exactly what the ticket asks
// for and what the two sets of ifs made impossible.
#ifndef APP_DEVICE_PANEL_H
#define APP_DEVICE_PANEL_H

#include "../base/base.h"
#include "strings.h"

// What the device thread has told us. The panel never reads anything else.
typedef enum AppDeviceState {
    AppDeviceState_None = 0,   // nothing on the bus
    AppDeviceState_NoDriver,   // present, code 28: the guided screen (P-001)
    AppDeviceState_InUse,      // another application holds it
    AppDeviceState_Connected,  // open, and it answered a ping
    AppDeviceState_Error,
    AppDeviceState_COUNT
} AppDeviceState;

// What the panel says. It is the device state plus the one thing the device
// state cannot know: that we are in the middle of writing to it.
typedef enum NetmdPanelState {
    NetmdPanel_NoDevice = 0,
    NetmdPanel_NoDriver,
    NetmdPanel_InUse,
    NetmdPanel_Connected,
    NetmdPanel_Unreachable,
    NetmdPanel_Burning,
    NetmdPanel_COUNT
} NetmdPanelState;

// Pure. `burning` wins over `connected` and over nothing else: a burn that
// loses the cable is an unreachable device, not a burn.
md_inline u32 netmd_panel_state(u32 device_state, b32 burning) {
    switch (device_state) {
        case AppDeviceState_None: return NetmdPanel_NoDevice;
        case AppDeviceState_NoDriver: return NetmdPanel_NoDriver;
        case AppDeviceState_InUse: return NetmdPanel_InUse;
        case AppDeviceState_Connected:
            return burning ? (u32)NetmdPanel_Burning : (u32)NetmdPanel_Connected;
        default: return NetmdPanel_Unreachable;
    }
}

// The header line, right of the panel title. Connected answers a bare "%S": the
// device's own name is the shortest true thing that line can say.
md_inline Str netmd_panel_header_string(u32 panel) {
    switch (panel) {
        case NetmdPanel_NoDevice: return Str_DeviceNone;
        case NetmdPanel_NoDriver: return Str_DeviceNoDriver;
        case NetmdPanel_InUse: return Str_DeviceInUse;
        case NetmdPanel_Connected: return Str_DevicePanelName;
        case NetmdPanel_Burning: return Str_DeviceBurning;
        default: return Str_DeviceUnreachable;
    }
}

// The first line of the body. The states that carry a second, explanatory line
// answer it through netmd_panel_hint_string; NetmdPanel_NoDriver's body is the
// three steps and the button of P-001, which is why its hint is its own body
// paragraph and not a one liner.
md_inline Str netmd_panel_body_string(u32 panel) {
    switch (panel) {
        case NetmdPanel_NoDevice: return Str_DeviceNone;
        case NetmdPanel_NoDriver: return Str_DeviceNoDriver;
        case NetmdPanel_InUse: return Str_DeviceInUse;
        case NetmdPanel_Connected: return Str_DeviceConnected;
        case NetmdPanel_Burning: return Str_DeviceBurning;
        default: return Str_DeviceUnreachable;
    }
}

// The muted second line, or Str_COUNT when the state has nothing to add.
md_inline Str netmd_panel_hint_string(u32 panel) {
    switch (panel) {
        case NetmdPanel_NoDevice: return Str_DeviceNoneHint;
        case NetmdPanel_NoDriver: return Str_DeviceNoDriverBody;
        case NetmdPanel_InUse: return Str_DeviceInUseHint;
        case NetmdPanel_Burning: return Str_DeviceBurningHint;
        case NetmdPanel_Unreachable: return Str_DeviceUnreachableHint;
        default: return Str_COUNT;
    }
}

#endif  // APP_DEVICE_PANEL_H
