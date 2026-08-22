@echo off
setlocal

if not exist build mkdir build

where gcc >nul 2>&1
if errorlevel 1 (
    echo ERROR: gcc was not found in PATH.
    exit /b 1
)

where windres >nul 2>&1
if errorlevel 1 (
    echo ERROR: windres was not found in PATH.
    exit /b 1
)

echo Building GUI resources...
windres src\Y720BacklightGUI.rc -O coff -o build\Y720BacklightGUI.res.o
if errorlevel 1 goto fail

echo Building standalone GUI release...
gcc -Wall -Wextra -O2 -mwindows -o build\Y720BacklightGUI.exe ^
    src\Y720BacklightGUI.c ^
    src\Y720BacklightCore.c ^
    src\Y720BacklightHID.c ^
    build\Y720BacklightGUI.res.o ^
    -lsetupapi -lhid -ladvapi32 -luser32 -lgdi32 -lshell32
if errorlevel 1 goto fail

if exist config\Y720Backlight.ini copy /Y config\Y720Backlight.ini build\Y720Backlight.ini >nul

if exist build\Y720BacklightGUI.exe (
    echo.
    echo BUILD SUCCESSFUL.
    echo Release executable: build\Y720BacklightGUI.exe
    echo.
    echo The GUI is standalone and does not require Y720Backlight.exe.
    exit /b 0
)

goto fail

:fail
echo.
echo BUILD FAILED.
exit /b 1
