// win32_thread.c - threads, semaphores, SRW mutexes and cpu count.
// CreateThread and not _beginthreadex: there is no CRT to initialize, and a
// thread that borrowed a scratch arena releases it before it returns.
#include <windows.h>

#include "../platform.h"

// The handover slots: CreateThread takes one pointer, we need two (proc, data).
// A slot is only live between CreateThread and the first line of the new
// thread, so a handful would do; 64 costs a kilobyte and never runs out.
#define WIN32_THREAD_SLOT_COUNT 64

typedef struct Win32ThreadSlot {
    volatile long in_use;
    OsThreadProc *proc;
    void *data;
} Win32ThreadSlot;

typedef HRESULT(WINAPI *Win32SetThreadDescription)(HANDLE, const WCHAR *);

global Win32ThreadSlot win32_thread_slots[WIN32_THREAD_SLOT_COUNT];
global Win32SetThreadDescription win32_set_thread_description;
global b32 win32_thread_description_resolved;

// SetThreadDescription only exists from Windows 10 1607 on: resolved by hand so
// an older machine loses the thread names instead of failing to start.
static Win32SetThreadDescription win32_thread_description_entry(void) {
    if (!win32_thread_description_resolved) {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        AssertAlways(kernel32 != 0);  // we are linked against it: it is loaded
        win32_set_thread_description =
                (Win32SetThreadDescription)GetProcAddress(kernel32, "SetThreadDescription");
        win32_thread_description_resolved = 1;
    }
    return win32_set_thread_description;
}

static void win32_thread_name(HANDLE thread, String8 name) {
    Win32SetThreadDescription set_name = win32_thread_description_entry();
    if (!set_name || name.size == 0) { return; }
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 wide = str16_from_str8(scratch.arena, name);
    set_name(thread, (const WCHAR *)wide.str);
    scratch_end(scratch);
}

static DWORD WINAPI win32_thread_entry(void *parameter) {
    Win32ThreadSlot *slot = (Win32ThreadSlot *)parameter;
    OsThreadProc *proc = slot->proc;
    void *data = slot->data;
    InterlockedExchange(&slot->in_use, 0);  // the slot carried the handover, nothing more
    proc(data);
    return 0;
}

u32 os_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (u32)info.dwNumberOfProcessors;
}

OsThread os_thread_create(OsThreadProc *proc, void *data, String8 name) {
    Win32ThreadSlot *slot = 0;
    for (u32 i = 0; i < WIN32_THREAD_SLOT_COUNT && slot == 0; i += 1) {
        if (InterlockedCompareExchange(&win32_thread_slots[i].in_use, 1, 0) == 0) {
            slot = &win32_thread_slots[i];
        }
    }
    AssertAlways(slot != 0);  // 64 threads being born at once is a bug, not a limit
    slot->proc = proc;
    slot->data = data;

    HANDLE handle = CreateThread(0, 0, win32_thread_entry, slot, 0, 0);
    AssertAlways(handle != 0);
    win32_thread_name(handle, name);

    OsThread result;
    result.v = (u64)handle;
    return result;
}

// NOLINTBEGIN(performance-no-int-to-ptr) OsThread and OsSemaphore carry a HANDLE
// through a u64, which is what keeps windows.h out of platform.h.
void os_thread_join(OsThread thread) {
    HANDLE handle = (HANDLE)thread.v;
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
}

void os_thread_set_name(String8 name) { win32_thread_name(GetCurrentThread(), name); }

void os_thread_yield(void) { SwitchToThread(); }

OsSemaphore os_semaphore_create(u32 initial_count, u32 max_count) {
    OsSemaphore result;
    result.v = (u64)CreateSemaphoreW(0, (LONG)initial_count, (LONG)max_count, 0);
    AssertAlways(result.v != 0);
    return result;
}

void os_semaphore_destroy(OsSemaphore semaphore) { CloseHandle((HANDLE)semaphore.v); }

void os_semaphore_wait(OsSemaphore semaphore) {
    WaitForSingleObject((HANDLE)semaphore.v, INFINITE);
}

void os_semaphore_signal(OsSemaphore semaphore, u32 count) {
    if (count == 0) { return; }
    ReleaseSemaphore((HANDLE)semaphore.v, (LONG)count, 0);
}
// NOLINTEND(performance-no-int-to-ptr)

// SRWLOCK is one pointer and needs no destruction, which is the whole reason we
// expose it rather than a CRITICAL_SECTION.
StaticAssert(sizeof(SRWLOCK) == sizeof(OsMutex), os_mutex_is_an_srwlock);

void os_mutex_init(OsMutex *mutex) { InitializeSRWLock((PSRWLOCK)mutex); }
void os_mutex_lock(OsMutex *mutex) { AcquireSRWLockExclusive((PSRWLOCK)mutex); }
void os_mutex_unlock(OsMutex *mutex) { ReleaseSRWLockExclusive((PSRWLOCK)mutex); }
