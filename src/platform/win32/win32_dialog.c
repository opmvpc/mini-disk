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
// One filter row of SetFileTypes; COMDLG_FILTERSPEC, declared here for the
// same reason as the vtables (shobjidl.h is C++ only).
typedef struct Win32FilterSpec {
    const WCHAR *name;
    const WCHAR *spec;
} Win32FilterSpec;

typedef struct IFileDialogVtbl {
    void *slots_unknown[3];
    HRESULT(WINAPI *Show)(IFileDialog *self, HWND owner);
    HRESULT(WINAPI *SetFileTypes)(IFileDialog *self, u32 count, const Win32FilterSpec *types);
    void *slots_1[4];  // SetFileTypeIndex .. Unadvise
    HRESULT(WINAPI *SetOptions)(IFileDialog *self, u32 options);
    void *slots_2[1];  // GetOptions
    void *slots_3[4];  // SetDefaultFolder .. GetCurrentSelection
    HRESULT(WINAPI *SetFileName)(IFileDialog *self, const WCHAR *name);
    void *slots_3b[1];  // GetFileName
    HRESULT(WINAPI *SetTitle)(IFileDialog *self, const WCHAR *title);
    void *slots_4[2];  // SetOkButtonLabel, SetFileNameLabel
    HRESULT(WINAPI *GetResult)(IFileDialog *self, IShellItem **out);
    void *slots_5[1];  // AddPlace
    HRESULT(WINAPI *SetDefaultExtension)(IFileDialog *self, const WCHAR *extension);
} IFileDialogVtbl;
struct IFileDialog { IFileDialogVtbl *lpVtbl; };

#define FOS_OVERWRITEPROMPT  0x00000002u
#define FOS_PICKFOLDERS      0x00000020u
#define FOS_FORCEFILESYSTEM  0x00000040u
#define FOS_FILEMUSTEXIST    0x00001000u
#define SIGDN_FILESYSPATH    0x80058000u
#define COINIT_APARTMENTTHREADED 0x2u
#define CLSCTX_INPROC     0x1u  // CLSCTX_INPROC_SERVER, absent under WIN32_LEAN_AND_MEAN

static const GUID win32_clsid_file_open_dialog = {
    0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const GUID win32_clsid_file_save_dialog = {
    0xC0B4E2F3, 0xBA21, 0x4773, {0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B}};
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

// The file half of the picker, open and save through the same code: they differ
// by a class id, one option bit and whether a name is proposed. `spec` is the
// pattern ("*.mdplan"); its extension is also what a save appends when the user
// types a bare name.
static String8 win32_dialog_file(Arena *arena, const GUID *clsid, u32 options, String8 title,
                                 String8 filter_name, String8 spec, String8 suggested) {
    String8 result = str8(0, 0);
    if (!win32_dialog_load()) { return result; }

    IFileDialog *dialog = 0;
    if (win32_dialog.co_create(clsid, 0, CLSCTX_INPROC, &win32_iid_file_dialog,
                               (void **)&dialog) != S_OK) {
        return result;
    }

    ArenaTemp scratch = scratch_begin(&arena, 1);
    String16 title16 = str16_from_str8(scratch.arena, title);
    String16 name16 = str16_from_str8(scratch.arena, filter_name);
    String16 spec16 = str16_from_str8(scratch.arena, spec);
    // "*.mdplan" -> "mdplan": the default extension carries no dot and no star.
    String8 extension = str8_skip(spec, (spec.size >= 2 && spec.str[0] == '*') ? 2 : 0);
    String16 extension16 = str16_from_str8(scratch.arena, extension);
    Win32FilterSpec filter;
    filter.name = (const WCHAR *)name16.str;
    filter.spec = (const WCHAR *)spec16.str;
    dialog->lpVtbl->SetOptions(dialog, options | FOS_FORCEFILESYSTEM);
    dialog->lpVtbl->SetFileTypes(dialog, 1, &filter);
    dialog->lpVtbl->SetDefaultExtension(dialog, (const WCHAR *)extension16.str);
    dialog->lpVtbl->SetTitle(dialog, (const WCHAR *)title16.str);
    if (suggested.size != 0) {
        String16 suggested16 = str16_from_str8(scratch.arena, suggested);
        dialog->lpVtbl->SetFileName(dialog, (const WCHAR *)suggested16.str);
    }
    scratch_end(scratch);

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

String8 os_dialog_open_file(Arena *arena, String8 title, String8 filter_name,
                            String8 filter_spec) {
    return win32_dialog_file(arena, &win32_clsid_file_open_dialog, FOS_FILEMUSTEXIST, title,
                             filter_name, filter_spec, str8(0, 0));
}

String8 os_dialog_save_file(Arena *arena, String8 title, String8 filter_name,
                            String8 filter_spec, String8 suggested) {
    return win32_dialog_file(arena, &win32_clsid_file_save_dialog, FOS_OVERWRITEPROMPT, title,
                             filter_name, filter_spec, suggested);
}
