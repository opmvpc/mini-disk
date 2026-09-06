#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma function(memset)
void *memset(void *d, int c, size_t n){ unsigned char *p=d; while(n--) *p++=(unsigned char)c; return d; }
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l){
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
void __stdcall entry_point(void){
    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = L"minidisk";
    wc.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"minidisk smoke", WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, 0, 0, inst, 0);
    (void)hwnd;
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    ExitProcess(0);
}
