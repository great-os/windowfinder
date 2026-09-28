#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <stdlib.h>
#include <wchar.h>
#include "picker.h"
#include "settings.h"

#define APP_CLASS L"WindowFinder.Main"
#define APP_TITLE L"\u7a97\u53e3\u5b9a\u4f4d\u5de5\u5177"
#define ID_FINDER 1001
#define ID_HANDLE 1002
#define ID_PID 1003
#define ID_PATH 1004
#define ID_OPEN 1005
#define ID_STATUS 1006
#define ID_ROOT 1007
#define ID_TITLE 1008
#define ID_NAME 1009
#define ID_ICON 1010
#define ID_COPY_HANDLE 1011
#define ID_COPY_ROOT 1012
#define ID_COPY_PATH 1013
#define ID_COPY_ALL 1014
#define ID_PIN 1015
#define TIMER_PATH 2
#define PATH_CAPACITY 32768

typedef struct Selection {
    HWND hit, root;
    DWORD pid, path_error;
    HANDLE process;
    WCHAR title[1024], name[256], path[PATH_CAPACITY];
    HICON icon;
} Selection;

static HINSTANCE instance;
static HWND main_window, finder, handle_edit, root_edit, pid_edit, path_edit;
static HWND open_button, status_label, handle_label, root_label, pid_label, path_label;
static HWND title_label, title_edit, name_label, program_icon, pin_button;
static HWND copy_handle, copy_root, copy_path, copy_all, tooltip, hot_button;
static HFONT ui_font;
static HICON app_icon;
static Selection selection;
static BOOL opening_location, resolving_selection, pinned, drag_armed;
static UINT dpi = 96;
static POINT drag_origin;
static int last_health = -1;
static DWORD last_health_error;

static int scaled(int value)
{
    return MulDiv(value, (int)dpi, 96);
}

static void set_status(const WCHAR *text)
{
    SetWindowTextW(status_label, text);
}

static void status_error(const WCHAR *text, DWORD error)
{
    WCHAR buffer[256];
    swprintf(buffer, ARRAYSIZE(buffer), L"%ls (\u9519\u8bef %lu)", text, error);
    set_status(buffer);
}

static void format_handle(WCHAR *buffer, size_t count, HWND window)
{
    swprintf(buffer, count, L"0x%0*llX", (int)(sizeof(HWND) * 2), (unsigned long long)(UINT_PTR)window);
}

static void refresh_controls(BOOL show_health)
{
    BOOL busy = picker_active() || opening_location || resolving_selection;
    BOOL file_available = FALSE;
    DWORD file_error = 0, exit_code = STILL_ACTIVE;
    int health = 0;
    if (selection.path[0] && !busy) {
        DWORD attributes = GetFileAttributesW(selection.path);
        file_available = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        if (!file_available) file_error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_FILE_NOT_FOUND;
    }
    EnableWindow(finder, !busy);
    EnableWindow(pin_button, !busy);
    EnableWindow(open_button, !busy && file_available);
    EnableWindow(copy_handle, !busy && selection.hit != NULL);
    EnableWindow(copy_root, !busy && selection.root != NULL);
    EnableWindow(copy_path, !busy && selection.path[0] != L'\0');
    EnableWindow(copy_all, !busy && selection.hit != NULL);
    if (!selection.hit || busy) return;
    if (selection.path_error) {
        health = 1;
        file_error = selection.path_error;
    } else if (!file_available) {
        health = 2;
    } else if (selection.process && GetExitCodeProcess(selection.process, &exit_code) && exit_code != STILL_ACTIVE) {
        health = 3;
    }
    if (show_health || health != last_health || file_error != last_health_error) {
        if (health == 1) {
            if (file_error == ERROR_ACCESS_DENIED)
                status_error(L"\u6743\u9650\u4e0d\u8db3\uff0c\u65e0\u6cd5\u8bfb\u53d6\u8fdb\u7a0b\u8def\u5f84", file_error);
            else if (file_error == ERROR_INVALID_PARAMETER || file_error == ERROR_INVALID_HANDLE)
                status_error(L"\u76ee\u6807\u8fdb\u7a0b\u5df2\u9000\u51fa\uff0c\u65e0\u6cd5\u8bfb\u53d6\u8def\u5f84", file_error);
            else
                status_error(L"\u65e0\u6cd5\u8bfb\u53d6\u8fdb\u7a0b\u8def\u5f84", file_error);
        } else if (health == 2) {
            if (file_error == ERROR_FILE_NOT_FOUND || file_error == ERROR_PATH_NOT_FOUND)
                set_status(L"\u6587\u4ef6\u5df2\u4e0d\u5b58\u5728\uff0c\u4fdd\u7559\u4e0a\u6b21\u4fe1\u606f");
            else
                status_error(L"\u6587\u4ef6\u5f53\u524d\u4e0d\u53ef\u8bbf\u95ee\uff0c\u4fdd\u7559\u4e0a\u6b21\u4fe1\u606f", file_error);
        } else if (health == 3) {
            set_status(L"\u76ee\u6807\u7a0b\u5e8f\u5df2\u9000\u51fa\uff0c\u4fdd\u7559\u4e0a\u6b21\u4fe1\u606f");
        } else {
            set_status(L"\u5df2\u83b7\u53d6\u7a97\u53e3\u4fe1\u606f");
        }
    }
    last_health = health;
    last_health_error = file_error;
}

static void dispose_selection(void)
{
    if (selection.process) CloseHandle(selection.process);
    if (selection.icon) DestroyIcon(selection.icon);
    ZeroMemory(&selection, sizeof(selection));
}

static void restore_window(void)
{
    ShowWindow(main_window, SW_SHOW);
    if (pinned) SetWindowPos(main_window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(main_window);
    refresh_controls(FALSE);
    SetFocus(finder);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
}

static void cancel_picking(BOOL interrupted)
{
    if (!picker_active()) return;
    picker_stop();
    restore_window();
    if (interrupted)
        set_status(L"\u9009\u53d6\u5df2\u4e2d\u65ad\uff0c\u53ef\u91cd\u65b0\u9009\u53d6");
    else
        set_status(selection.hit ? L"\u5df2\u53d6\u6d88\uff0c\u4fdd\u7559\u4e0a\u6b21\u7ed3\u679c" : L"\u5df2\u53d6\u6d88\u9009\u53d6");
}

static void begin_picking(BOOL from_mouse)
{
    DWORD error;
    if (picker_active() || opening_location || resolving_selection) return;
    SendMessageW(tooltip, TTM_POP, 0, 0);
    error = picker_start(main_window, instance, from_mouse);
    refresh_controls(FALSE);
    if (error) status_error(L"\u65e0\u6cd5\u5f00\u59cb\u9009\u53d6", error);
}

static HICON target_icon(HWND root, const WCHAR *path)
{
    DWORD_PTR value = 0;
    HICON icon = NULL;
    SHFILEINFOW info = {0};
    if (SendMessageTimeoutW(root, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &value) && value)
        icon = CopyIcon((HICON)value);
    if (!icon && path[0] && SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON))
        icon = info.hIcon;
    return icon ? icon : CopyIcon(LoadIconW(NULL, IDI_APPLICATION));
}

static void finish_picking(void)
{
    HWND target;
    Selection next = {0};
    WCHAR buffer[64];
    DWORD length;
    const WCHAR *filename;
    if (!picker_active()) return;
    target = picker_finish();
    if (target) GetWindowThreadProcessId(target, &next.pid);
    if (!target || !next.pid || next.pid == GetCurrentProcessId()) {
        restore_window();
        set_status(L"\u672a\u9009\u4e2d\u6709\u6548\u7a97\u53e3");
        return;
    }
    resolving_selection = TRUE;
    restore_window();
    next.hit = target;
    next.root = GetAncestor(target, GA_ROOT);
    if (!next.root) next.root = target;
    GetWindowTextW(next.root, next.title, ARRAYSIZE(next.title));
    next.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, next.pid);
    if (next.process) {
        length = ARRAYSIZE(next.path);
        if (!QueryFullProcessImageNameW(next.process, 0, next.path, &length)) {
            next.path_error = GetLastError();
            next.path[0] = L'\0';
        }
    } else {
        next.path_error = GetLastError();
    }
    filename = wcsrchr(next.path, L'\\');
    swprintf(next.name, ARRAYSIZE(next.name), L"%ls", next.path[0] ?
        (filename ? filename + 1 : next.path) : L"\u672a\u77e5\u5e94\u7528");
    next.icon = target_icon(next.root, next.path);
    SendMessageW(program_icon, STM_SETICON, 0, 0);
    dispose_selection();
    selection = next;
    format_handle(buffer, ARRAYSIZE(buffer), selection.hit);
    SetWindowTextW(handle_edit, buffer);
    format_handle(buffer, ARRAYSIZE(buffer), selection.root);
    SetWindowTextW(root_edit, buffer);
    swprintf(buffer, ARRAYSIZE(buffer), L"%lu", selection.pid);
    SetWindowTextW(pid_edit, buffer);
    SetWindowTextW(title_edit, selection.title[0] ? selection.title : L"(\u65e0\u6807\u9898)");
    SetWindowTextW(name_label, selection.name);
    SetWindowTextW(path_edit, selection.path);
    SendMessageW(program_icon, STM_SETICON, (WPARAM)selection.icon, 0);
    last_health = -1;
    resolving_selection = FALSE;
    refresh_controls(TRUE);
    SetFocus(finder);
}

static void copy_selection(int command)
{
    WCHAR hit[32], root[32];
    WCHAR *text;
    HGLOBAL memory;
    size_t count;
    DWORD error;
    if (picker_active() || opening_location || resolving_selection || !selection.hit ||
        (command == ID_COPY_PATH && !selection.path[0])) return;
    format_handle(hit, ARRAYSIZE(hit), selection.hit);
    format_handle(root, ARRAYSIZE(root), selection.root);
    count = wcslen(selection.path) + wcslen(selection.title) + wcslen(selection.name) + 512;
    memory = GlobalAlloc(GMEM_MOVEABLE, count * sizeof(WCHAR));
    if (!memory) {
        status_error(L"\u65e0\u6cd5\u590d\u5236", ERROR_NOT_ENOUGH_MEMORY);
        return;
    }
    text = GlobalLock(memory);
    if (!text) {
        GlobalFree(memory);
        status_error(L"\u65e0\u6cd5\u590d\u5236", ERROR_NOT_ENOUGH_MEMORY);
        return;
    }
    if (command == ID_COPY_ALL) {
        swprintf(text, count,
            L"\u5e94\u7528\u7a0b\u5e8f: %ls\r\n\u7a97\u53e3\u6807\u9898: %ls\r\n"
            L"\u547d\u4e2d\u7a97\u53e3: %ls\r\n\u6240\u5c5e\u4e3b\u7a97\u53e3: %ls\r\n"
            L"\u8fdb\u7a0b ID: %lu\r\n\u8def\u5f84: %ls",
            selection.name, selection.title, hit, root, selection.pid, selection.path);
    } else {
        swprintf(text, count, L"%ls", command == ID_COPY_PATH ? selection.path :
            command == ID_COPY_ROOT ? root : hit);
    }
    GlobalUnlock(memory);
    if (!OpenClipboard(main_window)) {
        error = GetLastError();
        GlobalFree(memory);
        status_error(L"\u526a\u8d34\u677f\u6b63\u5fd9\uff0c\u8bf7\u91cd\u8bd5", error);
        return;
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        error = GetLastError();
        GlobalFree(memory);
        status_error(L"\u65e0\u6cd5\u590d\u5236", error);
    } else {
        set_status(L"\u5df2\u590d\u5236\u5230\u526a\u8d34\u677f");
    }
    CloseClipboard();
}

static void open_location(void)
{
    PIDLIST_ABSOLUTE item = NULL;
    HRESULT result;
    if (picker_active() || opening_location || resolving_selection) return;
    refresh_controls(FALSE);
    if (!IsWindowEnabled(open_button)) return;
    opening_location = TRUE;
    refresh_controls(FALSE);
    result = SHParseDisplayName(selection.path, NULL, &item, 0, NULL);
    if (SUCCEEDED(result)) result = SHOpenFolderAndSelectItems(item, 0, NULL, 0);
    CoTaskMemFree(item);
    opening_location = FALSE;
    refresh_controls(FALSE);
    if (FAILED(result)) {
        if (HRESULT_FACILITY(result) == FACILITY_WIN32)
            status_error(L"\u65e0\u6cd5\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939", HRESULT_CODE(result));
        else {
            WCHAR buffer[128];
            swprintf(buffer, ARRAYSIZE(buffer),
                L"\u65e0\u6cd5\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939 (0x%08lX)", (unsigned long)result);
            set_status(buffer);
        }
    } else {
        set_status(L"\u5df2\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939");
    }
}

static void draw_symbol(HDC dc, RECT rect, COLORREF color, BOOL crosshair)
{
    int size = min(rect.right - rect.left, rect.bottom - rect.top);
    int x = (rect.left + rect.right) / 2, y = (rect.top + rect.bottom) / 2;
    int radius = size / 4, arm = size * 3 / 8;
    HPEN pen = CreatePen(PS_SOLID, max(1, size / 28), color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (crosshair) {
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
        MoveToEx(dc, x - arm, y, NULL);
        LineTo(dc, x + arm + 1, y);
        MoveToEx(dc, x, y - arm, NULL);
        LineTo(dc, x, y + arm + 1);
    } else {
        int offset = max(2, size / 8);
        Rectangle(dc, x - radius, y - radius, x + radius - offset, y + radius - offset);
        Rectangle(dc, x - radius + offset, y - radius + offset, x + radius, y + radius);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static HICON create_app_icon(void)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, NULL);
    HGDIOBJ old;
    ICONINFO info = {0};
    HICON icon = NULL;
    RECT rect = {0, 0, 32, 32};
    if (dc && color && mask) {
        old = SelectObject(dc, mask);
        PatBlt(dc, 0, 0, 32, 32, BLACKNESS);
        SelectObject(dc, color);
        FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
        draw_symbol(dc, rect, GetSysColor(COLOR_WINDOWTEXT), TRUE);
        SelectObject(dc, old);
        info.fIcon = TRUE;
        info.hbmMask = mask;
        info.hbmColor = color;
        icon = CreateIconIndirect(&info);
    }
    if (mask) DeleteObject(mask);
    if (color) DeleteObject(color);
    if (dc) DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return icon;
}

static void update_font(void)
{
    NONCLIENTMETRICSW metrics = {0};
    HFONT old = ui_font;
    HWND child;
    metrics.cbSize = sizeof(metrics);
    ui_font = SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)
        ? CreateFontIndirectW(&metrics.lfMessageFont) : NULL;
    for (child = GetWindow(main_window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT,
            (WPARAM)(ui_font ? ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (old) DeleteObject(old);
}

static void place(HWND control, int x, int y, int width, int height)
{
    MoveWindow(control, scaled(x), scaled(y), scaled(width), scaled(height), TRUE);
}

static void layout(void)
{
    RECT rect;
    int width, height;
    GetClientRect(main_window, &rect);
    width = MulDiv(rect.right, 96, (int)dpi);
    height = MulDiv(rect.bottom, 96, (int)dpi);
    place(finder, 20, 20, 60, 60);
    place(program_icon, 100, 20, 32, 32);
    place(name_label, 144, 24, width - 272, 24);
    place(pin_button, width - 104, 20, 84, 28);
    place(title_label, 100, 62, 76, 24);
    place(title_edit, 180, 58, width - 200, 28);
    place(handle_label, 20, 110, 108, 24);
    place(handle_edit, 134, 106, width - 198, 28);
    place(copy_handle, width - 52, 106, 32, 28);
    place(root_label, 20, 148, 108, 24);
    place(root_edit, 134, 144, width - 198, 28);
    place(copy_root, width - 52, 144, 32, 28);
    place(pid_label, 20, 186, 108, 24);
    place(pid_edit, 134, 182, 156, 28);
    place(path_label, 20, 226, width - 88, 24);
    place(copy_path, width - 52, 220, 32, 28);
    place(path_edit, 20, 254, width - 40, height - 362);
    place(status_label, 20, height - 100, width - 40, 40);
    place(copy_all, 20, height - 52, 132, 32);
    place(open_button, width - 184, height - 52, 164, 32);
}

static void disarm_drag(HWND window)
{
    drag_armed = FALSE;
    SendMessageW(window, BM_SETSTATE, FALSE, 0);
    if (GetCapture() == window) ReleaseCapture();
}

static LRESULT CALLBACK icon_button_proc(HWND window, UINT message, WPARAM wparam,
                                         LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    (void)id;
    (void)data;
    if (window == finder) {
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
            SetFocus(window);
            GetCursorPos(&drag_origin);
            drag_armed = TRUE;
            SetCapture(window);
            SendMessageW(window, BM_SETSTATE, TRUE, 0);
            return 0;
        }
        if (message == WM_MOUSEMOVE && drag_armed) {
            POINT point;
            GetCursorPos(&point);
            if (labs(point.x - drag_origin.x) >= GetSystemMetricsForDpi(SM_CXDRAG, dpi) ||
                labs(point.y - drag_origin.y) >= GetSystemMetricsForDpi(SM_CYDRAG, dpi)) {
                disarm_drag(window);
                begin_picking(TRUE);
                return 0;
            }
        }
        if ((message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_CANCELMODE ||
            (message == WM_KEYDOWN && wparam == VK_ESCAPE)) && drag_armed) {
            disarm_drag(window);
            return 0;
        }
        if (message == WM_CAPTURECHANGED && drag_armed) {
            drag_armed = FALSE;
            SendMessageW(window, BM_SETSTATE, FALSE, 0);
        }
        if (message == WM_SETCURSOR) {
            SetCursor(LoadCursorW(NULL, IDC_CROSS));
            return TRUE;
        }
    }
    if (message == WM_MOUSEMOVE && hot_button != window) {
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
        if (hot_button) InvalidateRect(hot_button, NULL, FALSE);
        hot_button = window;
        TrackMouseEvent(&tracking);
        InvalidateRect(window, NULL, FALSE);
    }
    if (message == WM_MOUSELEAVE && hot_button == window) {
        hot_button = NULL;
        InvalidateRect(window, NULL, FALSE);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

static HWND control(DWORD ex_style, const WCHAR *class_name, const WCHAR *text, DWORD style, int id)
{
    return CreateWindowExW(ex_style, class_name, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, main_window, (HMENU)(INT_PTR)id, instance, NULL);
}

static HWND readonly_edit(int id)
{
    return control(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, id);
}

static HWND icon_button(const WCHAR *name, int id)
{
    TOOLINFOW tool = {0};
    HWND button = control(0, L"BUTTON", name, BS_OWNERDRAW | WS_TABSTOP, id);
    if (!button || !SetWindowSubclass(button, icon_button_proc, 1, 0)) return NULL;
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = main_window;
    tool.uId = (UINT_PTR)button;
    tool.lpszText = (WCHAR *)name;
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
    return button;
}

static BOOL create_controls(void)
{
    tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, main_window, NULL, instance, NULL);
    if (!tooltip) return FALSE;
    finder = icon_button(L"\u9009\u53d6\u7a97\u53e3", ID_FINDER);
    program_icon = control(0, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, ID_ICON);
    name_label = control(0, L"STATIC", L"\u5c1a\u672a\u9009\u62e9\u5e94\u7528", SS_ENDELLIPSIS | SS_NOPREFIX, ID_NAME);
    pin_button = control(0, L"BUTTON", L"\u7f6e\u9876", BS_AUTOCHECKBOX | WS_TABSTOP, ID_PIN);
    title_label = control(0, L"STATIC", L"\u7a97\u53e3\u6807\u9898", 0, 0);
    title_edit = readonly_edit(ID_TITLE);
    handle_label = control(0, L"STATIC", L"\u547d\u4e2d\u7a97\u53e3", 0, 0);
    handle_edit = readonly_edit(ID_HANDLE);
    copy_handle = icon_button(L"\u590d\u5236\u547d\u4e2d\u7a97\u53e3\u53e5\u67c4", ID_COPY_HANDLE);
    root_label = control(0, L"STATIC", L"\u6240\u5c5e\u4e3b\u7a97\u53e3", 0, 0);
    root_edit = readonly_edit(ID_ROOT);
    copy_root = icon_button(L"\u590d\u5236\u4e3b\u7a97\u53e3\u53e5\u67c4", ID_COPY_ROOT);
    pid_label = control(0, L"STATIC", L"\u8fdb\u7a0b ID", 0, 0);
    pid_edit = readonly_edit(ID_PID);
    path_label = control(0, L"STATIC", L"\u5e94\u7528\u7a0b\u5e8f\u8def\u5f84", 0, 0);
    copy_path = icon_button(L"\u590d\u5236\u8def\u5f84", ID_COPY_PATH);
    path_edit = control(WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, ID_PATH);
    status_label = control(0, L"STATIC", L"\u5c1a\u672a\u9009\u62e9\u7a97\u53e3", SS_NOPREFIX, ID_STATUS);
    copy_all = control(0, L"BUTTON", L"\u590d\u5236\u5168\u90e8\u4fe1\u606f", BS_PUSHBUTTON | WS_TABSTOP, ID_COPY_ALL);
    open_button = control(0, L"BUTTON", L"\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939", BS_PUSHBUTTON | WS_TABSTOP, ID_OPEN);
    if (!finder || !program_icon || !name_label || !pin_button || !title_label || !title_edit ||
        !handle_label || !handle_edit || !copy_handle || !root_label || !root_edit || !copy_root ||
        !pid_label || !pid_edit || !path_label || !copy_path || !path_edit || !status_label || !copy_all || !open_button)
        return FALSE;
    SendMessageW(path_edit, EM_SETLIMITTEXT, PATH_CAPACITY - 1, 0);
    update_font();
    layout();
    refresh_controls(FALSE);
    return SetTimer(main_window, TIMER_PATH, 1500, NULL) != 0;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        main_window = window;
        dpi = GetDpiForWindow(window);
        return create_controls() ? 0 : -1;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) layout();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT minimum = {0, 0, scaled(560), scaled(420)};
        AdjustWindowRectExForDpi(&minimum, (DWORD)GetWindowLongPtrW(window, GWL_STYLE), FALSE, 0, dpi);
        ((MINMAXINFO *)lparam)->ptMinTrackSize.x = minimum.right - minimum.left;
        ((MINMAXINFO *)lparam)->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT *rect = (const RECT *)lparam;
        dpi = HIWORD(wparam);
        update_font();
        SetWindowPos(window, NULL, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        update_font();
        InvalidateRect(window, NULL, TRUE);
        return 0;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *item = (const DRAWITEMSTRUCT *)lparam;
        RECT rect = item->rcItem;
        HTHEME theme = OpenThemeData(item->hwndItem, L"BUTTON");
        int state = (item->itemState & ODS_DISABLED) ? PBS_DISABLED :
            (item->itemState & ODS_SELECTED) ? PBS_PRESSED :
            hot_button == item->hwndItem ? PBS_HOT : PBS_NORMAL;
        if (theme) {
            DrawThemeBackground(theme, item->hDC, BP_PUSHBUTTON, state, &rect, NULL);
            CloseThemeData(theme);
        } else {
            DrawFrameControl(item->hDC, &rect, DFC_BUTTON, DFCS_BUTTONPUSH |
                ((item->itemState & ODS_SELECTED) ? DFCS_PUSHED : 0) |
                ((item->itemState & ODS_DISABLED) ? DFCS_INACTIVE : 0));
        }
        draw_symbol(item->hDC, rect, GetSysColor((item->itemState & ODS_DISABLED) ? COLOR_GRAYTEXT : COLOR_BTNTEXT),
            item->CtlID == ID_FINDER);
        if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
            InflateRect(&rect, -scaled(4), -scaled(4));
            DrawFocusRect(item->hDC, &rect);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            int command = LOWORD(wparam);
            if (command == ID_FINDER) begin_picking(FALSE);
            else if (command == ID_OPEN) open_location();
            else if (command >= ID_COPY_HANDLE && command <= ID_COPY_ALL) copy_selection(command);
            else if (command == ID_PIN && !picker_active() && !opening_location && !resolving_selection) {
                pinned = SendMessageW(pin_button, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SetWindowPos(window, pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                settings_save(window, dpi, pinned);
            }
        }
        return 0;
    case WM_PICK_FINISH:
        finish_picking();
        return 0;
    case WM_PICK_CANCEL:
        cancel_picking(wparam == PICK_CANCEL_INTERRUPTED);
        return 0;
    case WM_CANCELMODE:
        cancel_picking(TRUE);
        return 0;
    case WM_DISPLAYCHANGE:
        cancel_picking(TRUE);
        settings_keep_visible(window);
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_PATH) refresh_controls(FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE && open_button) refresh_controls(FALSE);
        return 0;
    case WM_EXITSIZEMOVE:
        settings_save(window, dpi, pinned);
        return 0;
    case WM_QUERYENDSESSION:
        cancel_picking(TRUE);
        return TRUE;
    case WM_ENDSESSION:
        if (wparam) settings_save(window, dpi, pinned);
        return 0;
    case WM_CLOSE:
        settings_save(window, dpi, pinned);
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        picker_stop();
        KillTimer(window, TIMER_PATH);
        dispose_selection();
        if (ui_font) DeleteObject(ui_font);
        ui_font = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE previous, PWSTR command, int show)
{
    WNDCLASSEXW window_class = {0};
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    MSG message;
    RECT rect = {0, 0, 660, 460};
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
    HRESULT com;
    int result;
    (void)previous;
    (void)command;
    instance = app;
    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com) || !InitCommonControlsEx(&controls)) {
        MessageBoxW(NULL, L"\u521d\u59cb\u5316\u5931\u8d25", APP_TITLE, MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
    settings_init();
    app_icon = create_app_icon();
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = app_icon;
    window_class.hIconSm = app_icon;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window_class.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&window_class)) {
        if (app_icon) DestroyIcon(app_icon);
        CoUninitialize();
        return 1;
    }
    dpi = GetDpiForSystem();
    rect.right = scaled(rect.right);
    rect.bottom = scaled(rect.bottom);
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    main_window = CreateWindowExW(0, APP_CLASS, APP_TITLE, style, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, instance, NULL);
    if (!main_window) {
        MessageBoxW(NULL, L"\u65e0\u6cd5\u521b\u5efa\u7a97\u53e3", APP_TITLE, MB_OK | MB_ICONERROR);
        if (app_icon) DestroyIcon(app_icon);
        CoUninitialize();
        return 1;
    }
    pinned = settings_restore(main_window, dpi);
    SendMessageW(pin_button, BM_SETCHECK, pinned ? BST_CHECKED : BST_UNCHECKED, 0);
    ShowWindow(main_window, show);
    SetWindowPos(main_window, pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(main_window);
    SetFocus(finder);
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (!IsDialogMessageW(main_window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (app_icon) DestroyIcon(app_icon);
    CoUninitialize();
    return result == -1 ? 1 : (int)message.wParam;
}
