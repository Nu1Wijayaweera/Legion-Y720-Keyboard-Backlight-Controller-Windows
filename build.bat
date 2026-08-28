@echo off
rem Legion Y720 Backlight - build script (refactored)
rem Variables grouped at the top for easier maintenance and readability
setlocal ENABLEDELAYEDEXPANSION

rem -- Configuration
set "OUT_DIR=build"
set "SRC_DIR=src"
set "EXE=Y720BacklightGUI.exe"
set "RES_SRC=%SRC_DIR%\Y720BacklightGUI.rc"
set "RES_OBJ=%OUT_DIR%\Y720BacklightGUI.res.o"
set "CONFIG_INI=config\Y720Backlight.ini"
set "OUT_INI=%OUT_DIR%\Y720Backlight.ini"

rem Ensure output directory exists
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

rem -- Tool checks
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

rem Build resources
echo Building GUI resources...
windres "%RES_SRC%" -O coff -o "%RES_OBJ%"
if errorlevel 1 goto fail

rem Build the standalone GUI release
echo Building standalone GUI release...
gcc -Wall -Wextra -O2 -mwindows -o "%OUT_DIR%\%EXE%" ^
    "%SRC_DIR%\Y720BacklightGUI.c" ^
    "%SRC_DIR%\uninstall.c" ^
    "%SRC_DIR%\Y720BacklightCore.c" ^
    "%SRC_DIR%\Y720BacklightHID.c" ^
    "%RES_OBJ%" ^
    -lsetupapi -lhid -ladvapi32 -luser32 -lgdi32 -lshell32
if errorlevel 1 goto fail

rem Copy configuration into release folder if present
if exist "%CONFIG_INI%" copy /Y "%CONFIG_INI%" "%OUT_INI%" >nul

rem Success check
if exist "%OUT_DIR%\%EXE%" (
    echo.
    echo BUILD SUCCESSFUL.
    echo Release executable: %OUT_DIR%\%EXE%
    echo.
    echo The GUI is standalone and does not require Y720Backlight.exe.
    exit /b 0
)

goto fail

:fail
echo.
echo BUILD FAILED.
exit /b 1
