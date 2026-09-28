#ifndef WINDOW_FINDER_PICKER_H
#define WINDOW_FINDER_PICKER_H

#include <windows.h>

#define WM_PICK_FINISH (WM_APP + 1)
#define WM_PICK_CANCEL (WM_APP + 2)
#define PICK_CANCEL_USER 0
#define PICK_CANCEL_INTERRUPTED 1

DWORD picker_start(HWND owner, HINSTANCE instance, BOOL from_mouse);
BOOL picker_active(void);
HWND picker_finish(void);
void picker_stop(void);

#endif
