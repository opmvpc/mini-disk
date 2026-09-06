// win32_dialog.c - the system folder picker behind os_dialog_pick_folder.
//
// ole32 and shell32 are loaded on the first call so the import table stays
// kernel32 + user32, and the SDK's shobjidl.h is C++ only, so IFileDialog and
// IShellItem are declared here as plain vtables, exactly as win32_font_dwrite.c
// does for DirectWrite (P-006). Only the *order* of the slots is the ABI: the
// methods we never call stay opaque pointers.
#include <windows.h>

#include "../platform.h"

typedef struct ShUnknown ShUnknown;
typedef struct ShUnknownVtbl {
    HRESULT(WINAPI *QueryInterface)(ShUnknown *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(ShUnknown *self);
    ULONG(WINAPI *Release)(ShUnknown *self);
} ShUnknownVtbl;
struct ShUnknown { ShUnknownVtbl *lpVtbl; };

typedef struct IShellItem IShellItem;
typedef struct IShellItemVtbl {
    void *slots_unknown[3];
    void *slots_1[2];  // BindToHandler, GetParent
    HRESULT(WINAPI *GetDisplayName)(IShellItem *self, u32 form, PWSTR *out);
} IShellItemVtbl;
struct IShellItem { IShellItemVtbl *lpVtbl; };

typedef struct IFileDialog IFileDialog;
typedef struct IFileDialogVtbl {
    void *slots_unknown[3];
    HRESULT(WINAPI *Show)(IFileDialog *self, HWND owner);
    void *slots_1[5];  // SetFileTypes .. Unadvise
    HRESULT(WINAPI *SetOptions)(IFileDialog *self, u32 options);
    void *slots_2[1];  // GetOptions
    void *slots_3[6];  // SetDefaultFolder .. GetFileName
    HRESULT(WINAPI *SetTitle)(IFileDialog *self, const WCHAR *title);
    void *slots_4[2];  // SetOkButtonLabel, SetFileNameLabel
    HRESULT(WINAPI *GetResult)(IFileDialog *self, IShellItem **out);
} IFileDialogVtbl;
struct IFileDialog { IFileDialogVtbl *lpVtbl; };

#define FOS_PICKFOLDERS      0x00000020u
#define FOS_FORCEFILESYSTEM  0x00000040u
#define SIGDN_FILESYSPATH    0x80058000u
#define COINIT_APARTMENTTHREADED 0x2u
#define CLSCTX_INPROC     0x1u  // CLSCTX_INPROC_SERVER, absent under WIN32_LEAN_AND_MEAN

static const GUID win32_clsid_file_open_dialog = {
    0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const GUID win32_iid_file_dialog = {
    0x42F85136, 0xDB7E, 0x439C, {0x85, 0xF1, 0xE4, 0x07, 0x5D, 0x13, 0x5F, 0xC8}};

typedef HRESULT(WINAPI *Win32CoInitializeEx)(void *reserved, DWORD flags);
typedef HRESULT(WINAPI *Win32CoCreateInstance)(const GUID *clsid, void *outer, DWORD context,
                                               const GUID *iid, void **out);

typedef struct Win32DialogApi {
    b32 loaded;
    Win32CoInitializeEx co_initialize;
    Win32CoCreateInstance co_create;
} Win32DialogApi;

global Win32DialogApi win32_dialog;

static b32 win32_dialog_load(void) {
    if (win32_dialog.loaded) { return win32_dialog.co_create != 0; }
    win32_dialog.loaded = 1;
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    if (ole) {
        win32_dialog.co_initialize =
            (Win32CoInitializeEx)GetProcAddress(ole, "CoInitializeEx");
        win32_dialog.co_create =
            (Win32CoCreateInstance)GetProcAddress(ole, "CoCreateInstance");
    }
    // The picker lives in shell32; CoCreateInstance loads it through the
    // registry, but pulling it in now keeps the first click free of that hit.
    LoadLibraryW(L"shell32.dll");
    if (win32_dialog.co_initialize) {
        // S_FALSE means the thread was already an apartment: fine either way.
        win32_dialog.co_initialize(0, COINIT_APARTMENTTHREADED);
    }
    return win32_dialog.co_create != 0;
}

String8 os_dialog_pick_folder(Arena *arena, String8 title) {
    String8 result = str8(0, 0);
    if (!win32_dialog_load()) { return result; }

    IFileDialog *dialog = 0;
    if (win32_dialog.co_create(&win32_clsid_file_open_dialog, 0, CLSCTX_INPROC,
                               &win32_iid_file_dialog, (void **)&dialog) != S_OK) {
        return result;
    }

    ArenaTemp scratch = scratch_begin(&arena, 1);
    String16 title16 = str16_from_str8(scratch.arena, title);
    dialog->lpVtbl->SetOptions(dialog, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->lpVtbl->SetTitle(dialog, (const WCHAR *)title16.str);
    scratch_end(scratch);

    // The active window is ours: the picker is modal on it without the
    // platform layer having to hand its HWND around.
    if (dialog->lpVtbl->Show(dialog, GetActiveWindow()) == S_OK) {
        IShellItem *item = 0;
        if (dialog->lpVtbl->GetResult(dialog, &item) == S_OK) {
            PWSTR path = 0;
            if (item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path) == S_OK && path) {
                result = str8_from_cstr16(arena, (const u16 *)path);
                win32_shell_load();
                win32_shell.task_mem_release(path);
            }
            ((ShUnknown *)item)->lpVtbl->Release((ShUnknown *)item);
        }
    }
    ((ShUnknown *)dialog)->lpVtbl->Release((ShUnknown *)dialog);
    return result;
}
