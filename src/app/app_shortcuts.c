// app_shortcuts.c - see app_shortcuts.h. The table is the specification: the
// list below is research/02 s8.8 typed out, and everything that reads a key
// reads it here.
#include "app_shortcuts.h"

#define SC(key, mods, ctx, action, label) \
    {(u16)(key), (u8)(mods), (u8)(ctx), (u16)(action), (u16)(label)}

static const AppShortcut app_shortcut_table[] = {
    // --- global --------------------------------------------------------------
    SC(OsKey_Comma, OsMod_Ctrl, AppShortcutContext_Global, AppAction_Settings,
       Str_ActionSettings),
    SC(OsKey_F1, 0, AppShortcutContext_Global, AppAction_KeyboardHelp,
       Str_ActionKeyboardHelp),
    // "?" is where a hand looks for help before it looks for F1 (T-075).
    SC(OsKey_Slash, OsMod_Shift, AppShortcutContext_Global, AppAction_KeyboardHelp,
       Str_ActionKeyboardHelp),
    SC(OsKey_F11, 0, AppShortcutContext_Global, AppAction_DebugOverlay,
       Str_ActionDebugOverlay),
    SC(OsKey_Escape, 0, AppShortcutContext_Global, AppAction_Cancel, Str_ActionCancel),

    // --- the library panel ---------------------------------------------------
    SC(OsKey_F, OsMod_Ctrl, AppShortcutContext_Library, AppAction_Search, Str_ActionSearch),
    SC(OsKey_A, OsMod_Ctrl, AppShortcutContext_Library, AppAction_SelectAll,
       Str_ActionSelectAll),
    SC(OsKey_Enter, 0, AppShortcutContext_Library, AppAction_AddToPlan, Str_ActionAddToPlan),

    // --- the plan panel ------------------------------------------------------
    SC(OsKey_Delete, 0, AppShortcutContext_Plan, AppAction_Remove, Str_ActionRemove),
    SC(OsKey_F2, 0, AppShortcutContext_Plan, AppAction_Rename, Str_ActionRename),
    SC(OsKey_Up, OsMod_Alt, AppShortcutContext_Plan, AppAction_MoveUp, Str_ActionMoveUp),
    SC(OsKey_Down, OsMod_Alt, AppShortcutContext_Plan, AppAction_MoveDown, Str_ActionMoveDown),
    SC(OsKey_G, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_Group, Str_ActionGroup),
    SC(OsKey_G, OsMod_Ctrl | OsMod_Shift, AppShortcutContext_Plan, AppAction_Ungroup,
       Str_ActionUngroup),
    SC(OsKey_M, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_ModePicker,
       Str_ActionModePicker),
    SC(OsKey_1, OsMod_Ctrl | OsMod_Alt, AppShortcutContext_Plan, AppAction_ModeSp,
       Str_ActionModeSp),
    SC(OsKey_2, OsMod_Ctrl | OsMod_Alt, AppShortcutContext_Plan, AppAction_ModeMono,
       Str_ActionModeMono),
    SC(OsKey_3, OsMod_Ctrl | OsMod_Alt, AppShortcutContext_Plan, AppAction_ModeLp2,
       Str_ActionModeLp2),
    SC(OsKey_4, OsMod_Ctrl | OsMod_Alt, AppShortcutContext_Plan, AppAction_ModeLp4,
       Str_ActionModeLp4),
    SC(OsKey_Z, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_Undo, Str_ActionUndo),
    SC(OsKey_Y, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_Redo, Str_ActionRedo),
    SC(OsKey_S, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_SavePlan, Str_ActionSavePlan),
    SC(OsKey_O, OsMod_Ctrl, AppShortcutContext_Plan, AppAction_OpenPlan, Str_ActionOpenPlan),

    // --- the disc panel ------------------------------------------------------
    SC(OsKey_F2, 0, AppShortcutContext_Disc, AppAction_Rename, Str_ActionRename),
    SC(OsKey_Delete, 0, AppShortcutContext_Disc, AppAction_Erase, Str_ActionErase),
    SC(OsKey_G, OsMod_Ctrl, AppShortcutContext_Disc, AppAction_Group, Str_ActionGroup),
    SC(OsKey_G, OsMod_Ctrl | OsMod_Shift, AppShortcutContext_Disc, AppAction_Ungroup,
       Str_ActionUngroup),
};

#undef SC

const AppShortcut *app_shortcuts(void) { return app_shortcut_table; }
u32 app_shortcut_count(void) { return (u32)ArrayCount(app_shortcut_table); }

AppAction app_shortcut_action(u32 key, u32 modifiers, AppShortcutContext context) {
    for (u32 i = 0; i < app_shortcut_count(); i += 1) {
        const AppShortcut *shortcut = &app_shortcut_table[i];
        if (shortcut->key != key || shortcut->modifiers != modifiers) { continue; }
        if (shortcut->context == context || shortcut->context == AppShortcutContext_Global) {
            return (AppAction)shortcut->action;
        }
    }
    return AppAction_None;
}

// The printable name of one key. The letters and the digits are their own name
// and never translated; the named keys go through the table because "Suppr" and
// "Del" are the words their users type into a support thread.
static String8 app_shortcut_key_name(Arena *arena, u32 key) {
    if (key >= OsKey_A && key <= OsKey_Z) {
        return str8f(arena, "%c", (i32)('A' + (i32)(key - OsKey_A)));
    }
    if (key >= OsKey_0 && key <= OsKey_9) {
        return str8f(arena, "%c", (i32)('0' + (i32)(key - OsKey_0)));
    }
    if (key >= OsKey_F1 && key <= OsKey_F12) {
        return str8f(arena, "F%u", key - OsKey_F1 + 1);
    }
    switch (key) {
        case OsKey_Enter: return app_str(Str_KeyEnter);
        case OsKey_Escape: return app_str(Str_KeyEscape);
        case OsKey_Delete: return app_str(Str_KeyDelete);
        case OsKey_Space: return app_str(Str_KeySpace);
        case OsKey_Tab: return app_str(Str_KeyTab);
        case OsKey_Up: return app_str(Str_KeyUp);
        case OsKey_Down: return app_str(Str_KeyDown);
        case OsKey_Comma: return str8_lit(",");
        case OsKey_Period: return str8_lit(".");
        default: break;
    }
    return str8_lit("?");
}

String8 app_shortcut_keys(Arena *arena, const AppShortcut *shortcut) {
    String8 name = app_shortcut_key_name(arena, shortcut->key);
    String8 result = name;
    if (shortcut->modifiers & OsMod_Alt) {
        result = str8f(arena, "%S+%S", app_str(Str_ModAlt), result);
    }
    if (shortcut->modifiers & OsMod_Shift) {
        result = str8f(arena, "%S+%S", app_str(Str_ModShift), result);
    }
    if (shortcut->modifiers & OsMod_Ctrl) {
        result = str8f(arena, "%S+%S", app_str(Str_ModCtrl), result);
    }
    return result;
}

u32 app_shortcut_collisions(void) {
    u32 collisions = 0;
    u32 count = app_shortcut_count();
    for (u32 i = 0; i < count; i += 1) {
        for (u32 j = i + 1; j < count; j += 1) {
            const AppShortcut *a = &app_shortcut_table[i];
            const AppShortcut *b = &app_shortcut_table[j];
            if (a->key != b->key || a->modifiers != b->modifiers) { continue; }
            // Same keystroke: legal only in two different panel contexts. A
            // global row and a panel row would make the panel unreachable.
            if (a->context == b->context || a->context == AppShortcutContext_Global ||
                b->context == AppShortcutContext_Global) {
                collisions += 1;
            }
        }
    }
    return collisions;
}
