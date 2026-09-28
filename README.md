# WindowFinder

[![Build](https://github.com/great-os/windowfinder/actions/workflows/build.yml/badge.svg)](https://github.com/great-os/windowfinder/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A small native Windows window picker written in C. The interface is in Chinese.
Requires Windows 10 version 1703 or later, or Windows 11. No third-party runtime,
framework, service, installation, or administrator privileges are required.

Drag the crosshair onto a window to inspect its handle, process, and executable
path, then open the executable's folder in Explorer.

## Download

Download `WindowFinder-windows-x64.zip` from the
[latest release](https://github.com/great-os/windowfinder/releases/latest), extract
it, and run `WindowFinder.exe`. Releases include a `SHA256SUMS.txt` checksum file.
The executable is unsigned.

## Build and run

Use MinGW-w64 with `gcc` and `windres` on PATH, or a Visual Studio developer
PowerShell with the C compiler and Windows SDK available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
.\build\WindowFinder.exe
```

The output is a standalone executable using Windows system DLLs. The GCC build
statically links MinGW/libgcc support; UCRT is supplied by Windows. The MSVC build
uses `/MT`. The included build has been verified with MinGW-w64 GCC 15.2.

GitHub Actions builds the x64 MinGW-w64 version on pushes and pull requests.
Workflow artifacts include the executable, this README, the license, and a
checksum. Desktop interaction tests run manually on an unlocked Windows desktop;
they are not executed by CI.

## Use

1. Hold the left mouse button on the crosshair and drag past the system drag
   threshold. The tool hides itself; an ordinary click leaves it visible.
2. The pointer stays a crosshair, and a border follows the target window.
3. Release over another window. The tool returns and shows the hit HWND, top-level
   HWND, window title, program icon/name, process ID, and executable path.
4. Click the folder button to open Explorer and select that executable.

Escape or a right click cancels selection and preserves the previous result.
The crosshair is also keyboard
accessible: activate it with Space, then click a target window. Read-only fields
support selecting and copying text. Copy icons next to both handles and the path
copy individual values; the lower-left button copies all captured information.
The window supports resizing and per-monitor DPI. Icon buttons have tooltips,
native hover feedback, and keyboard focus indicators.

The topmost checkbox is off by default. Position, size, and topmost preference are
saved in `%LOCALAPPDATA%\WindowFinder\settings.ini`. Previously disconnected
monitor positions are moved back into the visible work area. A display-layout
change also brings the current window into view. No captured target information
is saved to disk.

The hit HWND is the result of `WindowFromPoint`, which can identify a child control;
the top-level HWND is displayed separately. Windows hit-testing skips
some hidden, disabled, and transparent controls. The path belongs to the process
owning that HWND, so hosted applications can report their host executable.

Protected processes can deny path access. In that case the HWND and process ID
remain visible, an error is shown, and the folder button stays disabled. The button
is also disabled initially, during selection, or when the executable is no longer
accessible. After cancellation, actions are restored for the previous result.
Captured values remain copyable after a target exits or its executable is removed.
The file is checked again before opening. Action buttons are disabled while
selection details or a folder-opening request are in progress.
Selection interrupted by loss of input access is cancelled and the tool is restored.
Status messages distinguish permission errors, exited processes, missing files,
and a busy clipboard, retaining error numbers where useful.

## Implementation

`main.c` contains the interface, selection snapshot, clipboard, and process queries.
GDI draws the crosshair and copy symbols; native themes render the buttons.
`picker.c` owns the temporary input layer, transparent highlight, and input hooks.
The input layer owns the crosshair cursor without changing global system cursors;
hit testing briefly makes that layer transparent to locate the underlying window.
Mouse and Escape hooks exist only during selection and pass work to the message
loop. The target is resolved before restoring the tool. `settings.c` manages the
per-user INI file and validates restored geometry against the available monitors.
`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` and `QueryFullProcessImageNameW`
retrieve the executable path, with a 32,768-character Unicode buffer.
`SHOpenFolderAndSelectItems` opens the location without building a shell command.

`app.rc` embeds a manifest for common controls, DPI awareness, and long paths.

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test
```

The integration test requires an unlocked interactive desktop and temporarily
moves the pointer. It restores the pointer and closes its own windows on exit.
It uses an isolated settings file and retains/restores the clipboard using OLE.
It covers target highlighting, cursor state, drag thresholds, cancellation,
copying, program metadata, file/process lifetime, layout, and persisted preferences.
Screenshots are written to `build/smoke.bmp`, `build/highlight.bmp`, and
`build/minimum.bmp`. The test-only `WINDOWFINDER_SETTINGS_FILE` environment override
selects the isolated INI file.

To also test the Explorer action, run:

```powershell
build\smoke.exe build\WindowFinder.exe --open-folder
```

This opens the build directory in Explorer.

## Contributing

Bug reports and pull requests are welcome in
[great-os/windowfinder](https://github.com/great-os/windowfinder). For bugs, include
the Windows version, display scaling, reproduction steps, and any error code.
Keep changes in C and use Windows system APIs to preserve the small dependency
footprint. Build with warnings treated as errors and run the desktop tests when
changing selection, clipboard, layout, or settings behavior.

## License

[MIT](LICENSE), copyright (c) 2026 greatbody.
