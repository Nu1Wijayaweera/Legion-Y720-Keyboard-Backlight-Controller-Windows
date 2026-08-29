#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <winreg.h>
#include <stdio.h>
#include <string.h>
#include "Y720BacklightCore.h"
#include "Y720BacklightHID.h"

/*
 * Legion Y720 Keyboard Backlight Controller - GUI
 *
 * This file implements the Windows GUI for configuring the keyboard backlight
 * on Lenovo Legion Y720 laptops. It intentionally keeps all GUI, state and
 * persistence logic in a single C file for readability and easy study.
 *
 * Organization and learning notes for newcomers / students:
 * - The file is organized into clearly labeled sections (see the table of contents
 *   below). Function declarations (forward prototypes) are grouped together so
 *   it's easy to find the public helpers each section exposes to others.
 * - Windows GUI basics used here:
 *   - Controls are HWND handles (buttons, comboboxes, etc.) stored in globals.
 *   - Select/modify control contents with SendMessageA, and create controls
 *     using CreateWindowExA. Owner-drawn combos are supported for visual swatches.
 * - State persistence uses a simple INI file in %APPDATA%/LegionY720Backlight/state.ini
 *   via GetPrivateProfileStringA / WritePrivateProfileStringA. This is intentionally
 *   simple and easy to inspect.
 * - Device interactions are done via the tiny shared core module (Y720BacklightCore.c)
 *   which encapsulates HID discovery and feature reports. The GUI calls into
 *   that core to apply lighting changes.
 *
 * Table of Contents (sections in this file)
 * 1) Includes & constants
 * 2) Global state and UI control handles
 * 3) Forward declarations (helpers grouped by purpose)
 * 4) Initialization helpers (DPI, fonts, layout)
 * 5) UI creation and owner-draw helpers
 * 6) State model & persistence helpers (ui_state_*, saved_state_*)
 * 7) High-level apply functions that talk to the core (apply_all_direct, ...)
 * 8) Event handlers (button actions, Fn+Space handler)
 * 9) Dialogs and profile management
 *10) Main window and message loop
 *11) Uninstall and shutdown helpers
 *12) Misc utility helpers
 *
 * Keep each section compact and well-commented. When extending, add functions to
 * the appropriate section and update the TOC above.
 */

#define APP_TITLE "Legion Y720 Keyboard Backlight Controller"
#define CLASS_NAME "Y720BacklightGUIClass"
#define ZONE_COUNT 4

#define IDC_GLOBAL_COLOR 1001
#define IDC_GLOBAL_BRIGHTNESS 1002
#define IDC_GLOBAL_MODE 1003
#define IDC_APPLY_ALL 1004
#define IDC_APPLY_MODE 1005
#define IDC_OFF 1006
#define IDC_SMOOTH 1007
#define IDC_PROFILE 1008
#define IDC_APPLY_PROFILE 1009
#define IDC_ZONE_BASE 2000
#define IDC_STATUS 3000
#define IDC_STARTUP 3001
#define IDC_UNINSTALL 3002
#define IDC_PROFILE_NEW 3010
#define IDC_PROFILE_DELETE 3011
#define IDC_PROFILE_DIALOG 3100
#define IDC_PROFILE_NAME 3101
#define IDC_PROFILE_COLOR 3102
#define IDC_PROFILE_BRIGHTNESS 3103
#define IDC_PROFILE_MODE 3104
#define IDC_PROFILE_SAVE 3105
#define IDC_PROFILE_CANCEL 3106

#define WM_SHOW_EXISTING (WM_APP + 2)

#define WM_TRAYICON (WM_APP + 1)
#define TRAY_ICON_ID 5001
#define IDI_Y720_KEYBOARD 101
#define ID_TRAY_OFF 5003
#define ID_TRAY_ON 5004
#define ID_TRAY_EXIT 5005

/* === GLOBAL STATE & UI CONTROL HANDLES ===
 *
 * These globals represent the GUI's in-process state and the HWNDs for the
 * controls created at runtime. They are intentionally centralized here so
 * event handlers and helpers across the file can easily find shared state.
 *
 * Teaching notes:
 * - HWND is an opaque window/control handle used by the Win32 API. Use
 *   SendMessageA to interact with controls and CreateWindowExA to create them.
 * - Prefer passing locals where practical; globals are used here to keep the
 *   example compact and easy to follow.
 * ---------------------------------------------------------------------- */
static HWND g_hWnd;
static HWND g_title;
static HWND g_globalColor, g_globalBrightness, g_globalMode;
static HWND g_profile, g_status, g_startup;
static HWND g_lightingToggle;

/* Profiles stored in memory for the profile combobox */
#define MAX_PROFILES 64
static char g_profile_values[MAX_PROFILES][128];
static int g_profile_count = 0;

/* Per-zone controls */
static HWND g_zoneColor[ZONE_COUNT], g_zoneBrightness[ZONE_COUNT];

/* Shared UI resources */
static HFONT g_font, g_fontBold;
static NOTIFYICONDATAA g_trayIcon;
static int g_trayAdded = 0, g_exiting = 0;
static UINT g_taskbarCreated = 0;
static HICON g_appIcon = NULL;
static HBRUSH g_bgBrush = NULL;
static HBRUSH g_controlBrush = NULL;
static HANDLE g_single_instance_mutex = NULL;

/* Layout / runtime state */
static int g_dpi = 96; /* DPI used for scaling */
static int g_lighting_on = 1; /* cached lighting enabled flag */
static char g_config_path[MAX_PATH];
static int g_config_path_initialized = 0;

/* Runtime copy of the last lighting state before an explicit Off command.
   This lets tray On restore the exact four-zone state without changing the
   persistent Off state or complicating the normal lighting path. */
static int g_last_on_valid = 0;

/* Legion-style dark charcoal/red accent palette. */
#define CLR_BG RGB(18, 18, 20)
#define CLR_PANEL RGB(27, 27, 30)
#define CLR_CONTROL RGB(35, 35, 39)
#define CLR_TEXT RGB(235, 235, 238)
#define CLR_MUTED RGB(170, 170, 176)
#define CLR_RED RGB(220, 30, 45)
#define CLR_RED_DARK RGB(145, 20, 30)
#define CLR_BUTTON RGB(38, 38, 42)
#define CLR_BUTTON_EDGE RGB(70, 70, 75)

/* Layout constants for create_controls() */
#define LAYOUT_MARGIN_X 25
#define LAYOUT_MARGIN_Y 18
#define LAYOUT_CONTROL_HEIGHT 24
#define LAYOUT_CONTROL_WIDTH 60
#define LAYOUT_COMBO_WIDTH 130
#define LAYOUT_COMBO_WIDTH_LONG 210
#define LAYOUT_BUTTON_HEIGHT 30
#define LAYOUT_BUTTON_HEIGHT_MED 34
#define LAYOUT_BUTTON_WIDTH 130
#define LAYOUT_BUTTON_WIDTH_LONG 180
#define LAYOUT_BUTTON_WIDTH_XLONG 235
#define LAYOUT_BUTTON_WIDTH_MAX 300
#define LAYOUT_GROUP_HEIGHT 145
#define LAYOUT_GROUP_HEIGHT_SMALL 75
#define LAYOUT_GROUP_HEIGHT_MED 115
#define LAYOUT_GROUP_HEIGHT_LARGE 235
#define LAYOUT_GROUP_WIDTH 490
#define LAYOUT_GROUP_WIDTH_FULL 800
#define LAYOUT_WINDOW_WIDTH 860
#define LAYOUT_WINDOW_HEIGHT 770
#define LAYOUT_DPI_MIN 96
#define LAYOUT_DPI_MAX 480
#define LAYOUT_FOCUS_INSET 3

static void update_lighting_toggle_button(int lighting_on)
{
    g_lighting_on = lighting_on ? 1 : 0;
    if (g_lightingToggle)
        SetWindowTextA(g_lightingToggle, g_lighting_on ? "Turn Lighting Off" : "Turn Lighting On");
}

#define SCALE(value) MulDiv((value), g_dpi, 96)

/* === FORWARD DECLARATIONS (PROTOTYPES) ===
 *
 * The file groups forward declarations by logical responsibility so readers can
 * quickly see what helpers are available without chasing implementation order.
 * Each block below corresponds to an implementation section later in the file.
 * ---------------------------------------------------------------------- */

/* UI creation helpers */
static HWND create_label(HWND parent, const char *text, int x, int y, int width, int height);
static HWND create_combo(HWND parent, int id, int x, int y, int width, int height);
static HWND create_button(HWND parent, const char *text, int id, int x, int y, int width, int height);
static void load_profiles(void);

/* === OWNER-DRAWN COLOR COMBO HELPERS ===
 * Owner-drawn color combobox creation and helper functions. These controls
 * display a small colour swatch next to a friendly label and require custom
 * drawing via WM_DRAWITEM. */
static HWND create_color_combo(HWND parent, int id, int x, int y, int width, int height);
static int is_color_combo_hwnd(HWND hwnd);
static void draw_color_combobox(const DRAWITEMSTRUCT *item);

/* === OWNER-DRAWN BRIGHTNESS COMBO HELPERS ===
 * Owner-drawn brightness combobox creation and draw helpers. Render a compact
 * visual indicator (filled/unfilled dots) and a readable label for each item. */
static HWND create_brightness_combo(HWND parent, int id, int x, int y, int width, int height);
static int is_brightness_combo_hwnd(HWND hwnd);
static void draw_brightness_combobox(const DRAWITEMSTRUCT *item);

/* DPI and layout helpers */
static void init_dpi_scaling(void);
static void center_window_on_active_monitor(HWND hwnd);

/* State and persistence helpers */
static int get_state_path(char *buffer, DWORD buffer_size);
static void save_state_values(int enabled, const char *mode,
                              const char *global_color, const char *global_brightness,
                              const char zone_color[ZONE_COUNT][64],
                              const char zone_brightness[ZONE_COUNT][32]);
static void save_last_on_state(void);
static void load_last_on_state(void);
static void save_current_state(int enabled, const char *mode_override);

/* Forward declaration for in-file state module (defined near the bottom). */
/* Forward-declare the struct tag so prototypes can reference it without
 * creating a conflicting typedef. The full typedef is defined later in the
 * state module section. */
struct ui_state_t;
static int ui_state_read(struct ui_state_t *s);
static void ui_state_apply_to_controls(const struct ui_state_t *s);
static void ui_state_save(const struct ui_state_t *s, int enabled);

static void load_state_into_controls(void);
static int restore_saved_state(void);

/* Device apply helpers (call into the core) */
static int apply_all_direct(const char *color, const char *bright, const char *mode);
static int apply_zone_direct(int zone, const char *color, const char *bright, const char *mode);
static int turn_off_direct(void);

/* Fn+Space / Raw input callbacks */
static void handle_fn_space(void *context);

/* Misc helpers used by perform_uninstall, drawing, etc. */
static void remember_current_on_state(void);
static void turn_off(void);
static void exit_application(HWND hwnd);
static void draw_button(const DRAWITEMSTRUCT *item);

/* === DPI & LAYOUT HELPERS ===
 * Helpers that initialize DPI scaling and perform window centering relative to
 * the active monitor. Keep platform compatibility code localized here.
 */
static void init_dpi_scaling(void)
{
    HMODULE user32;
    FARPROC set_process_dpi_aware_proc;
    BOOL(WINAPI * set_process_dpi_aware)(void) = NULL;
    HDC dc = NULL;

    user32 = GetModuleHandleA("user32.dll");
    set_process_dpi_aware_proc = user32 ? GetProcAddress(user32, "SetProcessDPIAware") : NULL;
    if (set_process_dpi_aware_proc)
    {
        /*
         * GetProcAddress returns FARPROC. Copy the address into the correctly
         * typed function pointer without an incompatible-function-pointer cast.
         */
        memcpy(&set_process_dpi_aware,
               &set_process_dpi_aware_proc,
               sizeof(set_process_dpi_aware));
        if (set_process_dpi_aware)
            set_process_dpi_aware();
    }

    dc = GetDC(NULL);
    if (dc)
    {
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        /* Validate DPI range - typical values are 96-480 */
        if (dpi >= LAYOUT_DPI_MIN && dpi <= LAYOUT_DPI_MAX)
            g_dpi = dpi;
        ReleaseDC(NULL, dc);
    }
}

static void center_window_on_active_monitor(HWND hwnd)
{
    POINT point;
    HMONITOR monitor;
    MONITORINFO info;
    RECT rect;
    int width, height, x, y;

    if (!hwnd)
        return;
    if (!GetCursorPos(&point))
        return;
    monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(monitor, &info))
        return;
    if (!GetWindowRect(hwnd, &rect))
        return;

    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
static char g_last_on_mode[64];
static char g_last_on_global_color[64];
static char g_last_on_global_brightness[32];
static char g_last_on_zone_color[ZONE_COUNT][64];
static char g_last_on_zone_brightness[ZONE_COUNT][32];

/* -------------------------------------------------------------------------
 * Section: Colour and brightness definitions
 *
 * The arrays below define the canonical colour identifiers (used by the core),
 * human-friendly labels for the UI, and conservative RGB swatches used for
 * owner-drawn comboboxes. Keep the arrays in sync: colors[] length ==
 * color_labels[] length == color_map[] length.
 * ---------------------------------------------------------------------- */
static const char *colors[] = {
    "azure_radiance", "blue", "blue_ribbon", "bright_green",
    "crimson", "cyan", "electric_violet", "electric_violet_2",
    "green", "hollywood_cerise", "international_orange", "lime",
    "magenta", "nocolor", "spring_green", "spring_green_2",
    "torch_red", "web_orange", "white", "yellow"};

static const char *color_labels[] = {
    "Azure Radiance", "Blue", "Blue Ribbon", "Bright Green",
    "Crimson", "Cyan", "Electric Violet", "Electric Violet 2",
    "Green", "Hollywood Cerise", "International Orange", "Lime",
    "Magenta", "No Color", "Spring Green", "Spring Green 2",
    "Torch Red", "Web Orange", "White", "Yellow"};

/* Conservative approximate RGB swatches aligned with the order in colors[].
   These are visual hints only and are not authoritative OEM values. Keep the
   array length equal to the colours[] array above. */
static const COLORREF color_map[] = {
    /* Updated conservative mapping based on the list you provided, with a
       slightly more bluish tint for the electric violet entries as requested. */
    RGB(0, 127, 255),   /* azure_radiance  #007FFF */
    RGB(0, 0, 255),     /* blue            #0000FF */
    RGB(0, 102, 255),   /* blue_ribbon     #0066FF */
    RGB(0, 255, 0),     /* bright_green    #00FF00 */
    RGB(220, 20, 60),   /* crimson         #DC143C */
    RGB(0, 255, 255),   /* cyan            #00FFFF */
    RGB(100, 0, 255),   /* electric_violet (tuned slightly bluish, approx) */
    RGB(120, 0, 255),   /* electric_violet_2 (tuned slightly bluish, approx) */
    RGB(0, 255, 0),     /* green           #00FF00 */
    RGB(244, 0, 161),   /* hollywood_cerise#F400A1 */
    RGB(255, 79, 0),    /* international_orange #FF4F00 */
    RGB(190, 255, 0),   /* lime (adjusted back to previous conservative mapping) */
    RGB(255, 0, 255),   /* magenta         #FF00FF */
    RGB(40, 40, 40),    /* nocolor (render as dark/empty) */
    RGB(0, 255, 127),   /* spring_green    #00FF7F */
    RGB(0, 238, 118),   /* spring_green_2  #00EE76 */
    RGB(253, 14, 53),   /* torch_red       #FD0E35 */
    RGB(255, 165, 0),   /* web_orange      #FFA500 */
    RGB(255, 255, 255), /* white           #FFFFFF */
    RGB(240, 255, 0)    /* yellow (less orangish) */
};

static const char *brightness[] = {"off", "low", "medium", "high", "ultra", "enough"};
static const char *brightness_labels[] = {"Off", "Low", "Medium", "High", "Ultra", "Enough"};

static const char *modes[] = {"always_on", "breath", "heartbeat", "smooth", "wave"};
static const char *mode_labels[] = {"Always On", "Breath", "Heartbeat", "Smooth", "Wave"};
static const char *friendly_value(const char *value,
                                  const char **values, const char **labels, int count)
{
    int i;
    if (!value)
        return "";
    for (i = 0; i < count; ++i)
        if (_stricmp(values[i], value) == 0)
            return labels[i];
    return value;
}

static const char *friendly_color(const char *value)
{
    return friendly_value(value, colors, color_labels,
                          (int)(sizeof(colors) / sizeof(colors[0])));
}

static const char *friendly_brightness(const char *value)
{
    return friendly_value(value, brightness, brightness_labels,
                          (int)(sizeof(brightness) / sizeof(brightness[0])));
}

static const char *friendly_mode(const char *value)
{
    return friendly_value(value, modes, mode_labels,
                          (int)(sizeof(modes) / sizeof(modes[0])));
}

static void make_profile_label(const char *value, char *buffer, int buffer_size)
{
    int i, j = 0;
    int capitalize = 1;

    if (!buffer || buffer_size <= 0)
        return;
    buffer[0] = '\0';
    if (!value)
        return;

    for (i = 0; value[i] && j < buffer_size - 1; ++i)
    {
        char c = value[i];
        if (c == '_' || c == '-')
        {
            if (j > 0 && buffer[j - 1] != ' ')
                buffer[j++] = ' ';
            capitalize = 1;
        }
        else
        {
            if (capitalize && c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            buffer[j++] = c;
            capitalize = 0;
        }
    }
    while (j > 0 && buffer[j - 1] == ' ')
        --j;
    buffer[j] = '\0';
}

static const char *zone_names[] = {
    "Caps Lock -> D", "F -> K", "L -> Enter", "Numeric Keypad"};

static void set_status(const char *text)
{
    if (g_status)
        SetWindowTextA(g_status, text);
}

static void add_combo_items(HWND combo, const char **labels, int count)
{
    int i;
    if (!combo)
        return;
    for (i = 0; i < count; ++i)
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)labels[i]);
}

static int select_combo_value(HWND combo, const char *value,
                              const char **values, int count)
{
    int i;
    if (!combo || !value)
        return 0;
    for (i = 0; i < count; ++i)
    {
        if (_stricmp(values[i], value) == 0)
        {
            SendMessageA(combo, CB_SETCURSEL, i, 0);
            return 1;
        }
    }
    return 0;
}

static int get_combo_value(HWND combo, const char **values, int count,
                           char *buffer, int buffer_size)
{
    int index;
    if (!buffer || buffer_size <= 0)
        return 0;
    buffer[0] = '\0';
    if (!combo)
        return 0;
    index = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= count)
        return 0;
    snprintf(buffer, buffer_size, "%s", values[index]);
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static void trim(char *s)
{
    char *start, *end;
    if (!s)
        return;
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        --end;
    *end = '\0';
}

static int is_valid_profile_name(const char *name)
{
    int i, len;
    if (!name)
        return 0;

    len = (int)strlen(name);
    if (len == 0 || len > 64)
        return 0;

    /* Check for leading/trailing spaces */
    if (name[0] == ' ' || name[len - 1] == ' ')
        return 0;

    /* Check for dangerous characters */
    for (i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127)
            return 0; /* Control characters */
        if (c == '[' || c == ']' || c == '=' || c == '\\' || c == '/' ||
            c == '"' || c == ';' || c == ':' || c == '*' || c == '?' ||
            c == '<' || c == '>' || c == '|')
            return 0;
    }

    return 1;
}

static int safe_atoi(const char *str, int min_val, int max_val, int *result)
{
    char *endptr;
    long val;

    if (!str || !result)
        return 0;

    val = strtol(str, &endptr, 10);
    if (endptr == str || *endptr != '\0')
        return 0; /* Not a valid number */
    if (val < min_val || val > max_val)
        return 0; /* Out of range */

    *result = (int)val;
    return 1;
}

static int get_exe_directory(char *buffer, DWORD buffer_size)
{
    DWORD length;
    char *slash;
    if (!buffer || !buffer_size)
        return 0;
    length = GetModuleFileNameA(NULL, buffer, buffer_size);
    if (!length || length >= buffer_size)
        return 0;
    slash = strrchr(buffer, '\\');
    if (!slash)
        slash = strrchr(buffer, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    return 1;
}

static int build_path(char *buffer, size_t buffer_size, const char *base, const char *suffix)
{
    int written;

    if (!buffer || buffer_size == 0 || !base || !suffix)
        return 0;

    written = snprintf(buffer, buffer_size, "%s\\%s", base, suffix);
    if (written < 0 || (size_t)written >= buffer_size)
    {
        buffer[0] = '\0';
        return 0;
    }

    return 1;
}

static int get_config_path(char *buffer, DWORD buffer_size)
{
    char directory[MAX_PATH], candidate[MAX_PATH];
    HANDLE file;

    if (!buffer || buffer_size == 0)
        return 0;
    if (g_config_path_initialized)
    {
        snprintf(buffer, buffer_size, "%s", g_config_path);
        buffer[buffer_size - 1] = '\0';
        return g_config_path[0] != '\0';
    }

    g_config_path[0] = '\0';
    if (!get_exe_directory(directory, sizeof(directory)))
        goto done;
    if (!build_path(candidate, sizeof(candidate), directory, "Y720Backlight.ini"))
        goto done;

    file = CreateFileA(candidate, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
        snprintf(g_config_path, sizeof(g_config_path), "%s", candidate);
        goto done;
    }

    {
        char appdata[MAX_PATH], folder[MAX_PATH], fallback[MAX_PATH];
        DWORD length = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
        if (!length || length >= sizeof(appdata))
            goto done;
        if (!build_path(folder, sizeof(folder), appdata, "LegionY720Backlight"))
            goto done;
        if (!CreateDirectoryA(folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            goto done;
        if (!build_path(fallback, sizeof(fallback), folder, "Y720Backlight.ini"))
            goto done;
        if (GetFileAttributesA(fallback) == INVALID_FILE_ATTRIBUTES)
            CopyFileA(candidate, fallback, TRUE);
        snprintf(g_config_path, sizeof(g_config_path), "%s", fallback);
    }

done:
    g_config_path[sizeof(g_config_path) - 1] = '\0';
    g_config_path_initialized = 1;
    snprintf(buffer, buffer_size, "%s", g_config_path);
    buffer[buffer_size - 1] = '\0';
    return g_config_path[0] != '\0';
}

static int zones_have_same_brightness(char *brightness_value, int size)
{
    int zone;

    if (!brightness_value || size <= 0)
        return 0;

    if (!g_last_on_zone_brightness[0][0])
        return 0;

    for (zone = 1; zone < ZONE_COUNT; ++zone)
    {
        if (_stricmp(g_last_on_zone_brightness[zone],
                     g_last_on_zone_brightness[0]) != 0)
            return 0;
    }

    snprintf(brightness_value, size, "%s",
             g_last_on_zone_brightness[0]);

    return 1;
}

static void sync_global_color_from_zones(void)
{
    int zone;
    char first_color[64];
    char zone_color[64];

    if (ZONE_COUNT <= 0)
        return;

    if (!get_combo_value(g_zoneColor[0],
                         colors,
                         (int)(sizeof(colors) / sizeof(colors[0])),
                         first_color,
                         sizeof(first_color)))
        return;

    for (zone = 1; zone < ZONE_COUNT; ++zone)
    {
        if (!get_combo_value(g_zoneColor[zone],
                             colors,
                             (int)(sizeof(colors) / sizeof(colors[0])),
                             zone_color,
                             sizeof(zone_color)))
            return;

        /*
         * There is no single global colour when zones differ.
         * Leave the existing global selection alone in that case.
         */
        if (_stricmp(first_color, zone_color) != 0)
            return;
    }

    /*
     * All zones have the same colour, so that colour is also the
     * effective global keyboard colour.
     */
    select_combo_value(g_globalColor,
                       first_color,
                       colors,
                       (int)(sizeof(colors) / sizeof(colors[0])));
}

static int apply_all_direct(const char *color, const char *bright, const char *mode)
{
    int color_id = color_value(color);
    int bright_id = brightness_value(bright);
    int mode_id = mode_value(mode);

    if (color_id < 0 || bright_id < 0 || mode_id < 0)
    {
        set_status("Invalid lighting settings.");
        return -1;
    }

    if (y720_apply_all(mode_id, color_id, bright_id) != 0)
    {
        set_status("Unable to apply lighting to the Y720 keyboard.");
        return -1;
    }

    return 0;
}

/* === DEVICE / CORE APPLY HELPERS ===
 *
 * Thin wrappers that translate human-readable combobox values into the
 * numeric identifiers required by the core module (Y720BacklightCore) and then
 * call the core apply functions. Keep conversions and basic validation here.
 */
static int apply_zone_direct(int zone, const char *color, const char *bright, const char *mode)
{
    int color_id = color_value(color);
    int bright_id = brightness_value(bright);
    int mode_id = mode_value(mode);

    if (zone < 0 || zone >= ZONE_COUNT || color_id < 0 || bright_id < 0 || mode_id < 0)
    {
        set_status("Invalid zone lighting settings.");
        return -1;
    }

    if (y720_apply_zone(zone, mode_id, color_id, bright_id) != 0)
    {
        set_status("Unable to apply the selected zone settings.");
        return -1;
    }

    return 0;
}

static int turn_off_direct(void)
{
    if (y720_turn_off() != 0)
    {
        set_status("Unable to turn keyboard lighting off.");
        return -1;
    }
    return 0;
}

/* === PERSISTED STATE (saved_state_t) ===
 *
 * This struct mirrors the INI layout written to %APPDATA%/LegionY720Backlight/state.ini
 * and contains the persisted lighting choices. It is intentionally simple and
 * string-based to keep the INI file human-readable.
 */
typedef struct
{
    int valid;
    int enabled;
    char mode[64];
    char global_color[64];
    char global_brightness[32];
    char zone_color[ZONE_COUNT][64];
    char zone_brightness[ZONE_COUNT][32];
} saved_state_t;

static int get_state_path(char *buffer, DWORD buffer_size)
{
    char appdata[MAX_PATH];
    DWORD length;

    if (!buffer || buffer_size == 0)
        return 0;

    length = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (!length || length >= sizeof(appdata) || !appdata[0])
        return 0;

    if (!build_path(buffer, buffer_size, appdata, "LegionY720Backlight"))
        return 0;

    CreateDirectoryA(buffer, NULL);

    if (!build_path(buffer, buffer_size, appdata, "LegionY720Backlight\\state.ini"))
        return 0;

    return 1;
}

static int get_startup_command(char *buffer, DWORD buffer_size)
{
    char exe[MAX_PATH];
    DWORD length;

    if (!buffer || buffer_size == 0)
        return 0;

    length = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!length || length >= sizeof(exe))
        return 0;

    snprintf(buffer, buffer_size, "\"%s\" --startup", exe);
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static int is_startup_enabled(void)
{
    HKEY key;
    LONG result;
    DWORD type = 0, size = 0;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *value_name = "LegionY720Backlight";

    result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS)
        return 0;

    result = RegQueryValueExA(key, value_name, NULL, &type, NULL, &size);
    RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ && size > 0;
}

static int get_startup_command_value(char *buffer, DWORD buffer_size)
{
    HKEY key;
    LONG result;
    DWORD type = 0, size = buffer_size;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *value_name = "LegionY720Backlight";
    int i;

    if (!buffer || !buffer_size)
        return 0;
    buffer[0] = '\0';
    result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS)
        return 0;
    result = RegQueryValueExA(key, value_name, NULL, &type, (LPBYTE)buffer, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || !buffer[0])
        return 0;
    buffer[buffer_size - 1] = '\0';

    /* Basic validation: check for suspicious characters that could indicate injection */
    for (i = 0; buffer[i]; ++i)
    {
        unsigned char c = (unsigned char)buffer[i];
        if (c < 32 || c == 127)
            return 0; /* Control characters */
        if (c == '|' || c == '&' || c == ';' || c == '`' || c == '$')
            return 0; /* Command injection chars */
    }

    return 1;
}

static int set_startup_enabled(int enabled);

static void repair_startup_path_if_enabled(void)
{
    char current[MAX_PATH + 32], stored[MAX_PATH + 64];
    if (!is_startup_enabled())
        return;
    if (!get_startup_command(current, sizeof(current)))
        return;
    if (!get_startup_command_value(stored, sizeof(stored)) || _stricmp(current, stored) != 0)
        set_startup_enabled(1);
}

static int set_startup_enabled(int enabled)
{
    HKEY key;
    LONG result;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *value_name = "LegionY720Backlight";

    if (!enabled)
    {
        result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND)
            return 1;
        if (result != ERROR_SUCCESS)
            return 0;

        result = RegDeleteValueA(key, value_name);
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    result = RegCreateKeyExA(HKEY_CURRENT_USER, run_key, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS)
        return 0;

    {
        char command[MAX_PATH + 32];

        if (!get_startup_command(command, sizeof(command)))
        {
            RegCloseKey(key);
            return 0;
        }

        result = RegSetValueExA(key, value_name, 0, REG_SZ,
                                (const BYTE *)command,
                                (DWORD)strlen(command) + 1);
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

/*
 * Perform uninstall: prompt user, turn off lighting, clear startup entry,
 * delete configuration and state files, attempt to remove the appdata folder,
 * and schedule a background batch that deletes the executable after exit.
 */
#include "uninstall.h"

/* Forward declarations used by perform_uninstall to avoid implicit declarations */
static void remember_current_on_state(void);
static void turn_off(void);
static void exit_application(HWND hwnd);
static void draw_button(const DRAWITEMSTRUCT *item);

/* === UNINSTALL / SHUTDOWN HELPERS ===
 * Uninstall action, graceful shutdown helpers and tray teardown. These are
 * invoked by UI actions but kept here to keep the main logic readable.
 */
static void perform_uninstall(HWND hwnd)
{
    if (MessageBoxA(hwnd,
                    "This will uninstall Legion Y720 Keyboard Backlight Controller.\r\n\r\nIt will remove startup entries, saved settings, and attempt to delete the application files.\r\n\r\nDo you want to continue?",
                    "Uninstall and Clear Residues", MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    /* Try to turn lighting off (best-effort) */
    turn_off();

    set_status("Uninstalling: clearing startup entry and saved state...");

    /* Delegate the heavy lifting to the uninstall module (keeps GUI file smaller). */
    uninstall_schedule_and_cleanup(hwnd);

    set_status("Uninstall scheduled. Exiting...");
    /* Exit the application so the scheduled batch can delete the executable */
    exit_application(hwnd);
}

static void save_state_values(int enabled, const char *mode,
                              const char *global_color, const char *global_brightness,
                              const char zone_color[ZONE_COUNT][64],
                              const char zone_brightness[ZONE_COUNT][32])
{
    char path[MAX_PATH];
    char number[16];
    int zone;

    if (!get_state_path(path, sizeof(path)))
        return;

    WritePrivateProfileStringA("state", "valid", "1", path);

    snprintf(number, sizeof(number), "%d", enabled ? 1 : 0);
    WritePrivateProfileStringA("state", "enabled", number, path);

    WritePrivateProfileStringA("state", "mode",
                               mode ? mode : "always_on", path);
    WritePrivateProfileStringA("state", "global_color",
                               global_color ? global_color : "crimson", path);
    WritePrivateProfileStringA("state", "global_brightness",
                               global_brightness ? global_brightness : "high", path);

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        char key[32];

        snprintf(key, sizeof(key), "zone%d_color", zone);
        WritePrivateProfileStringA("state", key, zone_color[zone], path);

        snprintf(key, sizeof(key), "zone%d_brightness", zone);
        WritePrivateProfileStringA("state", key, zone_brightness[zone], path);
    }
}


static void save_last_on_state(void)
{
    char path[MAX_PATH];
    int zone;

    if (!g_last_on_valid || !get_state_path(path, sizeof(path)))
        return;

    WritePrivateProfileStringA("last_on", "valid", "1", path);
    WritePrivateProfileStringA("last_on", "mode", g_last_on_mode, path);
    WritePrivateProfileStringA("last_on", "global_color", g_last_on_global_color, path);
    WritePrivateProfileStringA("last_on", "global_brightness", g_last_on_global_brightness, path);
    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        char key[32];
        snprintf(key, sizeof(key), "zone%d_color", zone);
        WritePrivateProfileStringA("last_on", key, g_last_on_zone_color[zone], path);
        snprintf(key, sizeof(key), "zone%d_brightness", zone);
        WritePrivateProfileStringA("last_on", key, g_last_on_zone_brightness[zone], path);
    }
}

static void load_last_on_state(void)
{
    char path[MAX_PATH], value[64], key[32];
    int zone;

    g_last_on_valid = 0;
    if (!get_state_path(path, sizeof(path)))
        return;
    GetPrivateProfileStringA("last_on", "valid", "0", value, sizeof(value), path);
    {
        int valid_flag;
        if (!safe_atoi(value, 0, 1, &valid_flag) || valid_flag != 1)
            return;
    }

    GetPrivateProfileStringA("last_on", "mode", "always_on", g_last_on_mode, sizeof(g_last_on_mode), path);
    GetPrivateProfileStringA("last_on", "global_color", "crimson", g_last_on_global_color, sizeof(g_last_on_global_color), path);
    GetPrivateProfileStringA("last_on", "global_brightness", "low", g_last_on_global_brightness, sizeof(g_last_on_global_brightness), path);
    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        snprintf(key, sizeof(key), "zone%d_color", zone);
        GetPrivateProfileStringA("last_on", key, g_last_on_global_color, g_last_on_zone_color[zone], sizeof(g_last_on_zone_color[zone]), path);
        snprintf(key, sizeof(key), "zone%d_brightness", zone);
        GetPrivateProfileStringA("last_on", key, g_last_on_global_brightness, g_last_on_zone_brightness[zone], sizeof(g_last_on_zone_brightness[zone]), path);
    }
    g_last_on_valid = 1;
}

/*
 * Central helper to set the visible "lighting on" state and persist the
 * current control state. This consolidates the common pattern of updating
 * the toggle UI and then saving the current state from multiple call sites.
 */
static void lighting_set_and_save(int enabled, const char *mode_override)
{
    /* Update UI button and cached flag. */
    update_lighting_toggle_button(enabled);

    /* Persist current control values into saved state. */
    save_current_state(enabled, mode_override);
}

static int load_saved_state(saved_state_t *state)
{
    char path[MAX_PATH], key[32], value[64];
    int zone;

    if (!state)
        return 0;

    ZeroMemory(state, sizeof(*state));

    if (!get_state_path(path, sizeof(path)))
        return 0;

    GetPrivateProfileStringA("state", "valid", "0",
                             value, sizeof(value), path);
    {
        int valid_flag;
        if (!safe_atoi(value, 0, 1, &valid_flag) || valid_flag != 1)
            return 0;
    }

    state->valid = 1;

    GetPrivateProfileStringA("state", "enabled", "1",
                             value, sizeof(value), path);
    {
        int enabled_flag;
        if (!safe_atoi(value, 0, 1, &enabled_flag))
            return 0;
        state->enabled = enabled_flag != 0;
    }

    GetPrivateProfileStringA("state", "mode", "always_on",
                             state->mode, sizeof(state->mode), path);
    GetPrivateProfileStringA("state", "global_color", "crimson",
                             state->global_color, sizeof(state->global_color), path);
    GetPrivateProfileStringA("state", "global_brightness", "high",
                             state->global_brightness, sizeof(state->global_brightness), path);

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        snprintf(key, sizeof(key), "zone%d_color", zone);
        GetPrivateProfileStringA("state", key, state->global_color,
                                 state->zone_color[zone],
                                 sizeof(state->zone_color[zone]), path);

        snprintf(key, sizeof(key), "zone%d_brightness", zone);
        GetPrivateProfileStringA("state", key, state->global_brightness,
                                 state->zone_brightness[zone],
                                 sizeof(state->zone_brightness[zone]), path);
    }

    return 1;
}

static int restore_saved_state(void)
{
    saved_state_t state;
    int zone;

    if (!load_saved_state(&state) || !state.valid)
        return 0;

    if (!state.enabled)
    {
        update_lighting_toggle_button(0);
        set_status("Saved state is Off; leaving keyboard lighting off.");
        return 1;
    }

    set_status("Restoring saved keyboard lighting state...");

    if (_stricmp(state.mode, "smooth") == 0)
    {
        if (apply_all_direct(state.global_color, state.global_brightness, "smooth") != 0)
            return -1;
    }
    else
    {
        int mode_id[ZONE_COUNT], color_id[ZONE_COUNT], bright_id[ZONE_COUNT];
        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            mode_id[zone] = mode_value(state.mode);
            color_id[zone] = color_value(state.zone_color[zone]);
            bright_id[zone] = brightness_value(state.zone_brightness[zone]);
            if (mode_id[zone] < 0 || color_id[zone] < 0 || bright_id[zone] < 0)
                return -1;
        }
        if (y720_apply_zones(mode_id, color_id, bright_id) != 0)
            return -1;
    }

    update_lighting_toggle_button(1);
    set_status("Saved keyboard lighting state restored.");
    return 1;
}

/* === STARTUP / SETTINGS HELPERS ===
 * Helpers that update the startup checkbox and read/write small runtime values.
 */
static void update_startup_checkbox(void)
{
    if (g_startup)
    {
        SendMessageA(g_startup, BM_SETCHECK,
                     is_startup_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static int read_global_values(char *color, int color_size,
                              char *bright, int bright_size,
                              char *mode, int mode_size)
{
    if (!get_combo_value(g_globalColor, colors, (int)(sizeof(colors) / sizeof(colors[0])), color, color_size) ||
        !get_combo_value(g_globalBrightness, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])), bright, bright_size) ||
        !get_combo_value(g_globalMode, modes, (int)(sizeof(modes) / sizeof(modes[0])), mode, mode_size))
    {
        set_status("Please select colour, brightness and a keyboard mode.");
        return 0;
    }
    return 1;
}

static void sync_zone_color_brightness(const char *color, const char *bright)
{
    int zone;
    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        select_combo_value(g_zoneColor[zone], color, colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_zoneBrightness[zone], bright, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    }
}

static void apply_all_zones(void)
{
    char color[64], bright[32], mode[64], status[256];
    if (!read_global_values(color, sizeof(color), bright, sizeof(bright), mode, sizeof(mode)))
        return;

    set_status("Applying colour, brightness and mode to the keyboard...");

    if (apply_all_direct(color, bright, mode) == 0)
    {
        sync_zone_color_brightness(color, bright);
        snprintf(status, sizeof(status), "All zones: %s / %s (keyboard mode: %s)", friendly_color(color), friendly_brightness(bright), friendly_mode(mode));
        set_status(status);
        remember_current_on_state();
        lighting_set_and_save(_stricmp(bright, "off") != 0, NULL);
        update_lighting_toggle_button(_stricmp(bright, "off") != 0);
    }
}

static int zones_have_lighting(void)
{
    int zone;

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        char bright[32];

        if (!get_combo_value(g_zoneBrightness[zone],
                             brightness,
                             (int)(sizeof(brightness) / sizeof(brightness[0])),
                             bright,
                             sizeof(bright)))
            return 0;

        if (_stricmp(bright, "off") != 0)
            return 1;
    }

    return 0;
}

/* Mode is keyboard-wide. This applies the selected mode while preserving each zone's colour/brightness. */
static void apply_keyboard_mode(void)
{
    char mode[64], color[ZONE_COUNT][64], bright[ZONE_COUNT][32], status[256];
    int mode_id[ZONE_COUNT], color_id[ZONE_COUNT], bright_id[ZONE_COUNT];
    int zone;

    if (!get_combo_value(g_globalMode, modes, (int)(sizeof(modes) / sizeof(modes[0])), mode, sizeof(mode)))
    {
        set_status("Please select a keyboard mode.");
        return;
    }

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        if (!get_combo_value(g_zoneColor[zone], colors, (int)(sizeof(colors) / sizeof(colors[0])), color[zone], sizeof(color[zone])) ||
            !get_combo_value(g_zoneBrightness[zone], brightness, (int)(sizeof(brightness) / sizeof(brightness[0])), bright[zone], sizeof(bright[zone])))
        {
            set_status("Please select colour and brightness for every zone.");
            return;
        }
        mode_id[zone] = mode_value(mode);
        color_id[zone] = color_value(color[zone]);
        bright_id[zone] = brightness_value(bright[zone]);
        if (mode_id[zone] < 0 || color_id[zone] < 0 || bright_id[zone] < 0)
        {
            set_status("Invalid lighting settings.");
            return;
        }
    }

    set_status("Applying keyboard-wide mode while preserving zone settings...");
    if (y720_apply_zones(mode_id, color_id, bright_id) != 0)
    {
        set_status("Unable to apply the selected keyboard mode.");
        return;
    }

    snprintf(status, sizeof(status), "Keyboard mode: %s. Zone colours and brightness preserved.", friendly_mode(mode));
    set_status(status);
    {
        int lighting_on = zones_have_lighting();
        remember_current_on_state();
        lighting_set_and_save(lighting_on, mode);
    }
}

/* Smooth is intentionally separate: it starts from the current global colour/brightness. */
static void apply_smooth(void)
{
    char color[64], bright[32], status[256];
    if (!get_combo_value(g_globalColor, colors, (int)(sizeof(colors) / sizeof(colors[0])), color, sizeof(color)) ||
        !get_combo_value(g_globalBrightness, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])), bright, sizeof(bright)))
    {
        set_status("Please select a starting colour and brightness for Smooth.");
        return;
    }

    set_status("Starting Smooth lighting from the selected colour...");

    if (apply_all_direct(color, bright, "smooth") == 0)
    {
        const char *display = NULL;
        int i;
        sync_zone_color_brightness(color, bright);
        for (i = 0; i < (int)(sizeof(color_labels) / sizeof(color_labels[0])); ++i)
            if (_stricmp(colors[i], color) == 0)
                display = color_labels[i];
        snprintf(status, sizeof(status), "Smooth lighting started from %s / %s.", display ? display : friendly_color(color), friendly_brightness(bright));
        set_status(status);
        remember_current_on_state();
        lighting_set_and_save(_stricmp(bright, "off") != 0, "smooth");
    }
}

static void remember_current_on_state(void)
{
    int zone;
    char global_brightness[32];

    /*
     * Off is not an On-state. Do not replace a valid saved state
     * with an Off state.
     */
    if (!get_combo_value(g_globalBrightness,
                         brightness,
                         (int)(sizeof(brightness) / sizeof(brightness[0])),
                         global_brightness,
                         sizeof(global_brightness)))
        return;

    if (_stricmp(global_brightness, "off") == 0)
        return;

    if (!get_combo_value(g_globalColor,
                         colors,
                         (int)(sizeof(colors) / sizeof(colors[0])),
                         g_last_on_global_color,
                         sizeof(g_last_on_global_color)) ||
        !get_combo_value(g_globalMode,
                         modes,
                         (int)(sizeof(modes) / sizeof(modes[0])),
                         g_last_on_mode,
                         sizeof(g_last_on_mode)))
        return;

    /*
     * Save the actual four-zone state that is currently active.
     */
    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        if (!get_combo_value(g_zoneColor[zone],
                             colors,
                             (int)(sizeof(colors) / sizeof(colors[0])),
                             g_last_on_zone_color[zone],
                             sizeof(g_last_on_zone_color[zone])) ||
            !get_combo_value(g_zoneBrightness[zone],
                             brightness,
                             (int)(sizeof(brightness) / sizeof(brightness[0])),
                             g_last_on_zone_brightness[zone],
                             sizeof(g_last_on_zone_brightness[zone])))
            return;
    }

    /*
     * Only update the saved global brightness after we have
     * confirmed that it represents an actual On-state.
     */
    snprintf(g_last_on_global_brightness,
             sizeof(g_last_on_global_brightness),
             "%s",
             global_brightness);

    g_last_on_valid = 1;
    save_last_on_state();
}

static void turn_on_from_last_state(void)
{
    int zone;
    char status[256];

    if (!g_last_on_valid)
    {
        /* No persisted pre-Off state is available; use Low as a safe fallback. */
        {
            char restored_global_brightness[32];

            if (zones_have_same_brightness(restored_global_brightness,
                                           sizeof(restored_global_brightness)))
            {
                select_combo_value(g_globalBrightness,
                                   restored_global_brightness,
                                   brightness,
                                   (int)(sizeof(brightness) / sizeof(brightness[0])));
            }
            else
            {
                select_combo_value(g_globalBrightness,
                                   g_last_on_global_brightness,
                                   brightness,
                                   (int)(sizeof(brightness) / sizeof(brightness[0])));
            }
        }
        for (zone = 0; zone < ZONE_COUNT; ++zone)
            select_combo_value(g_zoneBrightness[zone], "low", brightness,
                               (int)(sizeof(brightness) / sizeof(brightness[0])));
        apply_all_zones();
        return;
    }

    set_status("Restoring previous keyboard lighting state...");

    if (_stricmp(g_last_on_mode, "smooth") == 0)
    {
        if (apply_all_direct(g_last_on_global_color, g_last_on_global_brightness, "smooth") != 0)
            return;
    }
    else
    {
        int mode_id[ZONE_COUNT], color_id[ZONE_COUNT], bright_id[ZONE_COUNT];
        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            mode_id[zone] = mode_value(g_last_on_mode);
            color_id[zone] = color_value(g_last_on_zone_color[zone]);
            bright_id[zone] = brightness_value(g_last_on_zone_brightness[zone]);
            if (mode_id[zone] < 0 || color_id[zone] < 0 || bright_id[zone] < 0)
                return;
        }
        if (y720_apply_zones(mode_id, color_id, bright_id) != 0)
            return;
    }

    select_combo_value(g_globalColor, g_last_on_global_color,
                       colors, (int)(sizeof(colors) / sizeof(colors[0])));
    select_combo_value(g_globalBrightness, g_last_on_global_brightness,
                       brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    select_combo_value(g_globalMode, g_last_on_mode,
                       modes, (int)(sizeof(modes) / sizeof(modes[0])));

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        select_combo_value(g_zoneColor[zone], g_last_on_zone_color[zone],
                           colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_zoneBrightness[zone], g_last_on_zone_brightness[zone],
                           brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    }

    snprintf(status, sizeof(status), "Lighting restored: %s / %s.",
             friendly_color(g_last_on_global_color),
             friendly_brightness(g_last_on_global_brightness));
    set_status(status);
    lighting_set_and_save(1, g_last_on_mode);
}

static void remember_current_on_state(void);
static void turn_off(void);

static void toggle_lighting(void)
{
    if (g_lighting_on)
        turn_off();
    else
        turn_on_from_last_state();
}

static void turn_off(void)
{
    int zone;

    /* Capture the actual four-zone state before the hardware is turned off. */
    remember_current_on_state();

    set_status("Turning keyboard lighting off...");
    if (turn_off_direct() == 0)
    {
        /*
         * Off is a reset point for the Fn+Space brightness cycle.
         * Keep the controls synchronized with the actual keyboard state so
         * the next Fn+Space always starts at Low rather than advancing from
         * the brightness that was active before the manual Off command.
         */
        select_combo_value(g_globalBrightness, "off",
                           brightness,
                           (int)(sizeof(brightness) / sizeof(brightness[0])));

        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            select_combo_value(g_zoneBrightness[zone], "off",
                               brightness,
                               (int)(sizeof(brightness) / sizeof(brightness[0])));
        }

        set_status("Keyboard lighting is off. Fn + Space will start at Low.");
        lighting_set_and_save(0, NULL);
    }
}

static void apply_zone(int zone)
{
    char color[64], bright[32], mode[64], status[256];
    if (zone < 0 || zone >= ZONE_COUNT)
        return;

    if (!get_combo_value(g_zoneColor[zone], colors, (int)(sizeof(colors) / sizeof(colors[0])), color, sizeof(color)) ||
        !get_combo_value(g_zoneBrightness[zone], brightness, (int)(sizeof(brightness) / sizeof(brightness[0])), bright, sizeof(bright)) ||
        !get_combo_value(g_globalMode, modes, (int)(sizeof(modes) / sizeof(modes[0])), mode, sizeof(mode)))
    {
        set_status("Please select colour, brightness and a keyboard mode.");
        return;
    }

    snprintf(status, sizeof(status), "Applying settings to Zone %d...", zone);
    set_status(status);

    if (apply_zone_direct(zone, color, bright, mode) == 0)
    {
        int lighting_on = zones_have_lighting();

        snprintf(status, sizeof(status),
                 "Zone %d: %s / %s (keyboard mode: %s)",
                 zone,
                 friendly_color(color),
                 friendly_brightness(bright),
                 friendly_mode(mode));
        set_status(status);

        /*
         * The individual zone is now authoritative. If all zones happen
         * to have the same colour, synchronize the Global Colour control.
         */
        sync_global_color_from_zones();

        remember_current_on_state();
        lighting_set_and_save(lighting_on, mode);
    }
}

/* === PROFILE MANAGEMENT ===
 * Loading, applying and deleting named lighting profiles stored in the config
 * file. Profiles are simple sections with 'color', 'brightness' and 'mode'.
 */
static void apply_profile(void)
{
    int index;
    char profile[128], config[MAX_PATH], color[64] = "", bright[32] = "", mode[64] = "";
    char status[256], display_profile[128];

    index = (int)SendMessageA(g_profile, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= g_profile_count)
    {
        set_status("Please select a profile.");
        return;
    }

    snprintf(profile, sizeof(profile), "%s", g_profile_values[index]);
    make_profile_label(profile, display_profile, sizeof(display_profile));

    if (!get_config_path(config, sizeof(config)))
    {
        set_status("Could not locate configuration file.");
        return;
    }

    GetPrivateProfileStringA(profile, "color", "", color, sizeof(color), config);
    GetPrivateProfileStringA(profile, "brightness", "", bright, sizeof(bright), config);
    GetPrivateProfileStringA(profile, "mode", "", mode, sizeof(mode), config);

    trim(color);
    trim(bright);
    trim(mode);

    if (!color[0] || !bright[0] || !mode[0])
    {
        snprintf(status, sizeof(status), "Profile '%s' is incomplete.", display_profile);
        set_status(status);
        return;
    }

    snprintf(status, sizeof(status), "Applying profile '%s'...", display_profile);
    set_status(status);

    if (apply_all_direct(color, bright, mode) == 0)
    {
        select_combo_value(g_globalColor, color, colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_globalBrightness, bright, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
        select_combo_value(g_globalMode, mode, modes, (int)(sizeof(modes) / sizeof(modes[0])));
        sync_zone_color_brightness(color, bright);
        if (_stricmp(bright, "off") != 0)
            remember_current_on_state();
        snprintf(status, sizeof(status), "Profile '%s' applied: %s / %s / %s",
                 display_profile, friendly_color(color),
                 friendly_brightness(bright), friendly_mode(mode));
        set_status(status);
        lighting_set_and_save(_stricmp(bright, "off") != 0, mode);
    }
    else
    {
        set_status("Profile could not be applied.");
    }
}

/* === FN+SPACE BRIGHTNESS CYCLE ===
 * Handles cycling of the global brightness via the Fn+Space shortcut.
 * Steps: Off -> Low -> Medium -> Ultra -> Off (user-facing grouping).
 */

static int fnspace_next_brightness(const char *current, char *next, int next_size)
{
    int value;

    if (!current || !next || next_size <= 0)
        return 0;

    value = brightness_value(current);

    if (value == 0)
    {
        snprintf(next, next_size, "low");
        return 1;
    }

    if (value == 1)
    {
        snprintf(next, next_size, "medium");
        return 1;
    }

    /* Medium and High are treated as the same user-facing step. */
    if (value == 2 || value == 3)
    {
        snprintf(next, next_size, "ultra");
        return 1;
    }

    /* Ultra and Enough are treated as the same user-facing step. */
    if (value == 4 || value == 5)
    {
        snprintf(next, next_size, "off");
        return 1;
    }

    return 0;
}

static void handle_fn_space(void *context)
{
    char current[32], next[32], mode[64];
    char zone_color[ZONE_COUNT][64];
    char status[256];
    int zone;
    (void)context;

    if (!get_combo_value(g_globalBrightness,
                         brightness,
                         (int)(sizeof(brightness) / sizeof(brightness[0])),
                         current,
                         sizeof(current)))
    {
        set_status("Fn + Space: current brightness is unavailable.");
        return;
    }

    if (!fnspace_next_brightness(current, next, sizeof(next)))
    {
        set_status("Fn + Space: unsupported current brightness.");
        return;
    }

    if (!get_combo_value(g_globalMode,
                         modes,
                         (int)(sizeof(modes) / sizeof(modes[0])),
                         mode,
                         sizeof(mode)))
    {
        set_status("Fn + Space: current keyboard mode is unavailable.");
        return;
    }

    /* Preserve each zone's current colour while changing the keyboard's
       brightness step. The Y720 mode remains keyboard-wide. */
    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        if (!get_combo_value(g_zoneColor[zone],
                             colors,
                             (int)(sizeof(colors) / sizeof(colors[0])),
                             zone_color[zone],
                             sizeof(zone_color[zone])))
        {
            set_status("Fn + Space: current zone colour is unavailable.");
            return;
        }
    }

    set_status("Fn + Space: changing brightness...");

    {
        int mode_id[ZONE_COUNT], color_id[ZONE_COUNT], bright_id[ZONE_COUNT];
        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            mode_id[zone] = mode_value(mode);
            color_id[zone] = color_value(zone_color[zone]);
            bright_id[zone] = brightness_value(next);
            if (mode_id[zone] < 0 || color_id[zone] < 0 || bright_id[zone] < 0)
            {
                set_status("Fn + Space: invalid lighting settings.");
                return;
            }
        }
        if (y720_apply_zones(mode_id, color_id, bright_id) != 0)
            return;
    }

    select_combo_value(g_globalBrightness,
                       next,
                       brightness,
                       (int)(sizeof(brightness) / sizeof(brightness[0])));

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        select_combo_value(g_zoneBrightness[zone],
                           next,
                           brightness,
                           (int)(sizeof(brightness) / sizeof(brightness[0])));
    }

    snprintf(status,
             sizeof(status),
             "Fn + Space: brightness %s -> %s.",
             friendly_brightness(current),
             friendly_brightness(next));
    set_status(status);

    int lighting_on = (_stricmp(next, "off") != 0);

    if (lighting_on)
        remember_current_on_state();

    lighting_set_and_save(lighting_on, NULL);
}

static void load_profiles(void)
{
    char config[MAX_PATH], profiles[4096], *p;
    DWORD length;
    int i, j;
    g_profile_count = 0;
    if (g_profile)
        SendMessageA(g_profile, CB_RESETCONTENT, 0, 0);

    if (!get_config_path(config, sizeof(config)))
        return;

    ZeroMemory(profiles, sizeof(profiles));
    length = GetPrivateProfileStringA(NULL, NULL, "", profiles, sizeof(profiles), config);
    if (!length)
        return;

    /* Check if the buffer was too small to hold all profiles */
    if (length >= sizeof(profiles) - 2)
    {
        set_status("Warning: Profile list may be truncated. Consider reducing number of profiles.");
    }

    p = profiles;
    while (*p && g_profile_count < MAX_PROFILES)
    {
        snprintf(g_profile_values[g_profile_count],
                 sizeof(g_profile_values[g_profile_count]), "%s", p);
        ++g_profile_count;
        p += strlen(p) + 1;
    }

    /* Warn if we hit the profile limit */
    if (g_profile_count >= MAX_PROFILES && *p)
    {
        set_status("Warning: Maximum number of profiles reached. Some profiles may not be loaded.");
    }

    for (i = 1; i < g_profile_count; ++i)
    {
        char key[128];
        snprintf(key, sizeof(key), "%s", g_profile_values[i]);
        j = i - 1;
        while (j >= 0)
        {
            char a[128], b[128];
            make_profile_label(g_profile_values[j], a, sizeof(a));
            make_profile_label(key, b, sizeof(b));
            if (_stricmp(a, b) <= 0)
                break;
            snprintf(g_profile_values[j + 1], sizeof(g_profile_values[j + 1]), "%s", g_profile_values[j]);
            --j;
        }
        snprintf(g_profile_values[j + 1], sizeof(g_profile_values[j + 1]), "%s", key);
    }

    for (i = 0; i < g_profile_count; ++i)
    {
        char display_profile[128];
        make_profile_label(g_profile_values[i], display_profile, sizeof(display_profile));
        SendMessageA(g_profile, CB_ADDSTRING, 0, (LPARAM)display_profile);
    }
    if (g_profile_count > 0)
        SendMessageA(g_profile, CB_SETCURSEL, 0, 0);
}
typedef struct
{
    HWND hwnd;
    HWND name, color, bright, mode;
    int accepted;
} profile_dialog_t;

static profile_dialog_t g_profile_dialog;

static void profile_dialog_close(int accepted)
{
    g_profile_dialog.accepted = accepted;
    if (g_profile_dialog.hwnd)
        DestroyWindow(g_profile_dialog.hwnd);
}

static void show_main_window(void);

/* === DIALOG: PROFILE CREATION ===
 * Modal dialog message proc for profile creation. Handles saving and validating
 * a new profile and uses the same colour/brightness helpers as the main UI.
 */
static LRESULT CALLBACK ProfileDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        int color_count = (int)(sizeof(colors) / sizeof(colors[0]));
        int bright_count = (int)(sizeof(brightness) / sizeof(brightness[0]));
        int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));
        create_label(hwnd, "Profile Name", 20, 20, 100, LAYOUT_CONTROL_HEIGHT);
        g_profile_dialog.name = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                                SCALE(125), SCALE(18), SCALE(235), SCALE(26), hwnd, (HMENU)(INT_PTR)IDC_PROFILE_NAME,
                                                GetModuleHandleA(NULL), NULL);
        SendMessageA(g_profile_dialog.name, WM_SETFONT, (WPARAM)g_font, TRUE);
        create_label(hwnd, "Colour", 20, 60, 100, LAYOUT_CONTROL_HEIGHT);
        g_profile_dialog.color = create_color_combo(hwnd, IDC_PROFILE_COLOR, 125, 56, 235, 300);
        add_combo_items(g_profile_dialog.color, color_labels, color_count);
        select_combo_value(g_profile_dialog.color, "crimson", colors, color_count);
        create_label(hwnd, "Brightness", 20, 100, 100, LAYOUT_CONTROL_HEIGHT);
        g_profile_dialog.bright = create_brightness_combo(hwnd, IDC_PROFILE_BRIGHTNESS, 125, 96, 235, 300);
        add_combo_items(g_profile_dialog.bright, brightness_labels, bright_count);
        select_combo_value(g_profile_dialog.bright, "high", brightness, bright_count);
        create_label(hwnd, "Mode", 20, 140, 100, LAYOUT_CONTROL_HEIGHT);
        g_profile_dialog.mode = create_combo(hwnd, IDC_PROFILE_MODE, 125, 136, 235, 300);
        add_combo_items(g_profile_dialog.mode, mode_labels, mode_count);
        select_combo_value(g_profile_dialog.mode, "always_on", modes, mode_count);
        create_button(hwnd, "Save Profile", IDC_PROFILE_SAVE, 90, 185, LAYOUT_BUTTON_WIDTH, LAYOUT_BUTTON_HEIGHT);
        create_button(hwnd, "Cancel", IDC_PROFILE_CANCEL, 230, 185, LAYOUT_BUTTON_WIDTH, LAYOUT_BUTTON_HEIGHT);
        SendMessageA(hwnd, DM_SETDEFID, IDC_PROFILE_SAVE, 0);
        return 0;
    }

    /* Ensure controls match the main window's theme by handling control color messages */
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, CLR_BG);
        if (message == WM_CTLCOLORBTN)
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        return (LRESULT)g_bgBrush;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
        if (mis && mis->CtlType == ODT_COMBOBOX)
        {
            mis->itemHeight = SCALE(24);
        }
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
        if (dis && dis->CtlType == ODT_COMBOBOX && is_color_combo_hwnd(dis->hwndItem))
        {
            draw_color_combobox(dis);
            return TRUE;
        }
        else if (dis)
        {
            draw_button(dis);
            return TRUE;
        }
        return TRUE;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, CLR_CONTROL);
        return (LRESULT)g_controlBrush;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDC_PROFILE_CANCEL)
        {
            profile_dialog_close(0);
            return 0;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDC_PROFILE_SAVE)
        {
            char name[128], color[64], bright[32], mode[64], config[MAX_PATH];
            char display[128];
            GetWindowTextA(g_profile_dialog.name, name, sizeof(name));
            trim(name);
            if (!is_valid_profile_name(name))
            {
                MessageBoxA(hwnd, "Profile name must be 1-64 characters and cannot contain special characters: [ ] = \\ / \" ; : * ? < > |", "Profile", MB_ICONWARNING);
                return 0;
            }
            if (!get_combo_value(g_profile_dialog.color, colors, (int)(sizeof(colors) / sizeof(colors[0])), color, sizeof(color)) ||
                !get_combo_value(g_profile_dialog.bright, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])), bright, sizeof(bright)) ||
                !get_combo_value(g_profile_dialog.mode, modes, (int)(sizeof(modes) / sizeof(modes[0])), mode, sizeof(mode)) ||
                !get_config_path(config, sizeof(config)))
                return 0;
            if (GetPrivateProfileStringA(name, "color", "", display, sizeof(display), config) > 0)
            {
                if (MessageBoxA(hwnd, "A profile with this name already exists. Replace it?", "Profile", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return 0;
            }
            WritePrivateProfileStringA(name, "color", color, config);
            WritePrivateProfileStringA(name, "brightness", bright, config);
            WritePrivateProfileStringA(name, "mode", mode, config);
            profile_dialog_close(1);
            return 0;
        }
        break;
    case WM_CLOSE:
        profile_dialog_close(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static int create_profile_dialog(HWND owner)
{
    WNDCLASSA wc;
    MSG msg;
    const char *class_name = "Y720ProfileDialogClass";
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ProfileDialogProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);
    ZeroMemory(&g_profile_dialog, sizeof(g_profile_dialog));
    g_profile_dialog.hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name, "Create Profile",
                                            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
                                            CW_USEDEFAULT, CW_USEDEFAULT, SCALE(390), SCALE(260), owner, NULL, GetModuleHandleA(NULL), NULL);
    if (!g_profile_dialog.hwnd)
    {
        EnableWindow(owner, TRUE);
        return 0;
    }
    EnableWindow(owner, FALSE);
    ShowWindow(g_profile_dialog.hwnd, SW_SHOW);
    SetForegroundWindow(g_profile_dialog.hwnd);
    while (IsWindow(g_profile_dialog.hwnd))
    {
        int result = GetMessageA(&msg, NULL, 0, 0);
        if (result == 0)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (result < 0)
            break;
        if (IsDialogMessageA(g_profile_dialog.hwnd, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_profile_dialog.accepted;
}

static void create_profile(void)
{
    if (create_profile_dialog(g_hWnd))
    {
        load_profiles();
        set_status("Profile created successfully.");
    }
}

static void delete_profile(void)
{
    int index;
    char config[MAX_PATH], display[128], message[256];
    index = (int)SendMessageA(g_profile, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= g_profile_count)
    {
        set_status("Please select a profile to delete.");
        return;
    }
    make_profile_label(g_profile_values[index], display, sizeof(display));
    snprintf(message, sizeof(message), "Delete profile '%s'?", display);
    if (MessageBoxA(g_hWnd, message, "Delete Profile", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    if (!get_config_path(config, sizeof(config)) || !WritePrivateProfileStringA(g_profile_values[index], NULL, NULL, config))
    {
        set_status("Unable to delete the selected profile.");
        return;
    }
    load_profiles();
    set_status("Profile deleted.");
}

static void setup_tray_data(HWND hwnd)
{
    ZeroMemory(&g_trayIcon, sizeof(g_trayIcon));
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = TRAY_ICON_ID;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAYICON;
    g_trayIcon.hIcon = g_appIcon;
    lstrcpynA(g_trayIcon.szTip, "Legion Y720 Keyboard Backlight", sizeof(g_trayIcon.szTip));
}

static int add_tray_icon(HWND hwnd)
{
    setup_tray_data(hwnd);
    if (!g_trayIcon.hIcon)
        return 0;
    if (Shell_NotifyIconA(NIM_ADD, &g_trayIcon))
    {
        g_trayIcon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconA(NIM_SETVERSION, &g_trayIcon);
        g_trayAdded = 1;
        return 1;
    }
    g_trayAdded = 0;
    return 0;
}

static void remove_tray_icon(void)
{
    if (g_trayAdded)
    {
        Shell_NotifyIconA(NIM_DELETE, &g_trayIcon);
        g_trayAdded = 0;
    }
}

static void show_main_window(void)
{
    if (!g_hWnd)
        return;
    ShowWindow(g_hWnd, SW_SHOW);
    ShowWindow(g_hWnd, SW_RESTORE);
    SetForegroundWindow(g_hWnd);
}

static void hide_to_tray(void)
{
    if (g_hWnd)
        ShowWindow(g_hWnd, SW_HIDE);
}

static void show_tray_menu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    POINT point;
    if (!menu)
        return;
    AppendMenuA(menu, MF_STRING, ID_TRAY_OFF, "Turn Lighting Off");
    AppendMenuA(menu, MF_STRING, ID_TRAY_ON, "Turn Lighting On");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
    if (!GetCursorPos(&point))
    {
        DestroyMenu(menu);
        return;
    }
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_NOANIMATION,
                   point.x, point.y, 0, hwnd, NULL);
    PostMessageA(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void exit_application(HWND hwnd)
{
    g_exiting = 1;
    remove_tray_icon();
    DestroyWindow(hwnd);
}

/* === UI CREATION HELPERS (IMPLEMENTATION) ===
 * Small helpers that create labels, comboboxes and buttons with the common
 * fonts and styles used throughout the GUI. Keep these compact and reusable.
 */
static HWND create_label(HWND parent, const char *text, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE,
                                SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, NULL,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create label control.");
    }
    return hwnd;
}

static HWND create_group(HWND parent, const char *text, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(0, "BUTTON", text,
                                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, NULL,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create group control.");
    }
    return hwnd;
}

/* === COMBO / CONTROL HELPERS ===
 * Create standard combobox controls (non-color) and apply the shared font.
 */
static HWND create_combo(HWND parent, int id, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST | WS_VSCROLL,
                                SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, (HMENU)(INT_PTR)id,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create combo control.");
    }
    return hwnd;
}

/* === OWNER-DRAWN COLOR COMBO CREATION ===
 * Create an owner-drawn colour combobox (fixed item height) so each item can
 * show a small swatch. Only use this for colour selectors; other combos can
 * remain the standard system-drawn control. */
static HWND create_color_combo(HWND parent, int id, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST | WS_VSCROLL | CBS_OWNERDRAWFIXED,
                                SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, (HMENU)(INT_PTR)id,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create colour combo control.");
    }
    return hwnd;
}

static HWND create_brightness_combo(HWND parent, int id, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    CBS_DROPDOWNLIST | WS_VSCROLL | CBS_OWNERDRAWFIXED,
                                SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, (HMENU)(INT_PTR)id,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create brightness combo control.");
    }
    return hwnd;
}

static int is_accent_button(int id)
{
    return id == IDC_APPLY_ALL || id == IDC_APPLY_MODE ||
           id == IDC_SMOOTH || id == IDC_APPLY_PROFILE ||
           id == IDC_PROFILE_SAVE;
}

static HWND create_button(HWND parent, const char *text, int id,
                          int x, int y, int width, int height)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    HWND hwnd;

    /* Primary actions use an owner-drawn Legion-red treatment. Secondary
       actions keep the restrained dark Windows button style. */
    if (is_accent_button(id))
        style |= BS_OWNERDRAW;

    hwnd = CreateWindowExA(0, "BUTTON", text, style,
                           SCALE(x), SCALE(y), SCALE(width), SCALE(height), parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleA(NULL), NULL);
    if (hwnd)
    {
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    }
    else
    {
        set_status("Error: Unable to create button control.");
    }
    return hwnd;
}

static void draw_button(const DRAWITEMSTRUCT *item)
{
    /* Route owner-drawn combobox items here if the WM_DRAWITEM handler wasn't
       updated in all locations: handle colour and brightness combos first. */
    if (item && item->CtlType == ODT_COMBOBOX)
    {
        if (is_color_combo_hwnd(item->hwndItem))
        {
            draw_color_combobox(item);
            return;
        }
        if (is_brightness_combo_hwnd(item->hwndItem))
        {
            draw_brightness_combobox(item);
            return;
        }
    }

    RECT rect;
    HBRUSH brush;
    HPEN pen;
    char text[256];
    UINT state;
    COLORREF fill;
    COLORREF edge;
    COLORREF text_color;

    if (!item)
        return;
    rect = item->rcItem;
    state = item->itemState;

    if (is_accent_button((int)item->CtlID))
    {
        fill = (state & ODS_SELECTED) ? CLR_RED_DARK : CLR_RED;
        edge = CLR_RED;
        text_color = RGB(255, 255, 255);
    }
    else
    {
        fill = CLR_BUTTON;
        edge = CLR_BUTTON_EDGE;
        text_color = CLR_TEXT;
    }

    brush = CreateSolidBrush(fill);
    FillRect(item->hDC, &rect, brush);
    DeleteObject(brush);

    pen = CreatePen(PS_SOLID, 1, edge);
    if (pen)
    {
        HGDIOBJ old_pen = SelectObject(item->hDC, pen);
        HGDIOBJ old_brush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(item->hDC, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(item->hDC, old_brush);
        SelectObject(item->hDC, old_pen);
        DeleteObject(pen);
    }

    GetWindowTextA(item->hwndItem, text, sizeof(text));
    text[sizeof(text) - 1] = '\0';
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text_color);
    SelectObject(item->hDC, g_font);
    DrawTextA(item->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (state & ODS_FOCUS)
    {
        RECT focus = rect;
        InflateRect(&focus, -SCALE(LAYOUT_FOCUS_INSET), -SCALE(LAYOUT_FOCUS_INSET));
        DrawFocusRect(item->hDC, &focus);
    }
}

/* Helper to determine whether a combobox HWND is one of the colour selectors */
static int is_color_combo_hwnd(HWND hwnd)
{
    int i;
    if (!hwnd)
        return 0;
    if (hwnd == g_globalColor)
        return 1;
    if (g_profile_dialog.color && hwnd == g_profile_dialog.color)
        return 1;
    for (i = 0; i < ZONE_COUNT; ++i)
        if (g_zoneColor[i] && hwnd == g_zoneColor[i])
            return 1;
    return 0;
}

static int is_brightness_combo_hwnd(HWND hwnd)
{
    int i;
    if (!hwnd)
        return 0;
    if (hwnd == g_globalBrightness)
        return 1;
    if (g_profile_dialog.bright && hwnd == g_profile_dialog.bright)
        return 1;
    for (i = 0; i < ZONE_COUNT; ++i)
        if (g_zoneBrightness[i] && hwnd == g_zoneBrightness[i])
            return 1;
    return 0;
}

/* === OWNER-DRAWN COMBOBOX DRAW HELPERS ===
 * Draw an owner-drawn combobox list item with a small colour swatch on the left
 * and the friendly label to the right. Handles both list items and the
 * selection display (itemID == -1) by resolving the current selection index. */
static void draw_color_combobox(const DRAWITEMSTRUCT *item)
{
    if (!item)
        return;

    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    int idx = (int)item->itemID;
    char text[128] = {0};
    int color_count = (int)(sizeof(color_map) / sizeof(color_map[0]));

    /* If itemID == -1, draw the selection field: query current selection */
    if (idx == -1)
    {
        idx = (int)SendMessageA(item->hwndItem, CB_GETCURSEL, 0, 0);
    }

    /* Determine selection background/text colours using system highlight for accessibility */
    BOOL is_selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF bg = is_selected ? GetSysColor(COLOR_HIGHLIGHT) : CLR_CONTROL;
    COLORREF text_color = is_selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : CLR_TEXT;

    /* Background */
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);

    /* Swatch rectangle */
    int pad = SCALE(4);
    int sw = SCALE(16);
    RECT swr = {rc.left + pad, rc.top + SCALE(3), rc.left + pad + sw, rc.bottom - SCALE(3)};

    if (idx >= 0 && idx < color_count)
    {
        COLORREF c = color_map[idx];
        /* Special rendering for the "nocolor" entry (draw an empty box with an X) */
        if (c == RGB(40, 40, 40))
        {
            HBRUSH oldb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
            HGDIOBJ oldpen = SelectObject(dc, pen);
            Rectangle(dc, swr.left, swr.top, swr.right, swr.bottom);
            MoveToEx(dc, swr.left, swr.top, NULL);
            LineTo(dc, swr.right, swr.bottom);
            MoveToEx(dc, swr.left, swr.bottom, NULL);
            LineTo(dc, swr.right, swr.top);
            SelectObject(dc, oldpen);
            SelectObject(dc, oldb);
            DeleteObject(pen);
        }
        else
        {
            HBRUSH b = CreateSolidBrush(c);
            FillRect(dc, &swr, b);
            DeleteObject(b);
            /* border */
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HGDIOBJ old = SelectObject(dc, pen);
            HBRUSH oldb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, swr.left, swr.top, swr.right, swr.bottom);
            SelectObject(dc, oldb);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
    else
    {
        /* Unknown index: draw empty box */
        HBRUSH oldb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        HGDIOBJ old = SelectObject(dc, pen);
        Rectangle(dc, swr.left, swr.top, swr.right, swr.bottom);
        SelectObject(dc, old);
        SelectObject(dc, oldb);
        DeleteObject(pen);
    }

    /* Text: use the canonical labels array to avoid encoding issues when
       retrieving text from the control. The colour labels are stored in
       color_labels[] and are aligned with color_map/colors[]. */
    if (idx >= 0 && idx < color_count)
    {
        /* Copy with truncation protection */
        strncpy(text, color_labels[idx], sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }
    else
    {
        text[0] = '\0';
    }

    RECT tr = rc;
    tr.left = swr.right + SCALE(6);
    tr.right = rc.right - SCALE(4);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_color);
    HGDIOBJ old_font = SelectObject(dc, g_font);
    DrawTextA(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    /* Restore previous font */
    SelectObject(dc, old_font);

    if (item->itemState & ODS_FOCUS)
    {
        RECT focus = tr;
        InflateRect(&focus, -SCALE(LAYOUT_FOCUS_INSET), -SCALE(LAYOUT_FOCUS_INSET));
        DrawFocusRect(dc, &focus);
    }
}

/* === OWNER-DRAWN BRIGHTNESS DRAW HELPERS ===
 * Draw brightness indicators as a group of filled/unfilled circles followed by
 * the human-readable label. Uses brightness_labels[] as the canonical text. */
static void draw_brightness_combobox(const DRAWITEMSTRUCT *item)
{
    if (!item)
        return;

    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    int idx = (int)item->itemID; /* may be -1 for the selection field */
    char text[128] = {0};
    int bcount = (int)(sizeof(brightness) / sizeof(brightness[0]));
    int total_slots = 5; /* render 0..5 filled circles */

    if (idx == -1)
        idx = (int)SendMessageA(item->hwndItem, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= bcount)
        idx = 0;

    BOOL is_selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF bg = is_selected ? GetSysColor(COLOR_HIGHLIGHT) : CLR_CONTROL;
    COLORREF text_color = is_selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : CLR_TEXT;

    /* Background */
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);

    /* Circles area */
    int pad = SCALE(4);
    int diameter = SCALE(6);
    int spacing = SCALE(3);
    int start_x = rc.left + pad;
    int cy = (rc.top + rc.bottom) / 2;
    int y1 = cy - diameter / 2;
    int y2 = cy + diameter / 2;

    /* Draw total_slots circles using a compact layout so the text label still fits. */
    int i;
    for (i = 0; i < total_slots; ++i)
    {
        int x1 = start_x + i * (diameter + spacing);
        int x2 = x1 + diameter;
        /* Border */
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
        HGDIOBJ oldpen = SelectObject(dc, pen);
        if (i < idx)
        {
            /* filled */
            HBRUSH b = CreateSolidBrush(text_color);
            HGDIOBJ oldb = SelectObject(dc, b);
            Ellipse(dc, x1, y1, x2, y2);
            SelectObject(dc, oldb);
            DeleteObject(b);
        }
        else
        {
            /* empty */
            HBRUSH oldb = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, x1, y1, x2, y2);
            SelectObject(dc, oldb);
        }
        SelectObject(dc, oldpen);
        DeleteObject(pen);
    }

    /* Text label to the right of circles */
    strncpy(text, brightness_labels[idx], sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    RECT tr = rc;
    tr.left = start_x + total_slots * (diameter + spacing) + SCALE(4);
    tr.right = rc.right - SCALE(4);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_color);
    HGDIOBJ oldfont = SelectObject(dc, g_font);
    DrawTextA(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, oldfont);

    if (item->itemState & ODS_FOCUS)
    {
        RECT focus = tr;
        InflateRect(&focus, -SCALE(LAYOUT_FOCUS_INSET), -SCALE(LAYOUT_FOCUS_INSET));
        DrawFocusRect(dc, &focus);
    }
}

/* === UI CREATION (CONTROLS) ===
 * Create and layout the primary controls used by the GUI (comboboxes, buttons,
 * status area and profile controls). Keep layout values centralized above.
 */
static void create_controls(HWND hwnd)
{
    int color_count = (int)(sizeof(colors) / sizeof(colors[0]));
    int bright_count = (int)(sizeof(brightness) / sizeof(brightness[0]));
    int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));
    int zone;
    HWND title;

    title = create_label(hwnd, "LEGION Y720 KEYBOARD BACKLIGHT", LAYOUT_MARGIN_X + 5, LAYOUT_MARGIN_Y, 700, 35);
    g_title = title;
    if (title)
        SendMessageA(title, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    create_label(hwnd, "Native Windows controller  |  Four-zone colour and brightness control", LAYOUT_MARGIN_X + 7, LAYOUT_MARGIN_Y + 33, 760, 25);

    create_group(hwnd, "Global Lighting", LAYOUT_MARGIN_X, 82, LAYOUT_GROUP_WIDTH_FULL, LAYOUT_GROUP_HEIGHT);
    create_label(hwnd, "Colour", LAYOUT_MARGIN_X + 20, 115, LAYOUT_CONTROL_WIDTH, LAYOUT_CONTROL_HEIGHT);
    g_globalColor = create_color_combo(hwnd, IDC_GLOBAL_COLOR, 105, 111, LAYOUT_COMBO_WIDTH_LONG, 300);
    add_combo_items(g_globalColor, color_labels, color_count);
    select_combo_value(g_globalColor, "crimson", colors, color_count);

    create_label(hwnd, "Brightness", 335, 115, 85, LAYOUT_CONTROL_HEIGHT);
    g_globalBrightness = create_brightness_combo(hwnd, IDC_GLOBAL_BRIGHTNESS, 420, 111, LAYOUT_COMBO_WIDTH, 300);
    add_combo_items(g_globalBrightness, brightness_labels, bright_count);
    select_combo_value(g_globalBrightness, "high", brightness, bright_count);

    create_label(hwnd, "Keyboard Mode", 565, 115, 100, LAYOUT_CONTROL_HEIGHT);
    g_globalMode = create_combo(hwnd, IDC_GLOBAL_MODE, 665, 111, 125, 300);
    add_combo_items(g_globalMode, mode_labels, mode_count);
    select_combo_value(g_globalMode, "always_on", modes, mode_count);

    create_button(hwnd, "Apply Colour + Brightness to All Zones", IDC_APPLY_ALL, 45, 155, LAYOUT_BUTTON_WIDTH_MAX, LAYOUT_BUTTON_HEIGHT_MED);
    g_lightingToggle = create_button(hwnd, "Turn Lighting Off", IDC_OFF, 360, 155, LAYOUT_BUTTON_WIDTH_LONG, LAYOUT_BUTTON_HEIGHT_MED);
    create_button(hwnd, "Apply Mode to Keyboard", IDC_APPLY_MODE, 555, 155, LAYOUT_BUTTON_WIDTH_XLONG, LAYOUT_BUTTON_HEIGHT_MED);
    create_label(hwnd, "Mode is keyboard-wide on the Y720; colour and brightness remain zone-specific.", 45, 197, 740, 20);

    create_group(hwnd, "Smooth Lighting", LAYOUT_MARGIN_X, 240, LAYOUT_GROUP_WIDTH_FULL, LAYOUT_GROUP_HEIGHT_SMALL);
    create_label(hwnd, "Cycles colours automatically, starting from the selected global colour and brightness.",
                 45, 270, 600, 25);
    create_button(hwnd, "Start Smooth", IDC_SMOOTH, 660, 265, 130, LAYOUT_BUTTON_HEIGHT);

    create_group(hwnd, "Individual Zone Colour and Brightness", LAYOUT_MARGIN_X, 330, LAYOUT_GROUP_WIDTH_FULL, LAYOUT_GROUP_HEIGHT_LARGE);
    create_label(hwnd, "Zone", 45, 360, 65, 22);
    create_label(hwnd, "Keyboard Area", 115, 360, 180, 22);
    create_label(hwnd, "Colour", 305, 360, 130, 22);
    create_label(hwnd, "Brightness", 495, 360, 100, 22);

    for (zone = 0; zone < ZONE_COUNT; ++zone)
    {
        int y = 386 + zone * 42;
        char zone_number[32];
        snprintf(zone_number, sizeof(zone_number), "Zone %d", zone);
        create_label(hwnd, zone_number, 45, y + 7, 65, 25);
        create_label(hwnd, zone_names[zone], 115, y + 7, 180, 25);

        g_zoneColor[zone] = create_color_combo(hwnd, IDC_ZONE_BASE + zone * 10 + 1, 305, y, 170, 280);
        add_combo_items(g_zoneColor[zone], color_labels, color_count);
        select_combo_value(g_zoneColor[zone], "crimson", colors, color_count);

        g_zoneBrightness[zone] = create_brightness_combo(hwnd, IDC_ZONE_BASE + zone * 10 + 2, 495, y, 125, 280);
        add_combo_items(g_zoneBrightness[zone], brightness_labels, bright_count);
        select_combo_value(g_zoneBrightness[zone], "high", brightness, bright_count);

        create_button(hwnd, "Apply", IDC_ZONE_BASE + zone * 10 + 4, 650, y, 120, 27);
    }

    create_group(hwnd, "Profiles", LAYOUT_MARGIN_X, 580, LAYOUT_GROUP_WIDTH, LAYOUT_GROUP_HEIGHT_MED);
    create_label(hwnd, "Profile", 45, 609, 60, LAYOUT_CONTROL_HEIGHT);
    g_profile = create_combo(hwnd, IDC_PROFILE, 105, 605, 380, 300);
    load_profiles();
    create_button(hwnd, "Apply", IDC_APPLY_PROFILE, 45, 645, LAYOUT_BUTTON_WIDTH, LAYOUT_BUTTON_HEIGHT);
    create_button(hwnd, "New", IDC_PROFILE_NEW, 200, 645, LAYOUT_BUTTON_WIDTH, LAYOUT_BUTTON_HEIGHT);
    create_button(hwnd, "Delete", IDC_PROFILE_DELETE, 355, 645, LAYOUT_BUTTON_WIDTH, LAYOUT_BUTTON_HEIGHT);

    create_group(hwnd, "Startup", 530, 580, 295, LAYOUT_GROUP_HEIGHT_MED);
    g_startup = CreateWindowExA(0, "BUTTON", "Start with Windows and Restore Last State",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                SCALE(555), SCALE(602), SCALE(255), SCALE(24), hwnd, (HMENU)(INT_PTR)IDC_STARTUP,
                                GetModuleHandleA(NULL), NULL);
    if (g_startup)
    {
        SendMessageA(g_startup, WM_SETFONT, (WPARAM)g_font, TRUE);
        update_startup_checkbox();
    }
    create_label(hwnd, "Fn + Space: Off -> Low -> Medium -> Ultra -> Off. Keep the GUI running in the tray for this shortcut to work.",
                 555, 636, 255, 45);

    load_state_into_controls();
    load_last_on_state();

    create_label(hwnd, "Status", 30, 710, 55, LAYOUT_CONTROL_HEIGHT);
    /* Reduce width slightly to make room for the Uninstall button on the right */
    g_status = create_label(hwnd, "Ready.", 85, 710, 650, LAYOUT_CONTROL_HEIGHT);
    /* Small uninstall/clear-residue button on the bottom-right */
    create_button(hwnd, "Uninstall", IDC_UNINSTALL, 740, 706, 90, LAYOUT_BUTTON_HEIGHT);
}

/* === MAIN WINDOW: WNDPROC & MESSAGE LOOP ===
 * Main window procedure: routes commands, owner-draw notifications and tray
 * interactions. Keep message handling clear and delegate heavy work to helpers.
 */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreated && message == g_taskbarCreated)
    {
        g_trayAdded = 0;
        add_tray_icon(hwnd);
        return 0;
    }

    switch (message)
    {
    case WM_SHOW_EXISTING:
        show_main_window();
        return 0;

    case WM_CREATE:
        create_controls(hwnd);
        if (!add_tray_icon(hwnd))
            set_status("Warning: unable to create system tray icon.");
        return 0;

    case WM_INPUT:
    case WM_INPUT_DEVICE_CHANGE:
        if (y720_input_handle_message(message, wParam, lParam))
        {
            if (message == WM_INPUT)
                return DefWindowProcA(hwnd, message, wParam, lParam);
            return 0;
        }
        break;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int zone;
        if (id == IDC_APPLY_ALL)
        {
            apply_all_zones();
            return 0;
        }
        if (id == IDC_APPLY_MODE)
        {
            apply_keyboard_mode();
            return 0;
        }
        if (id == IDC_SMOOTH)
        {
            apply_smooth();
            return 0;
        }
        if (id == IDC_OFF)
        {
            toggle_lighting();
            return 0;
        }
        if (id == IDC_APPLY_PROFILE)
        {
            apply_profile();
            return 0;
        }
        if (id == IDC_PROFILE_NEW)
        {
            create_profile();
            return 0;
        }
        if (id == IDC_PROFILE_DELETE)
        {
            delete_profile();
            return 0;
        }
        if (id == IDC_STARTUP && HIWORD(wParam) == BN_CLICKED)
        {
            int enabled = (int)SendMessageA(g_startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!set_startup_enabled(enabled))
            {
                SendMessageA(g_startup, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
                set_status("Unable to change the Windows startup setting.");
            }
            else
            {
                set_status(enabled ? "Start with Windows enabled; last lighting state will be restored."
                                   : "Start with Windows disabled.");
            }
            return 0;
        }
        if (id == IDC_UNINSTALL)
        {
            perform_uninstall(hwnd);
            return 0;
        }
        if (id == ID_TRAY_OFF)
        {
            turn_off();
            return 0;
        }
        if (id == ID_TRAY_ON)
        {
            turn_on_from_last_state();
            return 0;
        }
        if (id == ID_TRAY_EXIT)
        {
            exit_application(hwnd);
            return 0;
        }
        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            if (id == IDC_ZONE_BASE + zone * 10 + 4)
            {
                apply_zone(zone);
                return 0;
            }
        }
        break;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            hide_to_tray();
            return 0;
        }
        break;

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
        if (mis && mis->CtlType == ODT_COMBOBOX)
        {
            mis->itemHeight = SCALE(24);
        }
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
        if (dis && dis->CtlType == ODT_COMBOBOX && is_color_combo_hwnd(dis->hwndItem))
        {
            draw_color_combobox(dis);
            return TRUE;
        }
        else if (dis)
        {
            draw_button(dis);
            return TRUE;
        }
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;
        if (control == g_title)
            SetTextColor(dc, CLR_RED);
        else
            SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, CLR_BG);
        if (message == WM_CTLCOLORBTN)
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        return (LRESULT)g_bgBrush;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, CLR_TEXT);
        SetBkColor(dc, CLR_CONTROL);
        return (LRESULT)g_controlBrush;
    }

    case WM_TRAYICON:
    {
        UINT event = LOWORD(lParam);
        switch (event)
        {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            show_main_window();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            show_tray_menu(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        if (!g_exiting)
        {
            hide_to_tray();
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

/* === ENTRYPOINT: WinMain ===
 * Program entrypoint: register window class, create the main window and enter
 * the standard Win32 message loop. Keep startup/teardown calls explicit here.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    MSG message;
    int startup_launch = 0;
    (void)previous;
    init_dpi_scaling();
    if (command_line && strstr(command_line, "--startup"))
        startup_launch = 1;

    g_single_instance_mutex = CreateMutexA(NULL, TRUE, "Local\\LegionY720BacklightController");
    if (!g_single_instance_mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowA(CLASS_NAME, NULL);
        if (existing)
            PostMessageA(existing, WM_SHOW_EXISTING, 0, 0);
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = NULL;
        return 0;
    }

    g_appIcon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(IDI_Y720_KEYBOARD), IMAGE_ICON,
                                  32, 32, LR_DEFAULTSIZE);
    if (!g_appIcon)
        g_appIcon = LoadIconA(NULL, IDI_APPLICATION);
    g_taskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    g_font = CreateFontA(SCALE(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fontBold = CreateFontA(SCALE(21), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = g_appIcon;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    g_bgBrush = CreateSolidBrush(CLR_BG);
    g_controlBrush = CreateSolidBrush(CLR_CONTROL);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassA(&wc))
    {
        MessageBoxA(NULL, "Unable to register GUI window class.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    g_hWnd = CreateWindowExA(0, CLASS_NAME, APP_TITLE,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, SCALE(LAYOUT_WINDOW_WIDTH), SCALE(LAYOUT_WINDOW_HEIGHT),
                             NULL, NULL, instance, NULL);
    if (!g_hWnd)
    {
        MessageBoxA(NULL, "Unable to create GUI window.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    center_window_on_active_monitor(g_hWnd);

    if (g_appIcon)
    {
        SendMessageA(g_hWnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        SendMessageA(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
    }

    if (!y720_input_init(g_hWnd, handle_fn_space, NULL))
        set_status("Warning: Fn + Space input could not be initialized.");

    repair_startup_path_if_enabled();

    if (startup_launch && is_startup_enabled())
    {
        ShowWindow(g_hWnd, SW_HIDE);
        UpdateWindow(g_hWnd);
        restore_saved_state();
    }
    else
    {
        ShowWindow(g_hWnd, show_command);
        UpdateWindow(g_hWnd);
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    /* Return the message wParam as the process exit code and close WinMain. */
    return (int)message.wParam;
}

/* === STATE MODULE (UI STATE HELPERS) ===
 * Encapsulates reading, applying and persisting the visible UI control state.
 * ui_state_t and its helpers are co-located here so the rest of the GUI can
 * treat state as a single unit. The implementation is intentionally kept in-file
 * for teaching convenience, but clearly separated from unrelated GUI code.
 */

    typedef struct ui_state_t
    {
        char mode[64];
        char global_color[64];
        char global_brightness[32];
        char zone_color[ZONE_COUNT][64];
        char zone_brightness[ZONE_COUNT][32];
    } ui_state_t;

    /* Read the current control values into the ui_state_t. Returns 1 on success. */
    static int ui_state_read(ui_state_t *s)
    {
        int zone;

        if (!s)
            return 0;

        if (!get_combo_value(g_globalColor,
                             colors, (int)(sizeof(colors) / sizeof(colors[0])),
                             s->global_color, sizeof(s->global_color)) ||
            !get_combo_value(g_globalBrightness,
                             brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                             s->global_brightness, sizeof(s->global_brightness)))
            return 0;

        if (!get_combo_value(g_globalMode,
                             modes, (int)(sizeof(modes) / sizeof(modes[0])),
                             s->mode, sizeof(s->mode)))
            return 0;

        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            if (!get_combo_value(g_zoneColor[zone],
                                 colors, (int)(sizeof(colors) / sizeof(colors[0])),
                                 s->zone_color[zone], sizeof(s->zone_color[zone])) ||
                !get_combo_value(g_zoneBrightness[zone],
                                 brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                                 s->zone_brightness[zone], sizeof(s->zone_brightness[zone])))
                return 0;
        }

        return 1;
    }

    /* Apply a ui_state_t to the UI controls. */
    static void ui_state_apply_to_controls(const ui_state_t *s)
    {
        int zone;
        if (!s)
            return;

        select_combo_value(g_globalColor, s->global_color, colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_globalBrightness, s->global_brightness, brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
        select_combo_value(g_globalMode, s->mode, modes, (int)(sizeof(modes) / sizeof(modes[0])));

        for (zone = 0; zone < ZONE_COUNT; ++zone)
        {
            select_combo_value(g_zoneColor[zone], s->zone_color[zone], colors, (int)(sizeof(colors) / sizeof(colors[0])));
            select_combo_value(g_zoneBrightness[zone], s->zone_brightness[zone], brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
        }
    }

    /* Save ui_state_t into persistent state file. */
    static void ui_state_save(const ui_state_t *s, int enabled)
    {
        if (!s)
            return;
        save_state_values(enabled, s->mode, s->global_color, s->global_brightness, s->zone_color, s->zone_brightness);
    }

    /* Save the current UI control values into persistent storage. */
    static void save_current_state(int enabled, const char *mode_override)
    {
        ui_state_t s;

        if (!ui_state_read(&s))
            return;

        if (mode_override)
        {
            snprintf(s.mode, sizeof(s.mode), "%s", mode_override);
        }

        ui_state_save(&s, enabled);
    }

    /* Convert saved_state_t -> ui_state_t and apply to controls. This was moved here
     * so the full ui_state_t is visible (typedef is defined in this section). */
    static void load_state_into_controls(void)
    {
        saved_state_t state;

        if (!load_saved_state(&state))
            return;

        /* Convert saved_state_t to ui_state_t and apply to controls in one place. */
        ui_state_t s;
        snprintf(s.mode, sizeof(s.mode), "%s", state.mode);
        snprintf(s.global_color, sizeof(s.global_color), "%s", state.global_color);
        snprintf(s.global_brightness, sizeof(s.global_brightness), "%s", state.global_brightness);
        for (int zone = 0; zone < ZONE_COUNT; ++zone)
        {
            snprintf(s.zone_color[zone], sizeof(s.zone_color[zone]), "%s", state.zone_color[zone]);
            snprintf(s.zone_brightness[zone], sizeof(s.zone_brightness[zone]), "%s", state.zone_brightness[zone]);
        }

        ui_state_apply_to_controls(&s);

        update_lighting_toggle_button(state.enabled);
    }
