#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <shlobj.h>
#include <wchar.h>
#include "settings.h"

static WCHAR settings_path[32768];

void settings_init(void)
{
    PWSTR directory = NULL;
    DWORD length = GetEnvironmentVariableW(L"WINDOWFINDER_SETTINGS_FILE", settings_path, ARRAYSIZE(settings_path));
    if (length && length < ARRAYSIZE(settings_path)) return;
    settings_path[0] = L'\0';
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &directory))) {
        if (wcslen(directory) + 40 < ARRAYSIZE(settings_path)) {
            swprintf(settings_path, ARRAYSIZE(settings_path), L"%ls\\WindowFinder", directory);
            if (CreateDirectoryW(settings_path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
                swprintf(settings_path, ARRAYSIZE(settings_path), L"%ls\\WindowFinder\\settings.ini", directory);
            else
                settings_path[0] = L'\0';
        }
        CoTaskMemFree(directory);
    }
}

static int read_number(const WCHAR *key, int fallback, int minimum, int maximum)
{
    WCHAR text[32], *end;
    long value;
    if (!settings_path[0] || !GetPrivateProfileStringW(L"Window", key, L"", text, ARRAYSIZE(text), settings_path))
        return fallback;
    value = wcstol(text, &end, 10);
    return *end || value < minimum || value > maximum ? fallback : (int)value;
}

BOOL settings_restore(HWND window, UINT dpi)
{
    RECT rect;
    MONITORINFO monitor = {0};
    int width, height, logical_width, logical_height, pass;
    BOOL pinned = read_number(L"Pinned", 0, 0, 1);
    GetWindowRect(window, &rect);
    logical_width = read_number(L"Width", MulDiv(rect.right - rect.left, 96, (int)dpi), 576, 8192);
    logical_height = read_number(L"Height", MulDiv(rect.bottom - rect.top, 96, (int)dpi), 459, 8192);
    rect.left = read_number(L"X", rect.left, -1000000, 1000000);
    rect.top = read_number(L"Y", rect.top, -1000000, 1000000);
    /* Recompute the logical size after Windows establishes the destination DPI. */
    for (pass = 0; pass < 2; ++pass) {
        width = MulDiv(logical_width, (int)dpi, 96);
        height = MulDiv(logical_height, (int)dpi, 96);
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
            width = min(width, monitor.rcWork.right - monitor.rcWork.left);
            height = min(height, monitor.rcWork.bottom - monitor.rcWork.top);
            rect.left = max(monitor.rcWork.left, min(rect.left, monitor.rcWork.right - width));
            rect.top = max(monitor.rcWork.top, min(rect.top, monitor.rcWork.bottom - height));
        }
        SetWindowPos(window, NULL, rect.left, rect.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
        dpi = GetDpiForWindow(window);
    }
    return pinned;
}

static void write_number(const WCHAR *key, int value)
{
    WCHAR text[32];
    swprintf(text, ARRAYSIZE(text), L"%d", value);
    WritePrivateProfileStringW(L"Window", key, text, settings_path);
}

void settings_keep_visible(HWND window)
{
    RECT rect;
    MONITORINFO monitor = {0};
    int width, height, x, y;
    if (IsIconic(window) || !GetWindowRect(window, &rect)) return;
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    width = min(rect.right - rect.left, monitor.rcWork.right - monitor.rcWork.left);
    height = min(rect.bottom - rect.top, monitor.rcWork.bottom - monitor.rcWork.top);
    x = max(monitor.rcWork.left, min(rect.left, monitor.rcWork.right - width));
    y = max(monitor.rcWork.top, min(rect.top, monitor.rcWork.bottom - height));
    SetWindowPos(window, NULL, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
}

void settings_save(HWND window, UINT dpi, BOOL pinned)
{
    RECT rect;
    if (!settings_path[0]) return;
    if (!IsIconic(window) && GetWindowRect(window, &rect)) {
        write_number(L"X", rect.left);
        write_number(L"Y", rect.top);
        write_number(L"Width", MulDiv(rect.right - rect.left, 96, (int)dpi));
        write_number(L"Height", MulDiv(rect.bottom - rect.top, 96, (int)dpi));
    }
    write_number(L"Pinned", pinned);
}
