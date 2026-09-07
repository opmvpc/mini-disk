// win32_usb.c - WinUSB behind os_usb_* (ADR-008, research/01 s2).
//
// winusb.dll, setupapi.dll, cfgmgr32.dll and advapi32.dll are loaded by hand on
// the first call: the import table stays kernel32 + user32, and a machine
// missing one of them loses the device panel instead of failing to start.
//
// Enumeration walks the *device nodes* of the USB enumerator, not the
// GUID_DEVINTERFACE_USB_DEVICE interfaces: a device with no driver (P-001,
// CM_PROB_FAILED_INSTALL / code 28) exposes no interface at all, and reporting
// it is the whole point of the guided driver screen. The interface path of a
// device that *does* have WinUSB is read back from the DeviceInterfaceGUIDs
// value Zadig (or an INF) wrote under its Device Parameters key - the GUID is
// whatever the installer chose, never a constant of ours (research/01 s2.2).
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>

#include "../platform.h"
#include "../../core/netmd/netmd_transport.h"

// Four devices open at once is three more than the app will ever want; the
// slots keep the handles out of any allocator (no malloc below platform/).
#define WIN32_USB_SLOT_COUNT 4
// One entry per pipe address we may cache a timeout for: IN pipes at 16 + n,
// OUT pipes and the control pipe at n.
#define WIN32_USB_PIPE_SLOTS 32
#define WIN32_USB_DEFAULT_TIMEOUT_MS 10000

typedef HDEVINFO(WINAPI *Win32SetupDiGetClassDevsW)(const GUID *, PCWSTR, HWND, DWORD);
typedef BOOL(WINAPI *Win32SetupDiEnumDeviceInfo)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
typedef BOOL(WINAPI *Win32SetupDiGetDeviceInstanceIdW)(HDEVINFO, PSP_DEVINFO_DATA, PWSTR, DWORD,
                                                       PDWORD);
typedef BOOL(WINAPI *Win32SetupDiGetDeviceRegistryPropertyW)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                                             PDWORD, PBYTE, DWORD, PDWORD);
typedef HKEY(WINAPI *Win32SetupDiOpenDevRegKey)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, DWORD, DWORD,
                                                REGSAM);
typedef BOOL(WINAPI *Win32SetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *,
                                                       DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL(WINAPI *Win32SetupDiGetDeviceInterfaceDetailW)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA,
                                                            PSP_DEVICE_INTERFACE_DETAIL_DATA_W,
                                                            DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL(WINAPI *Win32SetupDiDestroyDeviceInfoList)(HDEVINFO);
typedef CONFIGRET(WINAPI *Win32CM_Get_DevNode_Status)(PULONG, PULONG, DEVINST, ULONG);
typedef LONG(WINAPI *Win32RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG(WINAPI *Win32RegCloseKey)(HKEY);

typedef BOOL(WINAPI *Win32WinUsbInitialize)(HANDLE, WINUSB_INTERFACE_HANDLE *);
typedef BOOL(WINAPI *Win32WinUsbFree)(WINUSB_INTERFACE_HANDLE);
typedef BOOL(WINAPI *Win32WinUsbControlTransfer)(WINUSB_INTERFACE_HANDLE, WINUSB_SETUP_PACKET,
                                                 PUCHAR, ULONG, PULONG, LPOVERLAPPED);
typedef BOOL(WINAPI *Win32WinUsbReadPipe)(WINUSB_INTERFACE_HANDLE, UCHAR, PUCHAR, ULONG, PULONG,
                                          LPOVERLAPPED);
typedef BOOL(WINAPI *Win32WinUsbWritePipe)(WINUSB_INTERFACE_HANDLE, UCHAR, PUCHAR, ULONG, PULONG,
                                           LPOVERLAPPED);
typedef BOOL(WINAPI *Win32WinUsbSetPipePolicy)(WINUSB_INTERFACE_HANDLE, UCHAR, ULONG, ULONG,
                                               PVOID);
typedef BOOL(WINAPI *Win32WinUsbSetPowerPolicy)(WINUSB_INTERFACE_HANDLE, ULONG, ULONG, PVOID);
typedef BOOL(WINAPI *Win32WinUsbResetPipe)(WINUSB_INTERFACE_HANDLE, UCHAR);

typedef struct Win32UsbSlot {
    volatile long in_use;
    HANDLE file;
    WINUSB_INTERFACE_HANDLE winusb;
    u32 timeout_ms[WIN32_USB_PIPE_SLOTS];
} Win32UsbSlot;

typedef struct Win32UsbApi {
    b32 loaded;
    b32 ok;
    Win32SetupDiGetClassDevsW GetClassDevs;
    Win32SetupDiEnumDeviceInfo EnumDeviceInfo;
    Win32SetupDiGetDeviceInstanceIdW GetInstanceId;
    Win32SetupDiGetDeviceRegistryPropertyW GetRegistryProperty;
    Win32SetupDiOpenDevRegKey OpenDevRegKey;
    Win32SetupDiEnumDeviceInterfaces EnumInterfaces;
    Win32SetupDiGetDeviceInterfaceDetailW GetInterfaceDetail;
    Win32SetupDiDestroyDeviceInfoList DestroyInfoList;
    Win32CM_Get_DevNode_Status DevNodeStatus;
    Win32RegQueryValueExW RegQuery;
    Win32RegCloseKey RegClose;
    Win32WinUsbInitialize Initialize;
    Win32WinUsbFree Free;
    Win32WinUsbControlTransfer ControlTransfer;
    Win32WinUsbReadPipe ReadPipe;
    Win32WinUsbWritePipe WritePipe;
    Win32WinUsbSetPipePolicy SetPipePolicy;
    Win32WinUsbSetPowerPolicy SetPowerPolicy;
    Win32WinUsbResetPipe ResetPipe;
} Win32UsbApi;

global Win32UsbApi win32_usb;
global Win32UsbSlot win32_usb_slots[WIN32_USB_SLOT_COUNT];

static b32 win32_usb_load(void) {
    if (win32_usb.loaded) { return win32_usb.ok; }
    win32_usb.loaded = 1;
    HMODULE setup = LoadLibraryW(L"setupapi.dll");
    HMODULE cfgmgr = LoadLibraryW(L"cfgmgr32.dll");
    HMODULE winusb = LoadLibraryW(L"winusb.dll");
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    if (!setup || !cfgmgr || !winusb || !advapi) { return 0; }

    win32_usb.GetClassDevs =
            (Win32SetupDiGetClassDevsW)GetProcAddress(setup, "SetupDiGetClassDevsW");
    win32_usb.EnumDeviceInfo =
            (Win32SetupDiEnumDeviceInfo)GetProcAddress(setup, "SetupDiEnumDeviceInfo");
    win32_usb.GetInstanceId =
            (Win32SetupDiGetDeviceInstanceIdW)GetProcAddress(setup, "SetupDiGetDeviceInstanceIdW");
    win32_usb.GetRegistryProperty = (Win32SetupDiGetDeviceRegistryPropertyW)GetProcAddress(
            setup, "SetupDiGetDeviceRegistryPropertyW");
    win32_usb.OpenDevRegKey =
            (Win32SetupDiOpenDevRegKey)GetProcAddress(setup, "SetupDiOpenDevRegKey");
    win32_usb.EnumInterfaces = (Win32SetupDiEnumDeviceInterfaces)GetProcAddress(
            setup, "SetupDiEnumDeviceInterfaces");
    win32_usb.GetInterfaceDetail = (Win32SetupDiGetDeviceInterfaceDetailW)GetProcAddress(
            setup, "SetupDiGetDeviceInterfaceDetailW");
    win32_usb.DestroyInfoList = (Win32SetupDiDestroyDeviceInfoList)GetProcAddress(
            setup, "SetupDiDestroyDeviceInfoList");
    win32_usb.DevNodeStatus =
            (Win32CM_Get_DevNode_Status)GetProcAddress(cfgmgr, "CM_Get_DevNode_Status");
    win32_usb.RegQuery = (Win32RegQueryValueExW)GetProcAddress(advapi, "RegQueryValueExW");
    win32_usb.RegClose = (Win32RegCloseKey)GetProcAddress(advapi, "RegCloseKey");
    win32_usb.Initialize = (Win32WinUsbInitialize)GetProcAddress(winusb, "WinUsb_Initialize");
    win32_usb.Free = (Win32WinUsbFree)GetProcAddress(winusb, "WinUsb_Free");
    win32_usb.ControlTransfer =
            (Win32WinUsbControlTransfer)GetProcAddress(winusb, "WinUsb_ControlTransfer");
    win32_usb.ReadPipe = (Win32WinUsbReadPipe)GetProcAddress(winusb, "WinUsb_ReadPipe");
    win32_usb.WritePipe = (Win32WinUsbWritePipe)GetProcAddress(winusb, "WinUsb_WritePipe");
    win32_usb.SetPipePolicy =
            (Win32WinUsbSetPipePolicy)GetProcAddress(winusb, "WinUsb_SetPipePolicy");
    win32_usb.SetPowerPolicy =
            (Win32WinUsbSetPowerPolicy)GetProcAddress(winusb, "WinUsb_SetPowerPolicy");
    win32_usb.ResetPipe = (Win32WinUsbResetPipe)GetProcAddress(winusb, "WinUsb_ResetPipe");

    win32_usb.ok = win32_usb.GetClassDevs && win32_usb.EnumDeviceInfo &&
                   win32_usb.GetInstanceId && win32_usb.GetRegistryProperty &&
                   win32_usb.OpenDevRegKey && win32_usb.EnumInterfaces &&
                   win32_usb.GetInterfaceDetail && win32_usb.DestroyInfoList &&
                   win32_usb.DevNodeStatus && win32_usb.RegQuery && win32_usb.RegClose &&
                   win32_usb.Initialize && win32_usb.Free && win32_usb.ControlTransfer &&
                   win32_usb.ReadPipe && win32_usb.WritePipe && win32_usb.SetPipePolicy &&
                   win32_usb.ResetPipe;
    return win32_usb.ok;
}

// --- parsing helpers -------------------------------------------------------

static u32 win32_hex_value(u16 c) {
    if (c >= '0' && c <= '9') { return (u32)(c - '0'); }
    if (c >= 'a' && c <= 'f') { return (u32)(c - 'a') + 10; }
    if (c >= 'A' && c <= 'F') { return (u32)(c - 'A') + 10; }
    return U32_MAX;
}

// "USB\VID_054C&PID_0084\5&A8846EA&0&1" -> 0x054C, 0x0084. Both stay 0 when the
// instance id is not a VID/PID one (a hub, a composite parent).
static void win32_usb_ids_from_instance(const WCHAR *instance_id, u16 *out_vid, u16 *out_pid) {
    *out_vid = 0;
    *out_pid = 0;
    for (u64 i = 0; instance_id[i]; i += 1) {
        u16 *out = 0;
        if ((instance_id[i] == 'V' || instance_id[i] == 'v') && instance_id[i + 1] == 'I' &&
            instance_id[i + 2] == 'D' && instance_id[i + 3] == '_') {
            out = out_vid;
        } else if ((instance_id[i] == 'P' || instance_id[i] == 'p') && instance_id[i + 1] == 'I' &&
                   instance_id[i + 2] == 'D' && instance_id[i + 3] == '_') {
            out = out_pid;
        }
        if (!out) { continue; }
        u32 value = 0;
        u32 digits = 0;
        for (; digits < 4; digits += 1) {
            u32 nibble = win32_hex_value(instance_id[i + 4 + digits]);
            if (nibble == U32_MAX) { break; }
            value = (value << 4) | nibble;
        }
        if (digits == 4) { *out = (u16)value; }
    }
}

// "{A5DCBF10-6530-11D2-901F-00C04FB951ED}" -> GUID. 0 when the text is not one:
// the value under Device Parameters was written by an installer, so it is a
// boundary and the only place where this is checked.
static b32 win32_guid_from_text(const WCHAR *text, GUID *out) {
    u64 at = (text[0] == '{') ? 1 : 0;
    u8 bytes[16];
    global const u8 dash_after[4] = {4, 6, 8, 10};  // byte index each dash follows
    u32 dash = 0;
    for (u32 i = 0; i < 16; i += 1) {
        if (dash < 4 && i == dash_after[dash]) {
            if (text[at] != '-') { return 0; }
            at += 1;
            dash += 1;
        }
        u32 high = win32_hex_value(text[at]);
        u32 low = win32_hex_value(text[at + 1]);
        if (high == U32_MAX || low == U32_MAX) { return 0; }
        bytes[i] = (u8)((high << 4) | low);
        at += 2;
    }
    // The first three fields are little endian in memory, the last two are not.
    out->Data1 = ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
    out->Data2 = (u16)(((u32)bytes[4] << 8) | bytes[5]);
    out->Data3 = (u16)(((u32)bytes[6] << 8) | bytes[7]);
    mem_copy(out->Data4, bytes + 8, 8);
    return 1;
}

// The interface path of `instance_id` for `guid`, empty when the device does
// not expose one. Passing the instance id as the enumerator narrows the set to
// that one device, so there is no path to match by hand.
static String8 win32_usb_interface_path(Arena *arena, const GUID *guid, const WCHAR *instance_id) {
    String8 result = str8(0, 0);
    HDEVINFO set = win32_usb.GetClassDevs(guid, instance_id, 0,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) { return result; }
    SP_DEVICE_INTERFACE_DATA interface_data;
    StructZero(&interface_data);
    interface_data.cbSize = sizeof(interface_data);
    if (win32_usb.EnumInterfaces(set, 0, guid, 0, &interface_data)) {
        // 1 KB covers any device path Windows produces; the two call dance of
        // the documentation would only add a failure mode.
        u8 storage[1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)storage;
        StructZero(detail);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        DWORD needed = 0;
        if (win32_usb.GetInterfaceDetail(set, &interface_data, detail, sizeof(storage), &needed,
                                         0)) {
            result = str8_from_cstr16(arena, (const u16 *)detail->DevicePath);
        }
    }
    win32_usb.DestroyInfoList(set);
    return result;
}

// The GUIDs an installer registered for this device. REG_MULTI_SZ in the WinUSB
// INF of the documentation, REG_SZ in what some tools write: both are read.
static String8 win32_usb_path_from_registry(Arena *arena, HDEVINFO set, SP_DEVINFO_DATA *device,
                                            const WCHAR *instance_id) {
    String8 result = str8(0, 0);
    HKEY key = win32_usb.OpenDevRegKey(set, device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) { return result; }
    WCHAR value[512];
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (win32_usb.RegQuery(key, L"DeviceInterfaceGUIDs", 0, &type, (LPBYTE)value, &size) ==
                ERROR_SUCCESS ||
        win32_usb.RegQuery(key, L"DeviceInterfaceGUID", 0, &type, (LPBYTE)value, &size) ==
                ERROR_SUCCESS) {
        u64 units = size / sizeof(WCHAR);
        if (units >= ArrayCount(value)) { units = ArrayCount(value) - 1; }
        value[units] = 0;
        for (u64 at = 0; at < units && value[at] && result.size == 0;) {
            GUID guid;
            if (win32_guid_from_text(&value[at], &guid)) {
                result = win32_usb_interface_path(arena, &guid, instance_id);
            }
            while (at < units && value[at]) { at += 1; }  // past this string
            at += 1;                                      // and its terminator
        }
    }
    win32_usb.RegClose(key);
    return result;
}

static String8 win32_usb_bus_name(Arena *arena, HDEVINFO set, SP_DEVINFO_DATA *device) {
    WCHAR name[256];
    DWORD size = sizeof(name);
    DWORD type = 0;
    if (!win32_usb.GetRegistryProperty(set, device, SPDRP_FRIENDLYNAME, &type, (PBYTE)name, size,
                                       0)) {
        size = sizeof(name);
        if (!win32_usb.GetRegistryProperty(set, device, SPDRP_DEVICEDESC, &type, (PBYTE)name, size,
                                           0)) {
            return str8(0, 0);
        }
    }
    name[ArrayCount(name) - 1] = 0;
    return str8_from_cstr16(arena, (const u16 *)name);
}

// Ready, or taken by somebody else. Opening a device interface is what any
// WinUSB application does, and only devices that registered an interface GUID
// (that is: WinUSB ones) ever get here.
static u32 win32_usb_probe_state(String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
    scratch_end(scratch);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        return OsUsbState_Ready;
    }
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
        return OsUsbState_InUse;
    }
    return OsUsbState_NoDriver;
}

OsUsbDeviceList os_usb_enumerate(Arena *arena) {
    OsUsbDeviceList list;
    StructZero(&list);
    if (!win32_usb_load()) { return list; }

    // The USB enumerator, every class: a driverless node has no class at all,
    // and it is exactly the one the user needs to be told about.
    HDEVINFO set = win32_usb.GetClassDevs(0, L"USB", 0, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) { return list; }

    // 96 entries is 5 KB of stack: the array is built here and copied into the
    // arena at its final size, so the caller's arena holds no slack.
    OsUsbDeviceInfo found[OS_USB_DEVICE_MAX];
    u64 count = 0;
    SP_DEVINFO_DATA device;
    StructZero(&device);
    device.cbSize = sizeof(device);
    for (DWORD i = 0; count < OS_USB_DEVICE_MAX && win32_usb.EnumDeviceInfo(set, i, &device);
         i += 1) {
        WCHAR instance_id[MAX_DEVICE_ID_LEN];
        instance_id[0] = 0;
        if (!win32_usb.GetInstanceId(set, &device, instance_id, ArrayCount(instance_id), 0)) {
            continue;
        }
        OsUsbDeviceInfo *info = &found[count];
        StructZero(info);
        win32_usb_ids_from_instance(instance_id, &info->vid, &info->pid);
        if (info->vid == 0) { continue; }  // a hub or a root port, not a device

        ULONG status = 0;
        ULONG problem = 0;
        if (win32_usb.DevNodeStatus(&status, &problem, device.DevInst, 0) == CR_SUCCESS) {
            info->problem_code = (u32)problem;
        }
        info->bus_name = win32_usb_bus_name(arena, set, &device);
        if (info->problem_code != 0) {
            // Nothing is bound: no interface exists, so there is no path to
            // give and the UI has the code 28 story to tell (P-001).
            info->state = OsUsbState_NoDriver;
        } else {
            info->path = win32_usb_path_from_registry(arena, set, &device, instance_id);
            info->state = (info->path.size != 0) ? win32_usb_probe_state(info->path)
                                                 : (u32)OsUsbState_NoDriver;
        }
        count += 1;
    }
    win32_usb.DestroyInfoList(set);

    if (count != 0) {
        list.items = push_array(arena, OsUsbDeviceInfo, count);
        mem_copy(list.items, found, count * sizeof(OsUsbDeviceInfo));
        list.count = count;
    }
    return list;
}

// --- open, close -----------------------------------------------------------

static u32 win32_usb_pipe_slot(u8 endpoint) {
    return (u32)((endpoint & 0x80u) ? 16 + (endpoint & 0x0Fu) : (endpoint & 0x0Fu));
}

static void win32_usb_set_timeout(Win32UsbSlot *slot, u8 endpoint, u32 timeout_ms) {
    u32 index = win32_usb_pipe_slot(endpoint);
    if (slot->timeout_ms[index] == timeout_ms) { return; }
    ULONG value = timeout_ms;
    if (win32_usb.SetPipePolicy(slot->winusb, endpoint, PIPE_TRANSFER_TIMEOUT, sizeof(value),
                                &value)) {
        slot->timeout_ms[index] = timeout_ms;
    }
}

OsUsb os_usb_open(String8 path) {
    OsUsb result;
    result.v = 0;
    if (!win32_usb_load() || path.size == 0) { return result; }

    Win32UsbSlot *slot = 0;
    for (u32 i = 0; i < WIN32_USB_SLOT_COUNT && slot == 0; i += 1) {
        if (InterlockedCompareExchange(&win32_usb_slots[i].in_use, 1, 0) == 0) {
            slot = &win32_usb_slots[i];
        }
    }
    if (!slot) { return result; }  // four devices at once: a caller leak, not a limit

    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
    scratch_end(scratch);
    if (file == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&slot->in_use, 0);
        return result;
    }
    WINUSB_INTERFACE_HANDLE winusb = 0;
    if (!win32_usb.Initialize(file, &winusb)) {
        CloseHandle(file);
        InterlockedExchange(&slot->in_use, 0);
        return result;
    }
    slot->file = file;
    slot->winusb = winusb;
    mem_zero(slot->timeout_ms, sizeof(slot->timeout_ms));

    // The policies of research/01 s2.3, set before any transfer:
    //  - a transfer timeout on the control pipe and on both bulk pipes,
    //  - AUTO_CLEAR_STALL so a stalled pipe recovers on its own,
    //  - SHORT_PACKET_TERMINATE off: no zero length packet after a write whose
    //    size is a multiple of wMaxPacketSize, which the device would read as
    //    the end of a frame,
    //  - IGNORE_SHORT_PACKETS off: a short packet ends the read, which is what
    //    makes a reply of unknown length come back at once.
    win32_usb_set_timeout(slot, 0, WIN32_USB_DEFAULT_TIMEOUT_MS);
    win32_usb_set_timeout(slot, NETMD_EP_BULK_IN, WIN32_USB_DEFAULT_TIMEOUT_MS);
    win32_usb_set_timeout(slot, NETMD_EP_BULK_OUT, WIN32_USB_DEFAULT_TIMEOUT_MS);
    UCHAR on = TRUE;
    UCHAR off = FALSE;
    win32_usb.SetPipePolicy(winusb, NETMD_EP_BULK_IN, AUTO_CLEAR_STALL, sizeof(on), &on);
    win32_usb.SetPipePolicy(winusb, NETMD_EP_BULK_OUT, AUTO_CLEAR_STALL, sizeof(on), &on);
    win32_usb.SetPipePolicy(winusb, NETMD_EP_BULK_OUT, SHORT_PACKET_TERMINATE, sizeof(off), &off);
    win32_usb.SetPipePolicy(winusb, NETMD_EP_BULK_IN, IGNORE_SHORT_PACKETS, sizeof(off), &off);
    // A 74 minute download must not be cut in half by USB selective suspend.
    if (win32_usb.SetPowerPolicy) {
        win32_usb.SetPowerPolicy(winusb, AUTO_SUSPEND, sizeof(off), &off);
    }

    result.v = slot;
    return result;
}

void os_usb_close(OsUsb usb) {
    Win32UsbSlot *slot = (Win32UsbSlot *)usb.v;
    if (!slot) { return; }
    if (slot->winusb) { win32_usb.Free(slot->winusb); }
    if (slot->file) { CloseHandle(slot->file); }
    slot->winusb = 0;
    slot->file = 0;
    InterlockedExchange(&slot->in_use, 0);
}

// --- transfers -------------------------------------------------------------

static i32 win32_usb_error(void) {
    DWORD error = GetLastError();
    if (error == ERROR_SEM_TIMEOUT) { return OsUsbError_Timeout; }
    if (error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_NO_SUCH_DEVICE ||
        error == ERROR_GEN_FAILURE || error == ERROR_FILE_NOT_FOUND ||
        error == ERROR_DEVICE_REMOVED) {
        return OsUsbError_Disconnected;
    }
    return OsUsbError_Failed;
}

i32 os_usb_control(OsUsb usb, u8 request_type, u8 request, u16 value, u16 index, void *buffer,
                   u32 length, u32 timeout_ms) {
    Win32UsbSlot *slot = (Win32UsbSlot *)usb.v;
    if (!slot) { return OsUsbError_NotOpen; }
    win32_usb_set_timeout(slot, 0, timeout_ms);
    WINUSB_SETUP_PACKET setup;
    setup.RequestType = request_type;
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = (USHORT)length;
    ULONG transferred = 0;
    if (!win32_usb.ControlTransfer(slot->winusb, setup, (PUCHAR)buffer, length, &transferred, 0)) {
        return win32_usb_error();
    }
    return (i32)transferred;
}

i32 os_usb_bulk_write(OsUsb usb, u8 endpoint, const void *data, u32 length, u32 timeout_ms) {
    Win32UsbSlot *slot = (Win32UsbSlot *)usb.v;
    if (!slot) { return OsUsbError_NotOpen; }
    win32_usb_set_timeout(slot, endpoint, timeout_ms);
    ULONG transferred = 0;
    if (!win32_usb.WritePipe(slot->winusb, endpoint, (PUCHAR)data, length, &transferred, 0)) {
        return win32_usb_error();
    }
    return (i32)transferred;
}

i32 os_usb_bulk_read(OsUsb usb, u8 endpoint, void *data, u32 length, u32 timeout_ms) {
    Win32UsbSlot *slot = (Win32UsbSlot *)usb.v;
    if (!slot) { return OsUsbError_NotOpen; }
    win32_usb_set_timeout(slot, endpoint, timeout_ms);
    ULONG transferred = 0;
    if (!win32_usb.ReadPipe(slot->winusb, endpoint, (PUCHAR)data, length, &transferred, 0)) {
        return win32_usb_error();
    }
    return (i32)transferred;
}

b32 os_usb_reset(OsUsb usb) {
    Win32UsbSlot *slot = (Win32UsbSlot *)usb.v;
    if (!slot) { return 0; }
    b32 ok = win32_usb.ResetPipe(slot->winusb, NETMD_EP_BULK_IN) != 0;
    ok = (win32_usb.ResetPipe(slot->winusb, NETMD_EP_BULK_OUT) != 0) && ok;
    return ok;
}

// --- the UsbTransport binding (ADR-008) ------------------------------------
// The protocol layer of core/netmd only ever sees these three function
// pointers, which is what lets the tests replay a transcript instead.

static i32 win32_usb_transport_control(void *user, u8 request_type, u8 request, u16 value,
                                       u16 index, void *buffer, u32 length, u32 timeout_ms) {
    OsUsb usb;
    usb.v = user;
    return os_usb_control(usb, request_type, request, value, index, buffer, length, timeout_ms);
}

static i32 win32_usb_transport_bulk_write(void *user, u8 endpoint, const void *data, u32 length,
                                          u32 timeout_ms) {
    OsUsb usb;
    usb.v = user;
    return os_usb_bulk_write(usb, endpoint, data, length, timeout_ms);
}

static i32 win32_usb_transport_bulk_read(void *user, u8 endpoint, void *data, u32 length,
                                         u32 timeout_ms) {
    OsUsb usb;
    usb.v = user;
    return os_usb_bulk_read(usb, endpoint, data, length, timeout_ms);
}

void os_usb_transport(OsUsb usb, UsbTransport *out) {
    out->control = win32_usb_transport_control;
    out->bulk_write = win32_usb_transport_bulk_write;
    out->bulk_read = win32_usb_transport_bulk_read;
    out->user = usb.v;
}
