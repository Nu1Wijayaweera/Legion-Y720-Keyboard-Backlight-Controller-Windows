# Legion Y720 Keyboard Backlight Controller for Windows

A lightweight native Windows controller for the RGB keyboard backlight of the Lenovo Legion Y720.

The project provides both:

- A command-line controller
- A native Windows GUI with system-tray integration
- Independent control of all four keyboard lighting zones
- Configurable profiles

It communicates directly with the keyboard's HID interface and does not require Lenovo's proprietary lighting software.

---

# Features

- Direct Windows HID communication
- RGB keyboard backlight control
- Four-zone independent lighting control
- Color selection
- Brightness control
- Multiple lighting modes
- Profiles stored in an INI configuration file
- Native Windows GUI
- System-tray controller
- Tray controls for turning lighting on/off
- Click the tray icon to restore the GUI
- Command-line interface
- No installer required
- No background service required
- No Lenovo software required

---

# No Lenovo software required

This project does **not** require:

- Lenovo Vantage
- Lenovo Nerve Center
- Lenovo Utility
- Lenovo Legion software
- A background service

The controller communicates directly with the keyboard through the Windows HID API.

---

# Compatibility

The controller was developed and tested with:

- Lenovo Legion Y720
- Windows 10 64-bit

Known HID device:

    Vendor ID:       048D
    Product ID:      837A
    Version:         0009

Keyboard lighting HID collection:

    Usage Page:      FF89
    Usage:           00CC

The lighting collection exposes a 7-byte feature report.

Other Lenovo laptops may use different hardware or HID protocols and are not guaranteed to work.

If you have another Lenovo model and want to investigate compatibility, contributions and testing reports are welcome.

---

# Download

Precompiled Windows binaries are available from the GitHub Releases section.

The release package contains the required executables and configuration file.

No installer is required.

Extract the release ZIP and run:

    Y720BacklightGUI.exe

The command-line controller is:

    Y720Backlight.exe

Keep `Y720Backlight.exe` and `Y720BacklightGUI.exe` together in the same directory.

The GUI launches the command-line controller automatically when applying lighting settings.

---

# Windows GUI

The GUI provides a native Windows interface for controlling the keyboard.

It includes:

- Global lighting controls
- Four independent lighting zones
- Profiles
- Status information
- System-tray integration

## Global Lighting

The global controls allow you to select:

- Color
- Brightness
- Mode

and apply the settings to all four zones.

Example:

    Crimson
    High
    Always On

---

# Four-Zone Control

The keyboard is divided into four controllable lighting zones:

    Zone 0: Caps Lock -> D
    Zone 1: F -> K
    Zone 2: L -> Enter
    Zone 3: Numeric Keypad

Each zone has its own:

- Color
- Brightness
- Mode

The GUI provides an `Apply` button for each zone.

This allows different colors and effects to be used across different areas of the keyboard.

---

# System Tray

The GUI can remain available in the Windows system tray.

When the GUI is minimized or closed, it is hidden rather than terminated.

Clicking the tray keyboard icon restores the controller window.

Right-clicking the tray icon provides controls for:

- Turn Lighting Off
- Turn Lighting On
- Exit

If Windows Explorer restarts, the application automatically attempts to recreate the tray icon.

---

# Command-Line Controller

The command-line executable can be used independently of the GUI.

Open Command Prompt in the directory containing `Y720Backlight.exe`.

## Show help

    Y720Backlight.exe help

---

# Set Color, Brightness and Mode

The recommended command is:

    Y720Backlight.exe set COLOR BRIGHTNESS MODE

Examples:

    Y720Backlight.exe set crimson high always_on

    Y720Backlight.exe set blue low breath

    Y720Backlight.exe set green medium smooth

    Y720Backlight.exe set white ultra wave

    Y720Backlight.exe set crimson high heartbeat

This sends the color, brightness and mode together.

Using the complete `set` command is recommended because the keyboard firmware may otherwise restore or default another property when individual settings are changed.

---

# Colors

Supported firmware color values:

    crimson
    torch_red
    hollywood_cerise
    magenta
    electric_violet
    electric_violet_2
    blue
    blue_ribbon
    azure_radiance
    cyan
    spring_green
    spring_green_2
    green
    bright_green
    lime
    yellow
    web_orange
    international_orange
    white
    nocolor

Example:

    Y720Backlight.exe set crimson high always_on

---

# Brightness

Supported firmware brightness values:

    off
    low
    medium
    high
    ultra
    enough

Example:

    Y720Backlight.exe set blue low always_on

## Brightness observations

On the tested Y720:

- `off` works.
- `low` works.
- `medium` works.
- `high` works, but is visually very similar to `medium`.
- `ultra` works.
- `enough` produced no obvious additional effect.

These are values accepted by the keyboard firmware. Their visible effect may vary between hardware revisions.

---

# Lighting Modes

Supported modes:

    heartbeat
    breath
    smooth
    always_on
    wave

Examples:

    Y720Backlight.exe set crimson high heartbeat

    Y720Backlight.exe set crimson high breath

    Y720Backlight.exe set crimson high smooth

    Y720Backlight.exe set crimson high always_on

    Y720Backlight.exe set crimson high wave

---

# Turn the Keyboard Off

    Y720Backlight.exe off

---

# Profiles

Profiles are stored in:

    Y720Backlight.ini

Profiles contain:

- Color
- Brightness
- Mode

Example:

    [gaming]
    color=crimson
    brightness=high
    mode=wave

The GUI automatically reads the available profiles from the INI file.

Profiles can also be used through the command-line controller.

    Y720Backlight.exe profile gaming

You can also use:

    Y720Backlight.exe gaming

---

# Example Configuration

    [normal]
    color=white
    brightness=high
    mode=always_on

    [gaming]
    color=crimson
    brightness=high
    mode=wave

    [night]
    color=blue
    brightness=low
    mode=always_on

    [breathing_red]
    color=crimson
    brightness=high
    mode=breath

---

# List Profiles

    Y720Backlight.exe profiles

---

# HID Protocol

The original Linux implementation sends a feature report of the form:

    CC 00 mode color brightness zone

The Windows implementation uses the native Windows HID API to send the equivalent feature report to the Y720 lighting HID collection.

The tested lighting collection is:

    VID:        048D
    PID:        837A
    Usage Page: FF89
    Usage:      00CC

The controller supports the keyboard's four lighting zones.

---

# Building From Source

## Requirements

A MinGW GCC environment is required.

MSYS2 can be used to provide GCC and the required Windows development tools.

No Visual Studio installation is required.

The build also requires:

- GCC
- `windres`
- Windows system libraries provided by MinGW

---

# Build

Run:

    build.bat

The build script creates:

    build\Y720Backlight.exe
    build\Y720BacklightGUI.exe

It also builds the GUI resource object and copies the configuration file into the build directory.

The resulting `build` directory is suitable for local testing.

---

# Source Structure

The project is organized approximately as follows:

    src\
        Y720Backlight.c
        Y720BacklightGUI.c
        Y720BacklightGUI.rc

    resources\
        keyboard.ico

    config\
        Y720Backlight.ini

    build\
        (generated during build)

The command-line controller contains the HID communication and command-line interface.

The GUI provides the Windows interface and launches the command-line controller when applying settings.

---

# Troubleshooting

## "Y720 lighting device not found"

Check that:

1. The keyboard is connected and enabled.
2. The machine is a Y720 or compatible device.
3. The HID device has VID `048D` and PID `837A`.
4. You are using the correct executable.

The program does not depend on Lenovo's applications.

---

## The command runs but the keyboard does not change

The HID protocol is hardware-specific.

Check whether your keyboard exposes:

    Usage Page FF89
    Usage 00CC

Different Y720 revisions or other Lenovo laptops may use different controllers.

If you discover compatibility with another machine, please consider opening a GitHub issue and reporting the hardware information.

---

## A mode changes my color

Use:

    Y720Backlight.exe set COLOR BRIGHTNESS MODE

For example:

    Y720Backlight.exe set crimson high wave

Using all three values together avoids unwanted firmware defaults.

---

## Profiles do not appear in the GUI

Make sure:

    Y720Backlight.ini

is in the same release/build directory as the executables.

For a source build, run `build.bat` so that the configuration file is copied into the build directory.

---

# Project Status

The project currently provides a working Windows controller for the tested Lenovo Legion Y720 hardware.

Current functionality includes:

- Direct Windows HID communication
- Y720 lighting detection
- RGB color control
- Brightness control
- Lighting modes
- Four-zone control
- Independent zone settings
- Profiles
- Command-line interface
- Native Windows GUI
- System-tray integration
- No Lenovo software required

The project is considered usable, but compatibility with additional Y720 hardware revisions and other Lenovo models has not been fully investigated.

---

# Contributing

Contributions are welcome.

If you have:

- Another Lenovo laptop that appears to use the same lighting hardware
- A different Y720 hardware revision
- HID protocol information
- Bug fixes
- UI improvements
- New features
- Documentation improvements

please consider opening an issue or submitting a pull request.

You do not need to be the original author to improve the project.

---

# Credits

This project is based in part on the work of:

`threadexio/Legion-Y720-Keyboard-Backlight`

Original project:

Copyright (c) 2021 1337

The original project is licensed under the MIT License.

See:

    THIRD-PARTY-NOTICES.md

for the original license notice.

---

# License

This Windows implementation is released under the MIT License.

See `LICENSE` for details.

The original project's copyright and license are preserved in:

    THIRD-PARTY-NOTICES.md
