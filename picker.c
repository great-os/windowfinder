#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include "picker.h"

#define INPUT_CLASS L"WindowFinder.Input"
#define FRAME_CLASS L"WindowFinder.Highlight"

static HWND owner_window, input_window, frame_window;
static HHOOK mouse_hook, keyboard_hook;
static BOOL active, finish_pending, saw_left_down, watch_button;
static POINT release_point;
static RECT highlighted_rect;

BOOL picker_active(void)
{
    return active;
}

static void queue_cancel(WPARAM reason)
{
    if (active && !finish_pending)
        finish_pending = PostMessageW(owner_window, WM_PICK_CANCEL, reason, 0);
}

static HWND target_at(POINT point)
{
    HWND target;
    DWORD pid = 0;
    LONG_PTR style = GetWindowLongPtrW(input_window, GWL_EXSTYLE);
    /* Only hit testing changes; the input layer stays visually unchanged. */
    SetWindowLongPtrW(input_window, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
    target = WindowFromPoint(point);
    SetWindowLongPtrW(input_window, GWL_EXSTYLE, style);
    if (target) GetWindowThreadProcessId(target, &pid);
    return pid && pid != GetCurrentProcessId() ? target : NULL;
}

static void update_highlight(void)
{
    POINT point;
    RECT rect;
    HWND target;
    int width, height, thickness;
    HRGN outer, inner;
    if (!GetCursorPos(&point)) return;
    target = target_at(point);
    if (!target || !GetWindowRect(target, &rect) || IsRectEmpty(&rect)) {
        ShowWindow(frame_window, SW_HIDE);
        SetRectEmpty(&highlighted_rect);
        return;
    }
    if (EqualRect(&rect, &highlighted_rect) && IsWindowVisible(frame_window)) return;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    thickness = max(2, MulDiv(3, (int)GetDpiForWindow(target), 96));
    outer = CreateRectRgn(0, 0, width, height);
    inner = CreateRectRgn(thickness, thickness, max(thickness, width - thickness),
        max(thickness, height - thickness));
    if (!outer || !inner) {
        if (outer) DeleteObject(outer);
        if (inner) DeleteObject(inner);
        return;
    }
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    if (!SetWindowRgn(frame_window, outer, TRUE)) DeleteObject(outer);
    SetWindowPos(frame_window, HWND_TOPMOST, rect.left, rect.top, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    highlighted_rect = rect;
}

static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && active && !finish_pending) {
        if (wparam == WM_LBUTTONDOWN) {
            saw_left_down = TRUE;
            return 1;
        }
        if (wparam == WM_LBUTTONUP && saw_left_down) {
            release_point = ((const MSLLHOOKSTRUCT *)lparam)->pt;
            finish_pending = PostMessageW(owner_window, WM_PICK_FINISH, 0, 0);
            return 1;
        }
        if (wparam == WM_RBUTTONDOWN) {
            queue_cancel(PICK_CANCEL_USER);
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && active && !finish_pending &&
        (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) &&
        ((const KBDLLHOOKSTRUCT *)lparam)->vkCode == VK_ESCAPE) {
        queue_cancel(PICK_CANCEL_USER);
        return 1;
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK input_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATEANDEAT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_CROSS));
        return TRUE;
    case WM_DISPLAYCHANGE:
    case WM_CANCELMODE:
        queue_cancel(PICK_CANCEL_INTERRUPTED);
        return 0;
    case WM_TIMER:
        if (active && !finish_pending) {
            SHORT left = GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON);
            SHORT escape = GetAsyncKeyState(VK_ESCAPE);
            HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            if (!desktop || (escape & 0x8000))
                queue_cancel(PICK_CANCEL_USER);
            /* Suppressed keyboard-mode clicks do not update the async button state. */
            else if (watch_button && !(left & 0x8000) && !finish_pending)
                queue_cancel(PICK_CANCEL_INTERRUPTED);
            if (desktop) CloseDesktop(desktop);
            if (!finish_pending) {
                update_highlight();
                SetCursor(LoadCursorW(NULL, IDC_CROSS));
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void picker_stop(void)
{
    active = FALSE;
    finish_pending = FALSE;
    if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
    if (keyboard_hook) UnhookWindowsHookEx(keyboard_hook);
    mouse_hook = keyboard_hook = NULL;
    if (input_window) DestroyWindow(input_window);
    if (frame_window) DestroyWindow(frame_window);
    input_window = frame_window = NULL;
    SetRectEmpty(&highlighted_rect);
}

DWORD picker_start(HWND owner, HINSTANCE instance, BOOL from_mouse)
{
    WNDCLASSW window_class = {0};
    DWORD error;
    if (active) return ERROR_BUSY;
    owner_window = owner;
    saw_left_down = watch_button = from_mouse;
    finish_pending = FALSE;
    window_class.hInstance = instance;
    window_class.lpfnWndProc = input_proc;
    window_class.hCursor = LoadCursorW(NULL, IDC_CROSS);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = INPUT_CLASS;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return GetLastError();
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
    window_class.lpszClassName = FRAME_CLASS;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return GetLastError();

    input_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        INPUT_CLASS, NULL, WS_POPUP, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), NULL, NULL, instance, NULL);
    frame_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
        WS_EX_LAYERED | WS_EX_TRANSPARENT, FRAME_CLASS, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!input_window || !frame_window) goto failed;
    /* Nonzero alpha receives input and owns the cursor without changing system cursors. */
    if (!SetLayeredWindowAttributes(input_window, 0, 1, LWA_ALPHA) ||
        !SetLayeredWindowAttributes(frame_window, 0, 255, LWA_ALPHA)) goto failed;
    mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, instance, 0);
    if (!mouse_hook) goto failed;
    keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, instance, 0);
    if (!keyboard_hook || !SetTimer(input_window, 1, 30, NULL)) goto failed;
    active = TRUE;
    ShowWindow(owner, SW_HIDE);
    SetWindowPos(input_window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    update_highlight();
    SetCursor(LoadCursorW(NULL, IDC_CROSS));
    return ERROR_SUCCESS;
failed:
    error = GetLastError();
    picker_stop();
    return error ? error : ERROR_NOT_ENOUGH_MEMORY;
}

HWND picker_finish(void)
{
    HWND target = active ? target_at(release_point) : NULL;
    picker_stop();
    return target;
}
