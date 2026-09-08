// app_shortcuts.h - the keyboard model of research/02 s8.8 as one table
// (T-072). The dispatch, the keyboard help overlay and the status bar hints all
// read this array and nothing else: a shortcut that is not in it does not
// exist, and a shortcut that is in it is documented by construction.
//
// A row is (key, modifiers, context, action, label). The label is a Str id, so
// the help overlay is translated for free; the key names go through the string
// table too, because "Entrée" and "Enter" are not the same word.
#ifndef APP_SHORTCUTS_H
#define APP_SHORTCUTS_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "../base/base_string.h"
#include "../platform/platform.h"
#include "strings.h"

// Which panel the keystroke belongs to. Global rows answer everywhere, and the
// help overlay lists the global rows plus the ones of the panel in focus.
typedef enum AppShortcutContext {
    AppShortcutContext_Global = 0,
    AppShortcutContext_Library,
    AppShortcutContext_Plan,
    AppShortcutContext_Disc,
    AppShortcutContext_COUNT
} AppShortcutContext;

// What the row asks for. Only the rows app.c dispatches itself carry an action
// that does something here; the others are owned by their panel (which reads
// the same key out of ui_key_event) and are in the table so the help is
// complete and so the uniqueness test covers them.
typedef enum AppAction {
    AppAction_None = 0,
    AppAction_Settings,
    AppAction_KeyboardHelp,
    AppAction_DebugOverlay,
    AppAction_Search,
    AppAction_SelectAll,
    AppAction_AddToPlan,
    AppAction_Remove,
    AppAction_Rename,
    AppAction_MoveUp,
    AppAction_MoveDown,
    AppAction_Group,
    AppAction_Ungroup,
    AppAction_ModePicker,
    AppAction_ModeSp,
    AppAction_ModeMono,
    AppAction_ModeLp2,
    AppAction_ModeLp4,
    AppAction_Undo,
    AppAction_Redo,
    AppAction_SavePlan,
    AppAction_OpenPlan,
    AppAction_Erase,
    AppAction_Cancel,
    AppAction_COUNT
} AppAction;

// 8 bytes: the table is read once per keystroke and once per help overlay, so
// it is not a hot loop, but it is also not a reason to spend 32 bytes a row.
typedef struct AppShortcut {
    u16 key;      // OsKey
    u8 modifiers; // OsMod mask, exact match
    u8 context;   // AppShortcutContext
    u16 action;   // AppAction
    u16 label;    // Str
} AppShortcut;

const AppShortcut *app_shortcuts(void);
u32 app_shortcut_count(void);

// The action bound to this keystroke in this context, or in the global one.
// AppAction_None when nothing is.
AppAction app_shortcut_action(u32 key, u32 modifiers, AppShortcutContext context);

// "Ctrl+Maj+G", in the language of the moment. Allocated in `arena`.
String8 app_shortcut_keys(Arena *arena, const AppShortcut *shortcut);

// The invariant the test checks: no (key, modifiers) appears twice in the same
// context, and no context row shadows a global one. Returns the number of
// collisions, so a failure says how many.
u32 app_shortcut_collisions(void);

#endif // APP_SHORTCUTS_H
