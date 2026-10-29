// Diagnostic control: a hidden Win32 window without CRT, editor, or application UI.
// This is not the simple-md application and is never used to alter its memory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
LRESULT CALLBACK FloorProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
extern "C" void WINAPI Entry(){
    HINSTANCE instance=GetModuleHandleW(nullptr);
    static WNDCLASSW wc;wc.lpfnWndProc=FloorProc;wc.hInstance=instance;wc.lpszClassName=L"simple-md.MemoryFloor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"Win32 memory floor",WS_OVERLAPPEDWINDOW,0,0,1180,790,nullptr,nullptr,instance,nullptr);
    ShowWindow(window,SW_HIDE);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    ExitProcess(0);
}
