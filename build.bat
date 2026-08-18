@echo off
setlocal

echo ============================================
echo   Legion Y720 Keyboard Backlight - Build
echo ============================================
echo.

if not exist src\Y720Backlight.c (
    echo ERROR: src\Y720Backlight.c was not found.
    echo.
    pause
    exit /b 1
)

if not exist src\Y720BacklightGUI.c (
    echo ERROR: src\Y720BacklightGUI.c was not found.
    echo.
    pause
    exit /b 1
)

if not exist src\Y720BacklightGUI.rc (
    echo ERROR: src\Y720BacklightGUI.rc was not found.
    echo.
    pause
    exit /b 1
)

if not exist resources\keyboard.ico (
    echo ERROR: resources\keyboard.ico was not found.
    echo.
    echo Please place the keyboard icon here:
    echo.
    echo   resources\keyboard.ico
    echo.
    pause
    exit /b 1
)

if not exist config\Y720Backlight.ini (
    echo ERROR: config\Y720Backlight.ini was not found.
    echo.
    echo Please make sure the configuration file exists here:
    echo.
    echo   config\Y720Backlight.ini
    echo.
    pause
    exit /b 1
)

if not exist build (
    mkdir build
)

echo.
echo ============================================
echo   Building command-line controller
echo ============================================
echo.

gcc -std=c11 -O2 -Wall ^
    src\Y720Backlight.c ^
    -o build\Y720Backlight.exe ^
    -lsetupapi ^
    -lhid

if errorlevel 1 (
    echo.
    echo ============================================
    echo BUILD FAILED - COMMAND LINE CONTROLLER
    echo ============================================
    pause
    exit /b 1
)

echo.
echo Command-line controller built successfully.
echo.

echo ============================================
echo   Building GUI resources
echo ============================================
echo.

windres ^
    src\Y720BacklightGUI.rc ^
    -O coff ^
    -o build\Y720BacklightGUI-res.o

if errorlevel 1 (
    echo.
    echo ============================================
    echo BUILD FAILED - GUI RESOURCES
    echo ============================================
    pause
    exit /b 1
)

echo.
echo GUI resources built successfully.
echo.

echo ============================================
echo   Building Windows GUI
echo ============================================
echo.

gcc -std=c11 -O2 -Wall ^
    src\Y720BacklightGUI.c ^
    build\Y720BacklightGUI-res.o ^
    -o build\Y720BacklightGUI.exe ^
    -mwindows ^
    -luser32 ^
    -lgdi32 ^
    -lshell32

if errorlevel 1 (
    echo.
    echo ============================================
    echo BUILD FAILED - GUI
    echo ============================================
    pause
    exit /b 1
)

echo.
echo GUI built successfully.
echo.

echo ============================================
echo   Copying configuration
echo ============================================
echo.

copy /Y config\Y720Backlight.ini build\Y720Backlight.ini >nul

if errorlevel 1 (
    echo.
    echo ============================================
    echo BUILD FAILED - CONFIGURATION COPY
    echo ============================================
    pause
    exit /b 1
)

echo.
echo Configuration copied successfully.
echo.

echo ============================================
echo BUILD SUCCESSFUL
echo ============================================
echo.
echo Created:
echo.
echo   build\Y720Backlight.exe
echo   build\Y720BacklightGUI.exe
echo   build\Y720Backlight.ini
echo.
echo The GUI executable contains:
echo.
echo   - Keyboard application icon
echo   - Keyboard title-bar icon
echo   - Keyboard tray icon
echo   - Tray menu
echo   - Four-zone controller
echo   - Profiles
echo.
pause