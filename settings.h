#ifndef WINDOW_FINDER_SETTINGS_H
#define WINDOW_FINDER_SETTINGS_H
#include <windows.h>

void settings_init(void);
BOOL settings_restore(HWND window, UINT dpi);
void settings_save(HWND window, UINT dpi, BOOL pinned);
void settings_keep_visible(HWND window);

#endif
