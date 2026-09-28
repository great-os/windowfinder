#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>
#include <wchar.h>

#define CHECK(condition, description) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (Windows error %lu)\n", description, GetLastError()); \
        goto cleanup; \
    } \
    printf("PASS: %s\n", description); \
} while (0)

static int target_releases;
static DWORD wanted_pid;
static HWND found_window;

static LRESULT CALLBACK target_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_LBUTTONUP) ++target_releases;
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void pump(DWORD milliseconds)
{
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static BOOL CALLBACK find_window(HWND window, LPARAM unused)
{
    DWORD pid;
    (void)unused;
    GetWindowThreadProcessId(window, &pid);
    if (pid == wanted_pid && !GetWindow(window, GW_OWNER) && IsWindowVisible(window)) {
        found_window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND wait_for_window(DWORD pid)
{
    int attempt;
    for (attempt = 0; attempt < 200; ++attempt) {
        wanted_pid = pid;
        found_window = NULL;
        EnumWindows(find_window, 0);
        if (found_window) {
            HWND result = found_window;
            pump(100);
            return result;
        }
        pump(25);
    }
    return NULL;
}

static BOOL wait_visible(HWND window, BOOL visible)
{
    int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        if (!!IsWindowVisible(window) == !!visible) return TRUE;
        pump(20);
    }
    return FALSE;
}

static BOOL mouse(DWORD flags)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof(input)) == 1;
}

static BOOL key(WORD virtual_key, BOOL up)
{
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtual_key;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &input, sizeof(input)) == 1;
}

static BOOL mouse_at(DWORD flags, POINT point)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    input.mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535,
        GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
    input.mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535,
        GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    return SendInput(1, &input, sizeof(input)) == 1;
}

static POINT center(HWND window)
{
    RECT rect;
    POINT point;
    GetWindowRect(window, &rect);
    point.x = (rect.left + rect.right) / 2;
    point.y = (rect.top + rect.bottom) / 2;
    return point;
}

static BOOL start_drag(HWND app)
{
    POINT point = center(GetDlgItem(app, 1001));
    DWORD foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    DWORD current_thread = GetCurrentThreadId();
    BOOL attached = foreground_thread != current_thread &&
        AttachThreadInput(current_thread, foreground_thread, TRUE);
    SetWindowPos(app, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(app, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(app);
    if (attached) AttachThreadInput(current_thread, foreground_thread, FALSE);
    pump(300);
    if (!SetCursorPos(point.x, point.y) || WindowFromPoint(point) != GetDlgItem(app, 1001)) {
        HWND hit = WindowFromPoint(point);
        POINT now = center(GetDlgItem(app, 1001));
        RECT current_rect;
        WCHAR hit_class[256], hit_title[256];
        char class_text[1024], title_text[1024];
        DWORD hit_pid;
        GetClassNameW(hit, hit_class, ARRAYSIZE(hit_class));
        GetWindowTextW(GetAncestor(hit, GA_ROOT), hit_title, ARRAYSIZE(hit_title));
        GetWindowThreadProcessId(hit, &hit_pid);
        WideCharToMultiByte(CP_UTF8, 0, hit_class, -1, class_text, sizeof(class_text), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, hit_title, -1, title_text, sizeof(title_text), NULL, NULL);
        fprintf(stderr, "Finder is not reachable at (%ld, %ld), hit %p, expected %p\n",
            point.x, point.y, (void *)WindowFromPoint(point), (void *)GetDlgItem(app, 1001));
        fprintf(stderr, "Hit class %s, title %s, PID %lu\n", class_text, title_text, hit_pid);
        GetWindowRect(app, &current_rect);
        fprintf(stderr, "Current finder center (%ld,%ld), app rect (%ld,%ld,%ld,%ld), foreground %p, app %p\n",
            now.x, now.y, current_rect.left, current_rect.top, current_rect.right,
            current_rect.bottom, (void *)GetForegroundWindow(), (void *)app);
        return FALSE;
    }
    if (!mouse_at(MOUSEEVENTF_LEFTDOWN, point)) return FALSE;
    pump(60);
    if (!IsWindowVisible(app)) {
        fprintf(stderr, "Application hid before the drag threshold\n");
        return FALSE;
    }
    point.x += 1;
    mouse_at(0, point);
    pump(30);
    if (!IsWindowVisible(app)) return FALSE;
    point.x += GetSystemMetricsForDpi(SM_CXDRAG, GetDpiForWindow(app)) + 5;
    return mouse_at(0, point) && wait_visible(app, FALSE);
}

static BOOL release_over(HWND app, POINT point)
{
    return mouse_at(MOUSEEVENTF_LEFTUP, point) && wait_visible(app, TRUE);
}

static BOOL read_control(HWND app, int id, WCHAR *buffer, size_t count)
{
    DWORD_PTR result;
    buffer[0] = L'\0';
    return SendMessageTimeoutW(GetDlgItem(app, id), WM_GETTEXT, (WPARAM)count,
        (LPARAM)buffer, SMTO_ABORTIFHUNG, 1000, &result) != 0;
}

static BOOL wait_enabled(HWND control, BOOL enabled)
{
    int attempt;
    for (attempt = 0; attempt < 150; ++attempt) {
        if (!!IsWindowEnabled(control) == !!enabled) return TRUE;
        pump(20);
    }
    return FALSE;
}

static BOOL wait_status(HWND app, const WCHAR *expected)
{
    int attempt;
    WCHAR text[256];
    for (attempt = 0; attempt < 300; ++attempt) {
        if (read_control(app, 1006, text, ARRAYSIZE(text)) && wcscmp(text, expected) == 0)
            return TRUE;
        pump(25);
    }
    return FALSE;
}

static BOOL read_clipboard(WCHAR *buffer, size_t count)
{
    HANDLE data;
    const WCHAR *text;
    BOOL ok = FALSE;
    if (!OpenClipboard(NULL)) return FALSE;
    data = GetClipboardData(CF_UNICODETEXT);
    text = data ? GlobalLock(data) : NULL;
    if (text && wcslen(text) < count) {
        wcscpy(buffer, text);
        ok = TRUE;
    }
    if (text) GlobalUnlock(data);
    CloseClipboard();
    return ok;
}

static BOOL controls_fit(HWND app)
{
    RECT client, rect;
    HWND child;
    GetClientRect(app, &client);
    for (child = GetWindow(app, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child)) continue;
        GetWindowRect(child, &rect);
        MapWindowPoints(NULL, app, (POINT *)&rect, 2);
        if (rect.left < 0 || rect.top < 0 || rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top) return FALSE;
    }
    return TRUE;
}

static BOOL start_process(const WCHAR *path, const WCHAR *arguments, PROCESS_INFORMATION *process)
{
    WCHAR command[32768];
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    swprintf(command, ARRAYSIZE(command), L"\"%ls\" %ls", path, arguments);
    return CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, process);
}

static void close_process(PROCESS_INFORMATION *process, HWND window)
{
    if (!process->hProcess) return;
    if (window) PostMessageW(window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process->hProcess, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(process->hProcess, 1);
        WaitForSingleObject(process->hProcess, 2000);
    }
    CloseHandle(process->hThread);
    CloseHandle(process->hProcess);
    ZeroMemory(process, sizeof(*process));
}

static BOOL screenshot(HWND window, const WCHAR *path)
{
    RECT rect;
    BITMAPINFO info = {0};
    BITMAPFILEHEADER header = {0};
    HDC screen, dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    void *pixels = NULL;
    HANDLE file;
    DWORD written;
    BOOL ok = FALSE;
    GetWindowRect(window, &rect);
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = rect.right - rect.left;
    info.bmiHeader.biHeight = -(rect.bottom - rect.top);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biSizeImage = (DWORD)(info.bmiHeader.biWidth * -info.bmiHeader.biHeight * 4);
    screen = GetDC(NULL);
    dc = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!dc || !bitmap) goto done;
    old = SelectObject(dc, bitmap);
    BitBlt(dc, 0, 0, info.bmiHeader.biWidth, -info.bmiHeader.biHeight,
        screen, rect.left, rect.top, SRCCOPY);
    GdiFlush();
    SelectObject(dc, old);
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + info.bmiHeader.biSizeImage;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        ok = WriteFile(file, &header, sizeof(header), &written, NULL) &&
             WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL) &&
             WriteFile(file, pixels, info.bmiHeader.biSizeImage, &written, NULL);
        CloseHandle(file);
    }
done:
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return ok;
}

int wmain(int argc, WCHAR **argv)
{
    WNDCLASSW target_class = {0};
    PROCESS_INFORMATION app_process = {0}, fixture_process = {0};
    HWND target = NULL, child = NULL, app = NULL, fixture = NULL;
    HWND folder;
    WCHAR actual[32768], expected[32768], self_path[32768], hit_text[32], root_text[32];
    WCHAR temp_root[MAX_PATH], fixture_directory[MAX_PATH] = L"", fixture_path[MAX_PATH] = L"";
    WCHAR settings_file[MAX_PATH] = L"";
    RECT work, app_rect, saved_rect, restored_rect;
    POINT original_cursor, point;
    int result = 1;
    IDataObject *saved_clipboard = NULL;
    BOOL clipboard_changed = FALSE, ole_initialized = FALSE;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GetCursorPos(&original_cursor);
    target_class.hInstance = GetModuleHandleW(NULL);
    target_class.lpfnWndProc = target_proc;
    target_class.lpszClassName = L"WindowFinder.TestTarget";
    target_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    target_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassW(&target_class);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    target = CreateWindowW(target_class.lpszClassName, L"WindowFinder test target",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, max(work.left, work.right - 760),
        max(work.top, work.bottom - 520), 720, 480,
        NULL, NULL, target_class.hInstance, NULL);
    if (argc == 2 && wcscmp(argv[1], L"--fixture") == 0) {
        while (IsWindow(target)) pump(20);
        return 0;
    }
    CHECK((argc == 2 || (argc == 3 && wcscmp(argv[2], L"--open-folder") == 0)) && target, "test setup");
    CHECK(SUCCEEDED(OleInitialize(NULL)), "test clipboard initialization");
    ole_initialized = TRUE;
    CHECK(SUCCEEDED(OleGetClipboard(&saved_clipboard)), "original clipboard retained for restoration");
    CHECK(GetTempPathW(ARRAYSIZE(temp_root), temp_root), "temporary directory available");
    swprintf(settings_file, ARRAYSIZE(settings_file), L"%lsWindowFinder-settings-%lu.ini", temp_root, GetCurrentProcessId());
    CHECK(SetEnvironmentVariableW(L"WINDOWFINDER_SETTINGS_FILE", settings_file), "test settings isolated from user preferences");
    CHECK(!GetSystemMetrics(SM_SWAPBUTTON), "test desktop uses standard left mouse button");
    child = CreateWindowW(target_class.lpszClassName, L"Child target", WS_CHILD | WS_VISIBLE,
        130, 110, 320, 110, target, NULL, target_class.hInstance, NULL);
    CHECK(child, "child window created");
    CHECK(start_process(argv[1], L"", &app_process), "application starts");
    app = wait_for_window(app_process.dwProcessId);
    CHECK(app, "application window appears");
    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(app, HWND_TOP, max(work.left, work.right - 760),
        max(work.top, work.bottom - 560), 680, 520, 0);
    folder = GetDlgItem(app, 1005);
    CHECK(folder && !IsWindowEnabled(folder), "folder button initially disabled");
    CHECK(!IsWindowEnabled(GetDlgItem(app, 1011)) && !IsWindowEnabled(GetDlgItem(app, 1013)) &&
        !IsWindowEnabled(GetDlgItem(app, 1014)), "copy actions initially disabled");
    CHECK(SendMessageW(GetDlgItem(app, 1015), BM_GETCHECK, 0, 0) == BST_UNCHECKED, "pin is off by default");
    CHECK(controls_fit(app), "controls fit the default layout");

    point = center(child);
    GetWindowRect(app, &app_rect);
    CHECK(PtInRect(&app_rect, point), "target point initially covered by application");
    CHECK(start_drag(app), "application hides while dragging");
    CHECK(!IsWindowEnabled(folder), "folder button disabled while dragging");
    mouse_at(0, point);
    pump(150);
    {
        HWND input = FindWindowW(L"WindowFinder.Input", NULL);
        HWND highlight = FindWindowW(L"WindowFinder.Highlight", NULL);
        RECT highlighted, child_rect;
        CURSORINFO cursor = {0};
        cursor.cbSize = sizeof(cursor);
        CHECK(input && WindowFromPoint(point) == input, "temporary input layer owns pointer hit testing");
        CHECK(highlight && IsWindowVisible(highlight), "target highlight is visible");
        GetWindowRect(highlight, &highlighted);
        GetWindowRect(child, &child_rect);
        CHECK(EqualRect(&highlighted, &child_rect), "highlight matches the exact child bounds");
        CHECK(GetCursorInfo(&cursor) && cursor.hCursor == LoadCursorW(NULL, IDC_CROSS), "crosshair cursor persists over target");
        CHECK(screenshot(highlight, L"build\\highlight.bmp"), "highlight screenshot captured");
        {
            HDC screen = GetDC(NULL);
            COLORREF pixel = GetPixel(screen, highlighted.left + 1, highlighted.top + 1);
            ReleaseDC(NULL, screen);
            CHECK(pixel == GetSysColor(COLOR_HIGHLIGHT), "highlight border is actually painted");
        }
    }
    CHECK(release_over(app, point), "application returns after mouse release");
    CHECK(wait_enabled(folder, TRUE), "valid path enables folder button");
    CHECK(!FindWindowW(L"WindowFinder.Input", NULL) && !FindWindowW(L"WindowFinder.Highlight", NULL),
        "temporary selection windows removed after release");
    swprintf(expected, ARRAYSIZE(expected), L"0x%0*llX", (int)(sizeof(HWND) * 2),
        (unsigned long long)(UINT_PTR)child);
    CHECK(read_control(app, 1002, actual, ARRAYSIZE(actual)) && wcscmp(actual, expected) == 0,
        "exact child HWND recorded before restoring application");
    wcscpy(hit_text, expected);
    swprintf(root_text, ARRAYSIZE(root_text), L"0x%0*llX", (int)(sizeof(HWND) * 2),
        (unsigned long long)(UINT_PTR)target);
    CHECK(read_control(app, 1007, actual, ARRAYSIZE(actual)) && wcscmp(actual, root_text) == 0,
        "top-level HWND displayed separately");
    CHECK(read_control(app, 1008, actual, ARRAYSIZE(actual)) && wcscmp(actual, L"WindowFinder test target") == 0,
        "top-level window title displayed");
    CHECK(read_control(app, 1009, actual, ARRAYSIZE(actual)) && wcscmp(actual, L"smoke.exe") == 0,
        "application filename displayed");
    CHECK(SendMessageW(GetDlgItem(app, 1010), STM_GETICON, 0, 0), "application icon displayed");
    swprintf(expected, ARRAYSIZE(expected), L"%lu", GetCurrentProcessId());
    CHECK(read_control(app, 1003, actual, ARRAYSIZE(actual)) && wcscmp(actual, expected) == 0,
        "correct process ID");
    GetModuleFileNameW(NULL, self_path, ARRAYSIZE(self_path));
    CHECK(read_control(app, 1004, actual, ARRAYSIZE(actual)) && _wcsicmp(actual, self_path) == 0,
        "correct executable path");
    CHECK(target_releases == 0, "selection release does not click underlying target");

    clipboard_changed = TRUE;
    SendMessageW(GetDlgItem(app, 1011), BM_CLICK, 0, 0);
    CHECK(read_clipboard(actual, ARRAYSIZE(actual)) && wcscmp(actual, hit_text) == 0, "copy hit HWND");
    SendMessageW(GetDlgItem(app, 1012), BM_CLICK, 0, 0);
    CHECK(read_clipboard(actual, ARRAYSIZE(actual)) && wcscmp(actual, root_text) == 0, "copy top-level HWND");
    SendMessageW(GetDlgItem(app, 1013), BM_CLICK, 0, 0);
    CHECK(read_clipboard(actual, ARRAYSIZE(actual)) && _wcsicmp(actual, self_path) == 0, "copy executable path");
    SendMessageW(GetDlgItem(app, 1014), BM_CLICK, 0, 0);
    CHECK(read_clipboard(actual, ARRAYSIZE(actual)) && wcsstr(actual, hit_text) && wcsstr(actual, root_text) &&
        wcsstr(actual, self_path) && wcsstr(actual, L"WindowFinder test target"), "copy complete window information");
    CHECK(OpenClipboard(target), "clipboard contention setup");
    SendMessageW(GetDlgItem(app, 1011), BM_CLICK, 0, 0);
    CloseClipboard();
    CHECK(read_control(app, 1006, actual, ARRAYSIZE(actual)) && wcsstr(actual, L"\u526a\u8d34\u677f\u6b63\u5fd9"),
        "clipboard contention has an actionable error");

    CHECK(start_drag(app), "second selection hides application");
    CHECK(key(VK_ESCAPE, FALSE) && wait_visible(app, TRUE), "Escape restores application");
    key(VK_ESCAPE, TRUE);
    mouse(MOUSEEVENTF_LEFTUP);
    CHECK(wait_enabled(folder, TRUE), "cancelled selection restores folder button for previous result");
    CHECK(read_control(app, 1004, actual, ARRAYSIZE(actual)) && _wcsicmp(actual, self_path) == 0,
        "Escape preserves the previous path");
    CHECK(read_control(app, 1002, actual, ARRAYSIZE(actual)) && wcscmp(actual, hit_text) == 0,
        "Escape preserves the previous handle");
    CHECK(!FindWindowW(L"WindowFinder.Input", NULL) && !FindWindowW(L"WindowFinder.Highlight", NULL),
        "cancellation removes selection overlays");

    CHECK(start_drag(app), "right-click cancellation starts");
    CHECK(mouse(MOUSEEVENTF_RIGHTDOWN) && wait_visible(app, TRUE), "right click restores application");
    mouse(MOUSEEVENTF_RIGHTUP);
    mouse(MOUSEEVENTF_LEFTUP);
    CHECK(wait_enabled(folder, TRUE), "right-click cancellation preserves valid result actions");

    SetForegroundWindow(app);
    pump(80);
    CHECK(key(VK_SPACE, FALSE) && key(VK_SPACE, TRUE) && wait_visible(app, FALSE),
        "keyboard activates crosshair");
    point = center(child);
    SetCursorPos(point.x, point.y);
    CHECK(mouse(MOUSEEVENTF_LEFTDOWN) && release_over(app, point), "keyboard selection completes with click");
    CHECK(wait_enabled(folder, TRUE), "keyboard selection enables folder button");

    if (argc == 3) {
        point = center(folder);
        CHECK(mouse_at(MOUSEEVENTF_LEFTDOWN, point) && mouse_at(MOUSEEVENTF_LEFTUP, point),
            "folder button clicked");
        CHECK(wait_status(app, L"\u5df2\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939"),
            "Windows opens the containing folder successfully");
        CHECK(IsWindowEnabled(folder) && IsWindowEnabled(GetDlgItem(app, 1001)),
            "controls enabled again after opening folder");
    }

    point = center(GetDlgItem(app, 1001));
    SetCursorPos(point.x, point.y);
    SendMessageW(GetDlgItem(app, 1001), WM_LBUTTONDOWN, MK_LBUTTON, 0);
    SetCursorPos(point.x + 20, point.y);
    SendMessageW(GetDlgItem(app, 1001), WM_MOUSEMOVE, MK_LBUTTON, 0);
    CHECK(wait_visible(app, TRUE), "missing release event recovers hidden application");
    CHECK(wait_enabled(folder, TRUE), "interrupted selection restores previous result actions");

    SendMessageW(GetDlgItem(app, 1015), BM_CLICK, 0, 0);
    CHECK((GetWindowLongPtrW(app, GWL_EXSTYLE) & WS_EX_TOPMOST) &&
        SendMessageW(GetDlgItem(app, 1015), BM_GETCHECK, 0, 0) == BST_CHECKED, "pin enables topmost mode");
    SendMessageW(GetDlgItem(app, 1015), BM_CLICK, 0, 0);
    CHECK(!(GetWindowLongPtrW(app, GWL_EXSTYLE) & WS_EX_TOPMOST), "unpin restores normal window ordering");

    ShowWindow(target, SW_HIDE);
    CHECK(GetTempPathW(ARRAYSIZE(temp_root), temp_root), "temporary directory available");
    swprintf(fixture_directory, ARRAYSIZE(fixture_directory), L"%lsWindowFinder \u6d4b\u8bd5 %lu",
        temp_root, GetCurrentProcessId());
    CHECK(CreateDirectoryW(fixture_directory, NULL), "Unicode fixture directory created");
    swprintf(fixture_path, ARRAYSIZE(fixture_path), L"%ls\\test app.exe", fixture_directory);
    CHECK(CopyFileW(self_path, fixture_path, TRUE), "fixture executable copied");
    CHECK(start_process(fixture_path, L"--fixture", &fixture_process), "fixture executable starts");
    fixture = wait_for_window(fixture_process.dwProcessId);
    CHECK(fixture, "fixture window appears");
    SetWindowPos(fixture, HWND_TOPMOST, work.left + 40, work.top + 40, 500, 350, SWP_NOACTIVATE);
    CHECK(start_drag(app), "Unicode path selection starts");
    point = center(fixture);
    CHECK(release_over(app, point) && wait_enabled(folder, TRUE), "Unicode path selection completes");
    CHECK(read_control(app, 1004, actual, ARRAYSIZE(actual)) && _wcsicmp(actual, fixture_path) == 0,
        "Unicode and spaces preserved in executable path");
    SendMessageW(GetDlgItem(app, 1013), BM_CLICK, 0, 0);
    CHECK(read_clipboard(actual, ARRAYSIZE(actual)) && _wcsicmp(actual, fixture_path) == 0,
        "Unicode path copied without conversion");
    pump(200);
    CHECK(screenshot(app, L"build\\smoke.bmp"), "application screenshot captured");
    close_process(&fixture_process, fixture);
    fixture = NULL;
    CHECK(wait_status(app, L"\u76ee\u6807\u7a0b\u5e8f\u5df2\u9000\u51fa\uff0c\u4fdd\u7559\u4e0a\u6b21\u4fe1\u606f"),
        "exited process has a clear status");
    CHECK(IsWindowEnabled(folder), "existing executable can still be located after process exit");
    CHECK(DeleteFileW(fixture_path), "selected executable removed after process exits");
    CHECK(wait_enabled(folder, FALSE), "removed executable disables folder button");
    CHECK(read_control(app, 1004, actual, ARRAYSIZE(actual)) && _wcsicmp(actual, fixture_path) == 0,
        "last captured path remains available for copying");
    CHECK(IsWindowEnabled(GetDlgItem(app, 1013)) && IsWindowEnabled(GetDlgItem(app, 1014)),
        "missing file does not disable copying captured information");
    CHECK(read_control(app, 1006, actual, ARRAYSIZE(actual)) && wcsstr(actual, L"\u6587\u4ef6\u5df2\u4e0d\u5b58\u5728"),
        "missing executable has a clear status");

    SetWindowPos(app, NULL, work.left + 40, work.top + 40, 100, 100, SWP_NOZORDER);
    pump(100);
    CHECK(controls_fit(app), "controls fit at minimum window size");
    CHECK(screenshot(app, L"build\\minimum.bmp"), "minimum-size screenshot captured");
    SetWindowPos(app, NULL, work.left + 60, work.top + 60, 700, 540, SWP_NOZORDER);
    SendMessageW(GetDlgItem(app, 1015), BM_CLICK, 0, 0);
    CHECK(SendMessageW(GetDlgItem(app, 1015), BM_GETCHECK, 0, 0) == BST_CHECKED,
        "pin enabled before saving preferences");
    GetWindowRect(app, &saved_rect);
    close_process(&app_process, app);
    app = NULL;
    CHECK(GetPrivateProfileIntW(L"Window", L"Pinned", 0, settings_file) == 1, "pin preference written to settings file");
    CHECK(start_process(argv[1], L"", &app_process), "application restarts with saved settings");
    app = wait_for_window(app_process.dwProcessId);
    CHECK(app, "restored application appears");
    GetWindowRect(app, &restored_rect);
    CHECK(EqualRect(&saved_rect, &restored_rect), "window position and size survive restart");
    CHECK(SendMessageW(GetDlgItem(app, 1015), BM_GETCHECK, 0, 0) == BST_CHECKED, "pin checkbox survives restart");
    CHECK(GetWindowLongPtrW(app, GWL_EXSTYLE) & WS_EX_TOPMOST, "pin window style survives restart");
    close_process(&app_process, app);
    app = NULL;
    WritePrivateProfileStringW(L"Window", L"X", L"-100000", settings_file);
    WritePrivateProfileStringW(L"Window", L"Y", L"-100000", settings_file);
    CHECK(start_process(argv[1], L"", &app_process), "application starts with off-screen saved coordinates");
    app = wait_for_window(app_process.dwProcessId);
    CHECK(app, "off-screen recovery window appears");
    GetWindowRect(app, &restored_rect);
    CHECK(restored_rect.left >= work.left && restored_rect.top >= work.top &&
        restored_rect.right <= work.right && restored_rect.bottom <= work.bottom,
        "off-screen saved window is moved into the visible work area");
    result = 0;

cleanup:
    if (result && app && read_control(app, 1006, actual, ARRAYSIZE(actual))) {
        char status[2048];
        if (WideCharToMultiByte(CP_UTF8, 0, actual, -1, status, sizeof(status), NULL, NULL))
            fprintf(stderr, "Application status: %s\n", status);
        if (read_control(app, 1002, actual, ARRAYSIZE(actual)))
            fwprintf(stderr, L"Actual HWND: %ls\n", actual);
    }
    key(VK_ESCAPE, TRUE);
    key(VK_SPACE, TRUE);
    mouse(MOUSEEVENTF_LEFTUP);
    mouse(MOUSEEVENTF_RIGHTUP);
    close_process(&fixture_process, fixture);
    close_process(&app_process, app);
    if (target) DestroyWindow(target);
    if (fixture_path[0]) DeleteFileW(fixture_path);
    if (fixture_directory[0]) RemoveDirectoryW(fixture_directory);
    if (settings_file[0]) DeleteFileW(settings_file);
    if (clipboard_changed && saved_clipboard) {
        OleSetClipboard(saved_clipboard);
        OleFlushClipboard();
    }
    if (saved_clipboard) IDataObject_Release(saved_clipboard);
    if (ole_initialized) OleUninitialize();
    SetCursorPos(original_cursor.x, original_cursor.y);
    return result;
}
