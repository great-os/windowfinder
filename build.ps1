param([switch]$Test)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

Push-Location $projectRoot
try {
    if (Get-Command gcc -ErrorAction SilentlyContinue) {
        $gcc = (Get-Command gcc).Source
        $windres = Join-Path (Split-Path $gcc) 'windres.exe'
        & $windres app.rc -O coff -o build/app.o
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
        & $gcc -std=c11 -Os -Wall -Wextra -Werror -municode -mwindows -static -s `
            '-Wl,--dynamicbase,--nxcompat' main.c picker.c settings.c build/app.o -o build/WindowFinder.exe `
            -lcomctl32 -lshell32 -lole32 -lgdi32 -luser32 -luxtheme -luuid
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
        if ($Test) {
            & $gcc -std=c11 -O2 -Wall -Wextra -Werror -municode -static `
                tests/smoke.c -o build/smoke.exe -luser32 -lgdi32 -lole32
            if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        }
    } elseif (Get-Command cl -ErrorAction SilentlyContinue) {
        & rc /nologo /fo build/app.res app.rc
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
        & cl /nologo /TC /W4 /WX /O1 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /Fo:build/ `
            /Fe:build/WindowFinder.exe main.c picker.c settings.c build/app.res `
            /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT `
            comctl32.lib shell32.lib ole32.lib gdi32.lib user32.lib uxtheme.lib uuid.lib
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
        if ($Test) {
            & cl /nologo /TC /W4 /WX /O2 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /Fo:build/smoke.obj `
                /Fe:build/smoke.exe tests/smoke.c /link user32.lib gdi32.lib ole32.lib
            if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        }
    } else {
        throw 'Install MinGW-w64 (gcc/windres), or run from a Visual Studio developer shell.'
    }
    Write-Host "Built: $buildDirectory\WindowFinder.exe"
    if ($Test) {
        & (Join-Path $buildDirectory 'smoke.exe') (Join-Path $buildDirectory 'WindowFinder.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Smoke tests failed.' }
    }
} finally {
    Pop-Location
}
