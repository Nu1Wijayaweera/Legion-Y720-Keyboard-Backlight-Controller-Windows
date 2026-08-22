#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <winreg.h>
#include <stdio.h>
#include <string.h>
#include "Y720BacklightCore.h"
#include "Y720BacklightHID.h"

#define APP_TITLE "Legion Y720 Keyboard Backlight Controller"
#define CLASS_NAME "Y720BacklightGUIClass"
#define ZONE_COUNT 4

#define IDC_GLOBAL_COLOR       1001
#define IDC_GLOBAL_BRIGHTNESS  1002
#define IDC_GLOBAL_MODE        1003
#define IDC_APPLY_ALL          1004
#define IDC_APPLY_MODE         1005
#define IDC_OFF                1006
#define IDC_SMOOTH             1007
#define IDC_PROFILE            1008
#define IDC_APPLY_PROFILE      1009
#define IDC_ZONE_BASE          2000
#define IDC_STATUS             3000
#define IDC_STARTUP            3001
#define IDC_PROFILE_NEW        3010
#define IDC_PROFILE_DELETE     3011
#define IDC_PROFILE_DIALOG     3100
#define IDC_PROFILE_NAME       3101
#define IDC_PROFILE_COLOR      3102
#define IDC_PROFILE_BRIGHTNESS 3103
#define IDC_PROFILE_MODE       3104
#define IDC_PROFILE_SAVE       3105
#define IDC_PROFILE_CANCEL     3106

#define WM_SHOW_EXISTING       (WM_APP + 2)

#define WM_TRAYICON            (WM_APP + 1)
#define TRAY_ICON_ID           5001
#define IDI_Y720_KEYBOARD      101
#define ID_TRAY_OFF            5003
#define ID_TRAY_ON             5004
#define ID_TRAY_EXIT           5005

static HWND g_hWnd;
static HWND g_title;
static HWND g_globalColor, g_globalBrightness, g_globalMode;
static HWND g_profile, g_status, g_startup;
static HWND g_lightingToggle;
#define MAX_PROFILES 64
static char g_profile_values[MAX_PROFILES][128];
static int g_profile_count = 0;
static HWND g_zoneColor[ZONE_COUNT], g_zoneBrightness[ZONE_COUNT];
static HFONT g_font, g_fontBold;
static NOTIFYICONDATAA g_trayIcon;
static int g_trayAdded = 0, g_exiting = 0;
static UINT g_taskbarCreated = 0;
static HICON g_appIcon = NULL;
static HBRUSH g_bgBrush = NULL;
static HBRUSH g_controlBrush = NULL;
static HANDLE g_single_instance_mutex = NULL;

/* Runtime copy of the last lighting state before an explicit Off command.
   This lets tray On restore the exact four-zone state without changing the
   persistent Off state or complicating the normal lighting path. */
static int g_last_on_valid = 0;

static void update_lighting_toggle_button(int lighting_on)
{
    if (g_lightingToggle)
        SetWindowTextA(g_lightingToggle, lighting_on ? "Turn Lighting Off" : "Turn Lighting On");
}
static char g_last_on_mode[64];
static char g_last_on_global_color[64];
static char g_last_on_global_brightness[32];
static char g_last_on_zone_color[ZONE_COUNT][64];
static char g_last_on_zone_brightness[ZONE_COUNT][32];

/* Legion-style dark charcoal/red accent palette. */
#define CLR_BG        RGB(18,18,20)
#define CLR_PANEL     RGB(27,27,30)
#define CLR_CONTROL   RGB(35,35,39)
#define CLR_TEXT      RGB(235,235,238)
#define CLR_MUTED     RGB(170,170,176)
#define CLR_RED       RGB(220,30,45)
#define CLR_RED_DARK  RGB(145,20,30)
#define CLR_BUTTON    RGB(38,38,42)
#define CLR_BUTTON_EDGE RGB(70,70,75)

static const char *colors[] = {
    "azure_radiance", "blue", "blue_ribbon", "bright_green",
    "crimson", "cyan", "electric_violet", "electric_violet_2",
    "green", "hollywood_cerise", "international_orange", "lime",
    "magenta", "nocolor", "spring_green", "spring_green_2",
    "torch_red", "web_orange", "white", "yellow"
};

static const char *color_labels[] = {
    "Azure Radiance", "Blue", "Blue Ribbon", "Bright Green",
    "Crimson", "Cyan", "Electric Violet", "Electric Violet 2",
    "Green", "Hollywood Cerise", "International Orange", "Lime",
    "Magenta", "No Color", "Spring Green", "Spring Green 2",
    "Torch Red", "Web Orange", "White", "Yellow"
};

static const char *brightness[] = { "off", "low", "medium", "high", "ultra", "enough" };
static const char *brightness_labels[] = { "Off", "Low", "Medium", "High", "Ultra", "Enough" };

static const char *modes[] = { "always_on", "breath", "heartbeat", "wave" };
static const char *mode_labels[] = { "Always On", "Breath", "Heartbeat", "Wave" };
static const char *friendly_value(const char *value,
                                  const char **values, const char **labels, int count)
{
    int i;
    if (!value) return "";
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

    if (!buffer || buffer_size <= 0) return;
    buffer[0] = '\0';
    if (!value) return;

    for (i = 0; value[i] && j < buffer_size - 1; ++i) {
        char c = value[i];
        if (c == '_' || c == '-') {
            if (j > 0 && buffer[j - 1] != ' ')
                buffer[j++] = ' ';
            capitalize = 1;
        } else {
            if (capitalize && c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            buffer[j++] = c;
            capitalize = 0;
        }
    }
    while (j > 0 && buffer[j - 1] == ' ') --j;
    buffer[j] = '\0';
}


static const char *zone_names[] = {
    "Caps Lock -> D", "F -> K", "L -> Enter", "Numeric Keypad"
};

static void set_status(const char *text)
{
    if (g_status) SetWindowTextA(g_status, text);
}

static void add_combo_items(HWND combo, const char **labels, int count)
{
    int i;
    if (!combo) return;
    for (i = 0; i < count; ++i)
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)labels[i]);
}

static int select_combo_value(HWND combo, const char *value,
                              const char **values, int count)
{
    int i;
    if (!combo || !value) return 0;
    for (i = 0; i < count; ++i) {
        if (_stricmp(values[i], value) == 0) {
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
    if (!buffer || buffer_size <= 0) return 0;
    buffer[0] = '\0';
    if (!combo) return 0;
    index = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= count) return 0;
    snprintf(buffer, buffer_size, "%s", values[index]);
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static void trim(char *s)
{
    char *start, *end;
    if (!s) return;
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
    if (start != s) memmove(s, start, strlen(start) + 1);
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = '\0';
}

static int get_exe_directory(char *buffer, DWORD buffer_size)
{
    DWORD length;
    char *slash;
    if (!buffer || !buffer_size) return 0;
    length = GetModuleFileNameA(NULL, buffer, buffer_size);
    if (!length || length >= buffer_size) return 0;
    slash = strrchr(buffer, '\\');
    if (!slash) slash = strrchr(buffer, '/');
    if (!slash) return 0;
    *slash = '\0';
    return 1;
}

static int get_config_path(char *buffer, DWORD buffer_size)
{
    char directory[MAX_PATH];
    if (!get_exe_directory(directory, sizeof(directory))) return 0;
    snprintf(buffer, buffer_size, "%s\\Y720Backlight.ini", directory);
    buffer[buffer_size - 1] = '\0';
    return 1;
}


static int apply_all_direct(const char *color, const char *bright, const char *mode)
{
    int color_id = color_value(color);
    int bright_id = brightness_value(bright);
    int mode_id = mode_value(mode);

    if (color_id < 0 || bright_id < 0 || mode_id < 0) {
        set_status("Invalid lighting settings.");
        return -1;
    }

    if (y720_apply_all(mode_id, color_id, bright_id) != 0) {
        set_status("Unable to apply lighting to the Y720 keyboard.");
        return -1;
    }

    return 0;
}

static int apply_zone_direct(int zone, const char *color, const char *bright, const char *mode)
{
    int color_id = color_value(color);
    int bright_id = brightness_value(bright);
    int mode_id = mode_value(mode);

    if (zone < 0 || zone >= ZONE_COUNT || color_id < 0 || bright_id < 0 || mode_id < 0) {
        set_status("Invalid zone lighting settings.");
        return -1;
    }

    if (y720_apply_zone(zone, mode_id, color_id, bright_id) != 0) {
        set_status("Unable to apply the selected zone settings.");
        return -1;
    }

    return 0;
}

static int turn_off_direct(void)
{
    if (y720_turn_off() != 0) {
        set_status("Unable to turn keyboard lighting off.");
        return -1;
    }
    return 0;
}


typedef struct {
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
    if (!buffer || buffer_size == 0) return 0;
    if (!GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata)) || !appdata[0])
        return 0;

    snprintf(buffer, buffer_size, "%s\\LegionY720Backlight", appdata);
    buffer[buffer_size - 1] = '\0';
    CreateDirectoryA(buffer, NULL);

    snprintf(buffer, buffer_size, "%s\\LegionY720Backlight\\state.ini", appdata);
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static int get_startup_command(char *buffer, DWORD buffer_size)
{
    char exe[MAX_PATH];
    DWORD length;

    if (!buffer || buffer_size == 0) return 0;

    length = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!length || length >= sizeof(exe)) return 0;

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
    if (result != ERROR_SUCCESS) return 0;

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

    if (!buffer || !buffer_size) return 0;
    buffer[0] = '\0';
    result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS) return 0;
    result = RegQueryValueExA(key, value_name, NULL, &type, (LPBYTE)buffer, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || !buffer[0]) return 0;
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static int set_startup_enabled(int enabled);

static void repair_startup_path_if_enabled(void)
{
    char current[MAX_PATH + 32], stored[MAX_PATH + 64];
    if (!is_startup_enabled()) return;
    if (!get_startup_command(current, sizeof(current))) return;
    if (!get_startup_command_value(stored, sizeof(stored)) || _stricmp(current, stored) != 0)
        set_startup_enabled(1);
}

static int set_startup_enabled(int enabled)
{
    HKEY key;
    LONG result;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *value_name = "LegionY720Backlight";

    if (!enabled) {
        result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND) return 1;
        if (result != ERROR_SUCCESS) return 0;

        result = RegDeleteValueA(key, value_name);
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    result = RegCreateKeyExA(HKEY_CURRENT_USER, run_key, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) return 0;

    {
        char command[MAX_PATH + 32];

        if (!get_startup_command(command, sizeof(command))) {
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

static void save_state_values(int enabled, const char *mode,
                              const char *global_color, const char *global_brightness,
                              const char zone_color[ZONE_COUNT][64],
                              const char zone_brightness[ZONE_COUNT][32])
{
    char path[MAX_PATH];
    char number[16];
    int zone;

    if (!get_state_path(path, sizeof(path))) return;

    WritePrivateProfileStringA("state", "valid", "1", path);

    snprintf(number, sizeof(number), "%d", enabled ? 1 : 0);
    WritePrivateProfileStringA("state", "enabled", number, path);

    WritePrivateProfileStringA("state", "mode",
                               mode ? mode : "always_on", path);
    WritePrivateProfileStringA("state", "global_color",
                               global_color ? global_color : "crimson", path);
    WritePrivateProfileStringA("state", "global_brightness",
                               global_brightness ? global_brightness : "high", path);

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        char key[32];

        snprintf(key, sizeof(key), "zone%d_color", zone);
        WritePrivateProfileStringA("state", key, zone_color[zone], path);

        snprintf(key, sizeof(key), "zone%d_brightness", zone);
        WritePrivateProfileStringA("state", key, zone_brightness[zone], path);
    }
}

static void save_current_state(int enabled, const char *mode_override)
{
    char mode[64], global_color[64], global_brightness[32];
    char zone_color[ZONE_COUNT][64], zone_brightness[ZONE_COUNT][32];
    int zone;

    if (!get_combo_value(g_globalColor,
                         colors, (int)(sizeof(colors) / sizeof(colors[0])),
                         global_color, sizeof(global_color)) ||
        !get_combo_value(g_globalBrightness,
                         brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                         global_brightness, sizeof(global_brightness)))
        return;

    if (mode_override) {
        snprintf(mode, sizeof(mode), "%s", mode_override);
    } else if (!get_combo_value(g_globalMode,
                                modes, (int)(sizeof(modes) / sizeof(modes[0])),
                                mode, sizeof(mode))) {
        return;
    }

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (!get_combo_value(g_zoneColor[zone],
                             colors, (int)(sizeof(colors) / sizeof(colors[0])),
                             zone_color[zone], sizeof(zone_color[zone])) ||
            !get_combo_value(g_zoneBrightness[zone],
                             brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                             zone_brightness[zone], sizeof(zone_brightness[zone])))
            return;
    }

    save_state_values(enabled, mode, global_color, global_brightness,
                      zone_color, zone_brightness);
}

static int load_saved_state(saved_state_t *state)
{
    char path[MAX_PATH], key[32], value[64];
    int zone;

    if (!state) return 0;

    ZeroMemory(state, sizeof(*state));

    if (!get_state_path(path, sizeof(path))) return 0;

    GetPrivateProfileStringA("state", "valid", "0",
                             value, sizeof(value), path);
    if (atoi(value) != 1) return 0;

    state->valid = 1;

    GetPrivateProfileStringA("state", "enabled", "1",
                             value, sizeof(value), path);
    state->enabled = atoi(value) != 0;

    GetPrivateProfileStringA("state", "mode", "always_on",
                             state->mode, sizeof(state->mode), path);
    GetPrivateProfileStringA("state", "global_color", "crimson",
                             state->global_color, sizeof(state->global_color), path);
    GetPrivateProfileStringA("state", "global_brightness", "high",
                             state->global_brightness, sizeof(state->global_brightness), path);

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
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

static void load_state_into_controls(void)
{
    saved_state_t state;
    int zone;

    if (!load_saved_state(&state)) return;

    select_combo_value(g_globalColor, state.global_color,
                       colors, (int)(sizeof(colors) / sizeof(colors[0])));
    select_combo_value(g_globalBrightness, state.global_brightness,
                       brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    select_combo_value(g_globalMode, state.mode,
                       modes, (int)(sizeof(modes) / sizeof(modes[0])));

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        select_combo_value(g_zoneColor[zone], state.zone_color[zone],
                           colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_zoneBrightness[zone], state.zone_brightness[zone],
                           brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    }

    update_lighting_toggle_button(state.enabled);
}

static int restore_saved_state(void)
{
    saved_state_t state;
    int zone;

    if (!load_saved_state(&state) || !state.valid) return 0;

    if (!state.enabled) {
        update_lighting_toggle_button(0);
        set_status("Saved state is Off; leaving keyboard lighting off.");
        return 1;
    }

    set_status("Restoring saved keyboard lighting state...");

    if (_stricmp(state.mode, "smooth") == 0) {
        if (apply_all_direct(state.global_color, state.global_brightness, "smooth") != 0)
            return -1;
    } else {
        for (zone = 0; zone < ZONE_COUNT; ++zone) {
            if (apply_zone_direct(zone, state.zone_color[zone],
                                  state.zone_brightness[zone], state.mode) != 0)
                return -1;
        }
    }

    update_lighting_toggle_button(1);
    set_status("Saved keyboard lighting state restored.");
    return 1;
}

static void update_startup_checkbox(void)
{
    if (g_startup) {
        SendMessageA(g_startup, BM_SETCHECK,
                     is_startup_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static int read_global_values(char *color, int color_size,
                              char *bright, int bright_size,
                              char *mode, int mode_size)
{
    if (!get_combo_value(g_globalColor, colors, (int)(sizeof(colors)/sizeof(colors[0])), color, color_size) ||
        !get_combo_value(g_globalBrightness, brightness, (int)(sizeof(brightness)/sizeof(brightness[0])), bright, bright_size) ||
        !get_combo_value(g_globalMode, modes, (int)(sizeof(modes)/sizeof(modes[0])), mode, mode_size)) {
        set_status("Please select colour, brightness and a keyboard mode.");
        return 0;
    }
    return 1;
}

static void sync_zone_color_brightness(const char *color, const char *bright)
{
    int zone;
    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        select_combo_value(g_zoneColor[zone], color, colors, (int)(sizeof(colors)/sizeof(colors[0])));
        select_combo_value(g_zoneBrightness[zone], bright, brightness, (int)(sizeof(brightness)/sizeof(brightness[0])));
    }
}

static void apply_all_zones(void)
{
    char color[64], bright[32], mode[64], status[256];
    if (!read_global_values(color, sizeof(color), bright, sizeof(bright), mode, sizeof(mode))) return;

    set_status("Applying colour, brightness and mode to the keyboard...");

    if (apply_all_direct(color, bright, mode) == 0) {
        sync_zone_color_brightness(color, bright);
        snprintf(status, sizeof(status), "All zones: %s / %s (keyboard mode: %s)", friendly_color(color), friendly_brightness(bright), friendly_mode(mode));
        set_status(status);
        save_current_state(1, NULL);
        update_lighting_toggle_button(1);
    }
}

/* Mode is keyboard-wide. This applies the selected mode while preserving each zone's colour/brightness. */
static void apply_keyboard_mode(void)
{
    char mode[64], color[ZONE_COUNT][64], bright[ZONE_COUNT][32], status[256];
    int zone;

    if (!get_combo_value(g_globalMode, modes, (int)(sizeof(modes)/sizeof(modes[0])), mode, sizeof(mode))) {
        set_status("Please select a keyboard mode.");
        return;
    }

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (!get_combo_value(g_zoneColor[zone], colors, (int)(sizeof(colors)/sizeof(colors[0])), color[zone], sizeof(color[zone])) ||
            !get_combo_value(g_zoneBrightness[zone], brightness, (int)(sizeof(brightness)/sizeof(brightness[0])), bright[zone], sizeof(bright[zone]))) {
            set_status("Please select colour and brightness for every zone.");
            return;
        }
    }

    set_status("Applying keyboard-wide mode while preserving zone settings...");
    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (apply_zone_direct(zone, color[zone], bright[zone], mode) != 0) return;
    }

    snprintf(status, sizeof(status), "Keyboard mode: %s. Zone colours and brightness preserved.", friendly_mode(mode));
    set_status(status);
    save_current_state(1, mode);
    update_lighting_toggle_button(1);
}

/* Smooth is intentionally separate: it starts from the current global colour/brightness. */
static void apply_smooth(void)
{
    char color[64], bright[32], status[256];
    if (!get_combo_value(g_globalColor, colors, (int)(sizeof(colors)/sizeof(colors[0])), color, sizeof(color)) ||
        !get_combo_value(g_globalBrightness, brightness, (int)(sizeof(brightness)/sizeof(brightness[0])), bright, sizeof(bright))) {
        set_status("Please select a starting colour and brightness for Smooth.");
        return;
    }

    set_status("Starting Smooth lighting from the selected colour...");

    if (apply_all_direct(color, bright, "smooth") == 0) {
        char *display = NULL;
        int i;
        (void)display;
        sync_zone_color_brightness(color, bright);
        for (i = 0; i < (int)(sizeof(color_labels)/sizeof(color_labels[0])); ++i)
            if (_stricmp(colors[i], color) == 0) display = (char *)color_labels[i];
        snprintf(status, sizeof(status), "Smooth lighting started from %s / %s.", display ? display : friendly_color(color), friendly_brightness(bright));
        set_status(status);
        save_current_state(1, "smooth");
        update_lighting_toggle_button(1);
    }
}

static void remember_current_on_state(void)
{
    int zone;

    if (!get_combo_value(g_globalBrightness,
                         brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                         g_last_on_global_brightness, sizeof(g_last_on_global_brightness)))
        return;

    /* Do not replace a valid saved state with another Off state. */
    if (_stricmp(g_last_on_global_brightness, "off") == 0)
        return;

    if (!get_combo_value(g_globalColor, colors, (int)(sizeof(colors) / sizeof(colors[0])),
                         g_last_on_global_color, sizeof(g_last_on_global_color)) ||
        !get_combo_value(g_globalMode, modes, (int)(sizeof(modes) / sizeof(modes[0])),
                         g_last_on_mode, sizeof(g_last_on_mode)))
        return;

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (!get_combo_value(g_zoneColor[zone], colors, (int)(sizeof(colors) / sizeof(colors[0])),
                             g_last_on_zone_color[zone], sizeof(g_last_on_zone_color[zone])) ||
            !get_combo_value(g_zoneBrightness[zone], brightness, (int)(sizeof(brightness) / sizeof(brightness[0])),
                             g_last_on_zone_brightness[zone], sizeof(g_last_on_zone_brightness[zone])))
            return;
    }

    g_last_on_valid = 1;
}

static void turn_on_from_last_state(void)
{
    int zone;
    char status[256];

    if (!g_last_on_valid) {
        /* No in-memory state to restore; retain the existing simple fallback. */
        apply_all_zones();
        return;
    }

    set_status("Restoring previous keyboard lighting state...");

    if (_stricmp(g_last_on_mode, "smooth") == 0) {
        if (apply_all_direct(g_last_on_global_color, g_last_on_global_brightness, "smooth") != 0)
            return;
    } else {
        for (zone = 0; zone < ZONE_COUNT; ++zone) {
            if (apply_zone_direct(zone, g_last_on_zone_color[zone],
                                  g_last_on_zone_brightness[zone], g_last_on_mode) != 0)
                return;
        }
    }

    select_combo_value(g_globalColor, g_last_on_global_color,
                       colors, (int)(sizeof(colors) / sizeof(colors[0])));
    select_combo_value(g_globalBrightness, g_last_on_global_brightness,
                       brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    select_combo_value(g_globalMode, g_last_on_mode,
                       modes, (int)(sizeof(modes) / sizeof(modes[0])));

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        select_combo_value(g_zoneColor[zone], g_last_on_zone_color[zone],
                           colors, (int)(sizeof(colors) / sizeof(colors[0])));
        select_combo_value(g_zoneBrightness[zone], g_last_on_zone_brightness[zone],
                           brightness, (int)(sizeof(brightness) / sizeof(brightness[0])));
    }

    snprintf(status, sizeof(status), "Lighting restored: %s / %s.",
             friendly_color(g_last_on_global_color),
             friendly_brightness(g_last_on_global_brightness));
    set_status(status);
    save_current_state(1, g_last_on_mode);
    update_lighting_toggle_button(1);
}

static void turn_off(void);

static void toggle_lighting(void)
{
    char text[64];

    if (!g_lightingToggle) {
        turn_off();
        return;
    }

    GetWindowTextA(g_lightingToggle, text, sizeof(text));
    if (_stricmp(text, "Turn Lighting On") == 0)
        turn_on_from_last_state();
    else
        turn_off();
}

static void turn_off(void)
{
    int zone;

    /* Capture the actual four-zone state before the hardware is turned off. */
    remember_current_on_state();

    set_status("Turning keyboard lighting off...");
    if (turn_off_direct() == 0) {
        /*
         * Off is a reset point for the Fn+Space brightness cycle.
         * Keep the controls synchronized with the actual keyboard state so
         * the next Fn+Space always starts at Low rather than advancing from
         * the brightness that was active before the manual Off command.
         */
        select_combo_value(g_globalBrightness, "off",
                           brightness,
                           (int)(sizeof(brightness) / sizeof(brightness[0])));

        for (zone = 0; zone < ZONE_COUNT; ++zone) {
            select_combo_value(g_zoneBrightness[zone], "off",
                               brightness,
                               (int)(sizeof(brightness) / sizeof(brightness[0])));
        }

        set_status("Keyboard lighting is off. Fn + Space will start at Low.");
        update_lighting_toggle_button(0);
        save_current_state(0, NULL);
    }
}

static void apply_zone(int zone)
{
    char color[64], bright[32], mode[64], status[256];
    if (zone < 0 || zone >= ZONE_COUNT) return;

    if (!get_combo_value(g_zoneColor[zone], colors, (int)(sizeof(colors)/sizeof(colors[0])), color, sizeof(color)) ||
        !get_combo_value(g_zoneBrightness[zone], brightness, (int)(sizeof(brightness)/sizeof(brightness[0])), bright, sizeof(bright)) ||
        !get_combo_value(g_globalMode, modes, (int)(sizeof(modes)/sizeof(modes[0])), mode, sizeof(mode))) {
        set_status("Please select colour, brightness and a keyboard mode.");
        return;
    }

    snprintf(status, sizeof(status), "Applying settings to Zone %d...", zone);
    set_status(status);

    if (apply_zone_direct(zone, color, bright, mode) == 0) {
        snprintf(status, sizeof(status), "Zone %d: %s / %s (keyboard mode: %s)", zone, friendly_color(color), friendly_brightness(bright), friendly_mode(mode));
        set_status(status);
        save_current_state(1, mode);
        update_lighting_toggle_button(1);
    }
}

static void apply_profile(void)
{
    int index;
    char profile[128], config[MAX_PATH], color[64] = "", bright[32] = "", mode[64] = "";
    char status[256], display_profile[128];

    index = (int)SendMessageA(g_profile, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= g_profile_count) {
        set_status("Please select a profile.");
        return;
    }

    snprintf(profile, sizeof(profile), "%s", g_profile_values[index]);
    make_profile_label(profile, display_profile, sizeof(display_profile));

    if (!get_config_path(config, sizeof(config))) {
        set_status("Could not locate configuration file.");
        return;
    }

    GetPrivateProfileStringA(profile, "color", "", color, sizeof(color), config);
    GetPrivateProfileStringA(profile, "brightness", "", bright, sizeof(bright), config);
    GetPrivateProfileStringA(profile, "mode", "", mode, sizeof(mode), config);

    trim(color);
    trim(bright);
    trim(mode);

    if (!color[0] || !bright[0] || !mode[0]) {
        snprintf(status, sizeof(status), "Profile '%s' is incomplete.", display_profile);
        set_status(status);
        return;
    }

    snprintf(status, sizeof(status), "Applying profile '%s'...", display_profile);
    set_status(status);

    if (apply_all_direct(color, bright, mode) == 0) {
        select_combo_value(g_globalColor, color, colors, (int)(sizeof(colors)/sizeof(colors[0])));
        select_combo_value(g_globalBrightness, bright, brightness, (int)(sizeof(brightness)/sizeof(brightness[0])));
        select_combo_value(g_globalMode, mode, modes, (int)(sizeof(modes)/sizeof(modes[0])));
        sync_zone_color_brightness(color, bright);
        snprintf(status, sizeof(status), "Profile '%s' applied: %s / %s / %s",
                 display_profile, friendly_color(color),
                 friendly_brightness(bright), friendly_mode(mode));
        set_status(status);
        save_current_state(1, mode);
    } else {
        set_status("Profile could not be applied.");
    }
}


/* =========================================================
   Fn + Space brightness cycle
   ========================================================= */

static int fnspace_next_brightness(const char *current, char *next, int next_size)
{
    int value;

    if (!current || !next || next_size <= 0)
        return 0;

    value = brightness_value(current);

    if (value == 0) {
        snprintf(next, next_size, "low");
        return 1;
    }

    if (value == 1) {
        snprintf(next, next_size, "medium");
        return 1;
    }

    /* Medium and High are treated as the same user-facing step. */
    if (value == 2 || value == 3) {
        snprintf(next, next_size, "ultra");
        return 1;
    }

    /* Ultra and Enough are treated as the same user-facing step. */
    if (value == 4 || value == 5) {
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
                         sizeof(current))) {
        set_status("Fn + Space: current brightness is unavailable.");
        return;
    }

    if (!fnspace_next_brightness(current, next, sizeof(next))) {
        set_status("Fn + Space: unsupported current brightness.");
        return;
    }

    if (!get_combo_value(g_globalMode,
                         modes,
                         (int)(sizeof(modes) / sizeof(modes[0])),
                         mode,
                         sizeof(mode))) {
        set_status("Fn + Space: current keyboard mode is unavailable.");
        return;
    }

    /* Preserve each zone's current colour while changing the keyboard's
       brightness step. The Y720 mode remains keyboard-wide. */
    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (!get_combo_value(g_zoneColor[zone],
                             colors,
                             (int)(sizeof(colors) / sizeof(colors[0])),
                             zone_color[zone],
                             sizeof(zone_color[zone]))) {
            set_status("Fn + Space: current zone colour is unavailable.");
            return;
        }
    }

    set_status("Fn + Space: changing brightness...");

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (apply_zone_direct(zone, zone_color[zone], next, mode) != 0)
            return;
    }

    select_combo_value(g_globalBrightness,
                       next,
                       brightness,
                       (int)(sizeof(brightness) / sizeof(brightness[0])));

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
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

    save_current_state(_stricmp(next, "off") != 0, NULL);
    update_lighting_toggle_button(_stricmp(next, "off") != 0);
}


static void load_profiles(void)
{
    char config[MAX_PATH], profiles[4096], *p;
    DWORD length;
    int i, j;
    g_profile_count = 0;

    if (!get_config_path(config, sizeof(config))) return;

    ZeroMemory(profiles, sizeof(profiles));
    length = GetPrivateProfileStringA(NULL, NULL, "", profiles, sizeof(profiles), config);
    if (!length) return;

    p = profiles;
    while (*p && g_profile_count < MAX_PROFILES) {
        snprintf(g_profile_values[g_profile_count],
                 sizeof(g_profile_values[g_profile_count]), "%s", p);
        ++g_profile_count;
        p += strlen(p) + 1;
    }

    for (i = 1; i < g_profile_count; ++i) {
        char key[128];
        snprintf(key, sizeof(key), "%s", g_profile_values[i]);
        j = i - 1;
        while (j >= 0) {
            char a[128], b[128];
            make_profile_label(g_profile_values[j], a, sizeof(a));
            make_profile_label(key, b, sizeof(b));
            if (_stricmp(a, b) <= 0) break;
            snprintf(g_profile_values[j + 1], sizeof(g_profile_values[j + 1]), "%s", g_profile_values[j]);
            --j;
        }
        snprintf(g_profile_values[j + 1], sizeof(g_profile_values[j + 1]), "%s", key);
    }

    SendMessageA(g_profile, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_profile_count; ++i) {
        char display_profile[128];
        make_profile_label(g_profile_values[i], display_profile, sizeof(display_profile));
        SendMessageA(g_profile, CB_ADDSTRING, 0, (LPARAM)display_profile);
    }
    if (g_profile_count > 0) SendMessageA(g_profile, CB_SETCURSEL, 0, 0);
}



static HWND create_label(HWND parent, const char *text, int x, int y, int width, int height);
static HWND create_combo(HWND parent, int id, int x, int y, int width, int height);
static HWND create_button(HWND parent, const char *text, int id, int x, int y, int width, int height);
static void load_profiles(void);

typedef struct {
    HWND hwnd;
    HWND name, color, bright, mode;
    int accepted;
} profile_dialog_t;

static profile_dialog_t g_profile_dialog;

static void profile_dialog_close(int accepted)
{
    g_profile_dialog.accepted = accepted;
    if (g_profile_dialog.hwnd) DestroyWindow(g_profile_dialog.hwnd);
}

static void show_main_window(void);

static LRESULT CALLBACK ProfileDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_SHOW_EXISTING:
            show_main_window();
            return 0;

        case WM_CREATE: {
            int color_count = (int)(sizeof(colors) / sizeof(colors[0]));
            int bright_count = (int)(sizeof(brightness) / sizeof(brightness[0]));
            int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));
            create_label(hwnd, "Profile Name", 20, 20, 100, 24);
            g_profile_dialog.name = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                125, 18, 235, 26, hwnd, (HMENU)(INT_PTR)IDC_PROFILE_NAME,
                GetModuleHandleA(NULL), NULL);
            SendMessageA(g_profile_dialog.name, WM_SETFONT, (WPARAM)g_font, TRUE);
            create_label(hwnd, "Colour", 20, 60, 100, 24);
            g_profile_dialog.color = create_combo(hwnd, IDC_PROFILE_COLOR, 125, 56, 235, 300);
            add_combo_items(g_profile_dialog.color, color_labels, color_count);
            select_combo_value(g_profile_dialog.color, "crimson", colors, color_count);
            create_label(hwnd, "Brightness", 20, 100, 100, 24);
            g_profile_dialog.bright = create_combo(hwnd, IDC_PROFILE_BRIGHTNESS, 125, 96, 235, 300);
            add_combo_items(g_profile_dialog.bright, brightness_labels, bright_count);
            select_combo_value(g_profile_dialog.bright, "high", brightness, bright_count);
            create_label(hwnd, "Mode", 20, 140, 100, 24);
            g_profile_dialog.mode = create_combo(hwnd, IDC_PROFILE_MODE, 125, 136, 235, 300);
            add_combo_items(g_profile_dialog.mode, mode_labels, mode_count);
            select_combo_value(g_profile_dialog.mode, "always_on", modes, mode_count);
            create_button(hwnd, "Save Profile", IDC_PROFILE_SAVE, 125, 185, 110, 30);
            create_button(hwnd, "Cancel", IDC_PROFILE_CANCEL, 250, 185, 110, 30);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_PROFILE_CANCEL) { profile_dialog_close(0); return 0; }
            if (LOWORD(wParam) == IDC_PROFILE_SAVE) {
                char name[128], color[64], bright[32], mode[64], config[MAX_PATH];
                char display[128];
                GetWindowTextA(g_profile_dialog.name, name, sizeof(name));
                trim(name);
                if (!name[0]) { MessageBoxA(hwnd, "Please enter a profile name.", "Profile", MB_ICONWARNING); return 0; }
                for (int i = 0; name[i]; ++i) {
                    if (name[i] == '[' || name[i] == ']' || name[i] == '=' || name[i] == '\\' || name[i] == '/' || name[i] == '"') {
                        MessageBoxA(hwnd, "Profile name contains an unsupported character.", "Profile", MB_ICONWARNING); return 0;
                    }
                }
                if (!get_combo_value(g_profile_dialog.color, colors, (int)(sizeof(colors)/sizeof(colors[0])), color, sizeof(color)) ||
                    !get_combo_value(g_profile_dialog.bright, brightness, (int)(sizeof(brightness)/sizeof(brightness[0])), bright, sizeof(bright) ) ||
                    !get_combo_value(g_profile_dialog.mode, modes, (int)(sizeof(modes)/sizeof(modes[0])), mode, sizeof(mode)) ||
                    !get_config_path(config, sizeof(config))) return 0;
                if (GetPrivateProfileStringA(name, "color", "", display, sizeof(display), config) > 0) {
                    if (MessageBoxA(hwnd, "A profile with this name already exists. Replace it?", "Profile", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
                }
                WritePrivateProfileStringA(name, "color", color, config);
                WritePrivateProfileStringA(name, "brightness", bright, config);
                WritePrivateProfileStringA(name, "mode", mode, config);
                profile_dialog_close(1);
                return 0;
            }
            break;
        case WM_CLOSE: profile_dialog_close(0); return 0;
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
        CW_USEDEFAULT, CW_USEDEFAULT, 390, 260, owner, NULL, GetModuleHandleA(NULL), NULL);
    if (!g_profile_dialog.hwnd) return 0;
    EnableWindow(owner, FALSE);
    ShowWindow(g_profile_dialog.hwnd, SW_SHOW);
    SetForegroundWindow(g_profile_dialog.hwnd);
    while (IsWindow(g_profile_dialog.hwnd) && GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return g_profile_dialog.accepted;
}

static void create_profile(void)
{
    if (create_profile_dialog(g_hWnd)) {
        load_profiles();
        set_status("Profile created successfully.");
    }
}

static void delete_profile(void)
{
    int index;
    char config[MAX_PATH], display[128], message[256];
    index = (int)SendMessageA(g_profile, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR || index < 0 || index >= g_profile_count) { set_status("Please select a profile to delete."); return; }
    make_profile_label(g_profile_values[index], display, sizeof(display));
    snprintf(message, sizeof(message), "Delete profile '%s'?", display);
    if (MessageBoxA(g_hWnd, message, "Delete Profile", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!get_config_path(config, sizeof(config)) || !WritePrivateProfileStringA(g_profile_values[index], NULL, NULL, config)) {
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
    if (!g_trayIcon.hIcon) return 0;
    if (Shell_NotifyIconA(NIM_ADD, &g_trayIcon)) {
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
    if (g_trayAdded) {
        Shell_NotifyIconA(NIM_DELETE, &g_trayIcon);
        g_trayAdded = 0;
    }
}

static void show_main_window(void)
{
    if (!g_hWnd) return;
    ShowWindow(g_hWnd, SW_SHOW);
    ShowWindow(g_hWnd, SW_RESTORE);
    SetForegroundWindow(g_hWnd);
}

static void hide_to_tray(void)
{
    if (g_hWnd) ShowWindow(g_hWnd, SW_HIDE);
}

static void show_tray_menu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    POINT point;
    if (!menu) return;
    AppendMenuA(menu, MF_STRING, ID_TRAY_OFF, "Turn Lighting Off");
    AppendMenuA(menu, MF_STRING, ID_TRAY_ON, "Turn Lighting On");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_TRAY_EXIT, "Exit");
    if (!GetCursorPos(&point)) { DestroyMenu(menu); return; }
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

static HWND create_label(HWND parent, const char *text, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE,
                                x, y, width, height, parent, NULL,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd) SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hwnd;
}

static HWND create_group(HWND parent, const char *text, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(0, "BUTTON", text,
                                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                x, y, width, height, parent, NULL,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd) SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hwnd;
}

static HWND create_combo(HWND parent, int id, int x, int y, int width, int height)
{
    HWND hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                CBS_DROPDOWNLIST | WS_VSCROLL,
                                x, y, width, height, parent, (HMENU)(INT_PTR)id,
                                GetModuleHandleA(NULL), NULL);
    if (hwnd) SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hwnd;
}

static int is_accent_button(int id)
{
    return id == IDC_APPLY_ALL || id == IDC_APPLY_MODE ||
           id == IDC_SMOOTH || id == IDC_APPLY_PROFILE;
}

static HWND create_button(HWND parent, const char *text, int id,
                          int x, int y, int width, int height)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    HWND hwnd;

    /* Primary actions use an owner-drawn Legion-red treatment. Secondary
       actions keep the restrained dark Windows button style. */
    if (is_accent_button(id)) style |= BS_OWNERDRAW;

    hwnd = CreateWindowExA(0, "BUTTON", text, style,
                           x, y, width, height, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleA(NULL), NULL);
    if (hwnd) SendMessageA(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
    return hwnd;
}

static void draw_button(const DRAWITEMSTRUCT *item)
{
    RECT rect;
    HBRUSH brush;
    HPEN pen;
    char text[256];
    UINT state;
    COLORREF fill;
    COLORREF edge;
    COLORREF text_color;

    if (!item) return;
    rect = item->rcItem;
    state = item->itemState;

    if (is_accent_button((int)item->CtlID)) {
        fill = (state & ODS_SELECTED) ? CLR_RED_DARK : CLR_RED;
        edge = CLR_RED;
        text_color = RGB(255,255,255);
    } else {
        fill = CLR_BUTTON;
        edge = CLR_BUTTON_EDGE;
        text_color = CLR_TEXT;
    }

    brush = CreateSolidBrush(fill);
    FillRect(item->hDC, &rect, brush);
    DeleteObject(brush);

    pen = CreatePen(PS_SOLID, 1, edge);
    if (pen) {
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

    if (state & ODS_FOCUS) {
        RECT focus = rect;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item->hDC, &focus);
    }
}

static void create_controls(HWND hwnd)
{
    int color_count = (int)(sizeof(colors) / sizeof(colors[0]));
    int bright_count = (int)(sizeof(brightness) / sizeof(brightness[0]));
    int mode_count = (int)(sizeof(modes) / sizeof(modes[0]));
    int zone;
    HWND title;

    title = create_label(hwnd, "LEGION Y720 KEYBOARD BACKLIGHT", 30, 18, 700, 35);
    g_title = title;
    if (title) SendMessageA(title, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    create_label(hwnd, "Native Windows controller  |  Four-zone colour and brightness control", 32, 51, 760, 25);

    create_group(hwnd, "Global Lighting", 25, 82, 800, 145);
    create_label(hwnd, "Colour", 45, 115, 60, 24);
    g_globalColor = create_combo(hwnd, IDC_GLOBAL_COLOR, 105, 111, 210, 300);
    add_combo_items(g_globalColor, color_labels, color_count);
    select_combo_value(g_globalColor, "crimson", colors, color_count);

    create_label(hwnd, "Brightness", 335, 115, 85, 24);
    g_globalBrightness = create_combo(hwnd, IDC_GLOBAL_BRIGHTNESS, 420, 111, 130, 300);
    add_combo_items(g_globalBrightness, brightness_labels, bright_count);
    select_combo_value(g_globalBrightness, "high", brightness, bright_count);

    create_label(hwnd, "Keyboard Mode", 565, 115, 100, 24);
    g_globalMode = create_combo(hwnd, IDC_GLOBAL_MODE, 665, 111, 125, 300);
    add_combo_items(g_globalMode, mode_labels, mode_count);
    select_combo_value(g_globalMode, "always_on", modes, mode_count);

    create_button(hwnd, "Apply Colour + Brightness to All Zones", IDC_APPLY_ALL, 45, 155, 300, 34);
    g_lightingToggle = create_button(hwnd, "Turn Lighting Off", IDC_OFF, 360, 155, 180, 34);
    create_button(hwnd, "Apply Mode to Keyboard", IDC_APPLY_MODE, 555, 155, 235, 34);
    create_label(hwnd, "Mode is keyboard-wide on the Y720; colour and brightness remain zone-specific.", 45, 197, 740, 20);

    create_group(hwnd, "Smooth Lighting", 25, 240, 800, 75);
    create_label(hwnd, "Cycles colours automatically, starting from the selected global colour and brightness.",
                 45, 264, 600, 25);
    create_button(hwnd, "Start Smooth", IDC_SMOOTH, 660, 258, 130, 30);

    create_group(hwnd, "Individual Zone Colour and Brightness", 25, 330, 800, 245);
    create_label(hwnd, "Zone", 45, 360, 65, 22);
    create_label(hwnd, "Keyboard Area", 115, 360, 180, 22);
    create_label(hwnd, "Colour", 305, 360, 130, 22);
    create_label(hwnd, "Brightness", 495, 360, 100, 22);

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        int y = 386 + zone * 42;
        char zone_number[32];
        snprintf(zone_number, sizeof(zone_number), "Zone %d", zone);
        create_label(hwnd, zone_number, 45, y + 7, 65, 25);
        create_label(hwnd, zone_names[zone], 115, y + 7, 180, 25);

        g_zoneColor[zone] = create_combo(hwnd, IDC_ZONE_BASE + zone * 10 + 1, 305, y, 170, 280);
        add_combo_items(g_zoneColor[zone], color_labels, color_count);
        select_combo_value(g_zoneColor[zone], "crimson", colors, color_count);

        g_zoneBrightness[zone] = create_combo(hwnd, IDC_ZONE_BASE + zone * 10 + 2, 495, y, 125, 280);
        add_combo_items(g_zoneBrightness[zone], brightness_labels, bright_count);
        select_combo_value(g_zoneBrightness[zone], "high", brightness, bright_count);

        create_button(hwnd, "Apply", IDC_ZONE_BASE + zone * 10 + 4, 650, y, 120, 27);
    }

    create_group(hwnd, "Profiles", 25, 590, 800, 75);
    create_label(hwnd, "Profile", 45, 616, 60, 24);
    g_profile = create_combo(hwnd, IDC_PROFILE, 110, 612, 260, 300);
    load_profiles();
    create_button(hwnd, "Apply", IDC_APPLY_PROFILE, 380, 612, 85, 30);
    create_button(hwnd, "New", IDC_PROFILE_NEW, 475, 612, 70, 30);
    create_button(hwnd, "Delete", IDC_PROFILE_DELETE, 555, 612, 70, 30);
    g_startup = CreateWindowExA(0, "BUTTON", "Start with Windows and Restore",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                630, 612, 195, 30, hwnd, (HMENU)(INT_PTR)IDC_STARTUP,
                                GetModuleHandleA(NULL), NULL);
    if (g_startup) {
        SendMessageA(g_startup, WM_SETFONT, (WPARAM)g_font, TRUE);
        update_startup_checkbox();
    }

    load_state_into_controls();

    create_label(hwnd, "Fn + Space: cycles Off -> Low -> Medium -> Ultra -> Off. The GUI must remain running in the system tray for this keyboard shortcut to work.", 30, 670, 795, 32);
    create_label(hwnd, "Status", 30, 705, 55, 24);
    g_status = create_label(hwnd, "Ready.", 85, 705, 740, 24);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreated && message == g_taskbarCreated) {
        g_trayAdded = 0;
        add_tray_icon(hwnd);
        return 0;
    }

    switch (message) {
        case WM_CREATE:
            create_controls(hwnd);
            if (!add_tray_icon(hwnd)) set_status("Warning: unable to create system tray icon.");
            return 0;

        case WM_INPUT:
            if (y720_input_handle_message(message, wParam, lParam))
                return 0;
            break;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int zone;
            if (id == IDC_APPLY_ALL) { apply_all_zones(); return 0; }
            if (id == IDC_APPLY_MODE) { apply_keyboard_mode(); return 0; }
            if (id == IDC_SMOOTH) { apply_smooth(); return 0; }
            if (id == IDC_OFF) { toggle_lighting(); return 0; }
            if (id == IDC_APPLY_PROFILE) { apply_profile(); return 0; }
            if (id == IDC_PROFILE_NEW) { create_profile(); return 0; }
            if (id == IDC_PROFILE_DELETE) { delete_profile(); return 0; }
            if (id == IDC_STARTUP && HIWORD(wParam) == BN_CLICKED) {
                int enabled = (int)SendMessageA(g_startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (!set_startup_enabled(enabled)) {
                    SendMessageA(g_startup, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
                    set_status("Unable to change the Windows startup setting.");
                } else {
                    set_status(enabled ? "Start with Windows enabled; last lighting state will be restored."
                                        : "Start with Windows disabled.");
                }
                return 0;
            }
            if (id == ID_TRAY_OFF) { turn_off(); return 0; }
            if (id == ID_TRAY_ON) { turn_on_from_last_state(); return 0; }
            if (id == ID_TRAY_EXIT) { exit_application(hwnd); return 0; }
            for (zone = 0; zone < ZONE_COUNT; ++zone) {
                if (id == IDC_ZONE_BASE + zone * 10 + 4) { apply_zone(zone); return 0; }
            }
            break;
        }

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) { hide_to_tray(); return 0; }
            break;

        case WM_DRAWITEM:
            draw_button((const DRAWITEMSTRUCT *)lParam);
            return TRUE;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wParam;
            HWND control = (HWND)lParam;
            if (control == g_title) SetTextColor(dc, CLR_RED);
            else SetTextColor(dc, CLR_TEXT);
            SetBkColor(dc, CLR_BG);
            if (message == WM_CTLCOLORBTN) return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            return (LRESULT)g_bgBrush;
        }

        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, CLR_TEXT);
            SetBkColor(dc, CLR_CONTROL);
            return (LRESULT)g_controlBrush;
        }

        case WM_TRAYICON: {
            UINT event = LOWORD(lParam);
            switch (event) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK: show_main_window(); return 0;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: show_tray_menu(hwnd); return 0;
            }
            break;
        }

        case WM_CLOSE:
            if (!g_exiting) { hide_to_tray(); return 0; }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            remove_tray_icon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command)
{
    WNDCLASSA wc;
    MSG message;
    int startup_launch = 0;
    (void)previous;
    if (command_line && strstr(command_line, "--startup"))
        startup_launch = 1;

    g_single_instance_mutex = CreateMutexA(NULL, TRUE, "Local\\LegionY720BacklightController");
    if (!g_single_instance_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowA(CLASS_NAME, NULL);
        if (existing) PostMessageA(existing, WM_SHOW_EXISTING, 0, 0);
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = NULL;
        return 0;
    }

    g_appIcon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(IDI_Y720_KEYBOARD), IMAGE_ICON,
                                  32, 32, LR_DEFAULTSIZE);
    if (!g_appIcon) g_appIcon = LoadIconA(NULL, IDI_APPLICATION);
    g_taskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    g_font = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_fontBold = CreateFontA(21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Unable to register GUI window class.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    g_hWnd = CreateWindowExA(0, CLASS_NAME, APP_TITLE,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 865, 770,
                             NULL, NULL, instance, NULL);
    if (!g_hWnd) {
        MessageBoxA(NULL, "Unable to create GUI window.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    if (g_appIcon) {
        SendMessageA(g_hWnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        SendMessageA(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon);
    }

    if (!y720_input_init(g_hWnd, handle_fn_space, NULL))
        set_status("Warning: Fn + Space input could not be initialized.");

    repair_startup_path_if_enabled();

    if (startup_launch && is_startup_enabled()) {
        ShowWindow(g_hWnd, SW_HIDE);
        UpdateWindow(g_hWnd);
        restore_saved_state();
    } else {
        ShowWindow(g_hWnd, show_command);
        UpdateWindow(g_hWnd);
    }

    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    y720_input_shutdown();
    remove_tray_icon();
    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_bgBrush) DeleteObject(g_bgBrush);
    if (g_controlBrush) DeleteObject(g_controlBrush);
    if (g_appIcon) DestroyIcon(g_appIcon);
    if (g_single_instance_mutex) CloseHandle(g_single_instance_mutex);
    return (int)message.wParam;
}
