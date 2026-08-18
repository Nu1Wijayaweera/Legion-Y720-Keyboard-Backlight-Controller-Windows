#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_TITLE "Legion Y720 Keyboard Backlight Controller"
#define CLASS_NAME "Y720BacklightGUIClass"

/* =========================================================
   Control IDs
   ========================================================= */

#define IDC_GLOBAL_COLOR       1001
#define IDC_GLOBAL_BRIGHTNESS  1002
#define IDC_GLOBAL_MODE        1003
#define IDC_APPLY_ALL          1004
#define IDC_OFF                1005

#define IDC_PROFILE            1006
#define IDC_APPLY_PROFILE      1007

#define IDC_ZONE_BASE          2000

#define IDC_STATUS             3000

/* =========================================================
   Tray
   ========================================================= */

#define WM_TRAYICON            (WM_APP + 1)

#define TRAY_ICON_ID           5001

#define IDI_Y720_KEYBOARD      101

#define ID_TRAY_OFF            5003
#define ID_TRAY_ON             5004
#define ID_TRAY_EXIT           5005

#define ZONE_COUNT 4

/* =========================================================
   Global state
   ========================================================= */

static HWND g_hWnd;

static HWND g_globalColor;
static HWND g_globalBrightness;
static HWND g_globalMode;

static HWND g_profile;
static HWND g_status;

static HWND g_zoneColor[ZONE_COUNT];
static HWND g_zoneBrightness[ZONE_COUNT];
static HWND g_zoneMode[ZONE_COUNT];
static HWND g_zoneButton[ZONE_COUNT];

static HFONT g_font;
static HFONT g_fontBold;

/* Tray state */

static NOTIFYICONDATAA g_trayIcon;
static int g_trayAdded = 0;
static int g_exiting = 0;

static UINT g_taskbarCreated = 0;

static HICON g_appIcon = NULL;


/* =========================================================
   Data
   ========================================================= */

static const char *colors[] = {
    "crimson",
    "torch_red",
    "hollywood_cerise",
    "magenta",
    "electric_violet",
    "electric_violet_2",
    "blue",
    "blue_ribbon",
    "azure_radiance",
    "cyan",
    "spring_green",
    "spring_green_2",
    "green",
    "bright_green",
    "lime",
    "yellow",
    "web_orange",
    "international_orange",
    "white",
    "nocolor"
};

static const char *brightness[] = {
    "off",
    "low",
    "medium",
    "high",
    "ultra",
    "enough"
};

static const char *modes[] = {
    "heartbeat",
    "breath",
    "smooth",
    "always_on",
    "wave"
};

static const char *zone_names[] = {
    "Caps Lock -> D",
    "F -> K",
    "L -> Enter",
    "Numeric Keypad"
};


/* =========================================================
   Utility
   ========================================================= */

static void set_status(const char *text)
{
    if (g_status)
        SetWindowTextA(g_status, text);
}


static void add_combo_items(
    HWND combo,
    const char **items,
    int count)
{
    int i;

    if (!combo)
        return;

    for (i = 0; i < count; i++) {

        SendMessageA(
            combo,
            CB_ADDSTRING,
            0,
            (LPARAM)items[i]
        );
    }
}


static void select_combo(
    HWND combo,
    const char *text)
{
    int count;
    int i;

    if (!combo || !text)
        return;

    count =
        (int)SendMessageA(
            combo,
            CB_GETCOUNT,
            0,
            0
        );

    for (i = 0; i < count; i++) {

        char buffer[128];

        ZeroMemory(
            buffer,
            sizeof(buffer)
        );

        SendMessageA(
            combo,
            CB_GETLBTEXT,
            i,
            (LPARAM)buffer
        );

        if (_stricmp(buffer, text) == 0) {

            SendMessageA(
                combo,
                CB_SETCURSEL,
                i,
                0
            );

            return;
        }
    }
}


static void get_combo_text(
    HWND combo,
    char *buffer,
    int buffer_size)
{
    int index;

    if (!buffer || buffer_size <= 0)
        return;

    buffer[0] = '\0';

    if (!combo)
        return;

    index =
        (int)SendMessageA(
            combo,
            CB_GETCURSEL,
            0,
            0
        );

    if (index == CB_ERR)
        return;

    SendMessageA(
        combo,
        CB_GETLBTEXT,
        index,
        (LPARAM)buffer
    );

    buffer[buffer_size - 1] = '\0';
}


static void trim(char *s)
{
    char *start;
    char *end;

    if (!s)
        return;

    start = s;

    while (*start == ' ' ||
           *start == '\t' ||
           *start == '\r' ||
           *start == '\n')
        start++;

    if (start != s) {

        memmove(
            s,
            start,
            strlen(start) + 1
        );
    }

    end = s + strlen(s);

    while (end > s &&
           (end[-1] == ' ' ||
            end[-1] == '\t' ||
            end[-1] == '\r' ||
            end[-1] == '\n'))
        end--;

    *end = '\0';
}


/* =========================================================
   Executable / paths
   ========================================================= */

static int get_exe_directory(
    char *buffer,
    DWORD buffer_size)
{
    DWORD length;
    char *slash;

    if (!buffer || buffer_size == 0)
        return 0;

    length =
        GetModuleFileNameA(
            NULL,
            buffer,
            buffer_size
        );

    if (!length ||
        length >= buffer_size)
        return 0;

    slash =
        strrchr(buffer, '\\');

    if (!slash)
        slash =
            strrchr(buffer, '/');

    if (!slash)
        return 0;

    *slash = '\0';

    return 1;
}


static int get_cli_path(
    char *buffer,
    DWORD buffer_size)
{
    char directory[MAX_PATH];

    if (!get_exe_directory(
            directory,
            sizeof(directory)))
        return 0;

    snprintf(
        buffer,
        buffer_size,
        "%s\\Y720Backlight.exe",
        directory
    );

    buffer[buffer_size - 1] = '\0';

    return 1;
}


static int get_config_path(
    char *buffer,
    DWORD buffer_size)
{
    char directory[MAX_PATH];

    if (!get_exe_directory(
            directory,
            sizeof(directory)))
        return 0;

    snprintf(
        buffer,
        buffer_size,
        "%s\\Y720Backlight.ini",
        directory
    );

    buffer[buffer_size - 1] = '\0';

    return 1;
}


/* =========================================================
   Backend
   ========================================================= */

static int run_backend(
    const char *arguments)
{
    char exe[MAX_PATH];
    char command[2048];

    STARTUPINFOA startup;
    PROCESS_INFORMATION process;

    if (!arguments)
        return -1;

    if (!get_cli_path(
            exe,
            sizeof(exe))) {

        set_status(
            "Unable to locate Y720Backlight.exe."
        );

        return -1;
    }

    snprintf(
        command,
        sizeof(command),
        "\"%s\" %s",
        exe,
        arguments
    );

    command[sizeof(command) - 1] = '\0';

    ZeroMemory(
        &startup,
        sizeof(startup)
    );

    ZeroMemory(
        &process,
        sizeof(process)
    );

    startup.cb = sizeof(startup);

    startup.dwFlags =
        STARTF_USESHOWWINDOW;

    startup.wShowWindow =
        SW_HIDE;

    if (!CreateProcessA(
            NULL,
            command,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &process)) {

        char error[256];

        snprintf(
            error,
            sizeof(error),
            "Unable to start Y720Backlight.exe (error %lu).",
            GetLastError()
        );

        set_status(error);

        return -1;
    }

    WaitForSingleObject(
        process.hProcess,
        INFINITE
    );

    DWORD exit_code = 1;

    GetExitCodeProcess(
        process.hProcess,
        &exit_code
    );

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (exit_code != 0) {

        set_status(
            "Command failed. Check that the Y720 lighting device is available."
        );

        return -1;
    }

    return 0;
}


/* =========================================================
   Global lighting
   ========================================================= */

static void apply_all_zones(void)
{
    char color[128];
    char bright[128];
    char mode[128];
    char args[512];

    get_combo_text(
        g_globalColor,
        color,
        sizeof(color)
    );

    get_combo_text(
        g_globalBrightness,
        bright,
        sizeof(bright)
    );

    get_combo_text(
        g_globalMode,
        mode,
        sizeof(mode)
    );

    if (!color[0] ||
        !bright[0] ||
        !mode[0]) {

        set_status(
            "Please select color, brightness and mode."
        );

        return;
    }

    snprintf(
        args,
        sizeof(args),
        "set %s %s %s",
        color,
        bright,
        mode
    );

    set_status(
        "Applying settings to all zones..."
    );

    if (run_backend(args) == 0) {

        char status[256];

        snprintf(
            status,
            sizeof(status),
            "All zones: %s / %s / %s",
            color,
            bright,
            mode
        );

        set_status(status);
    }
}


static void turn_off(void)
{
    set_status(
        "Turning keyboard lighting off..."
    );

    if (run_backend("off") == 0)
        set_status(
            "Keyboard lighting is off."
        );
}


/* =========================================================
   Individual zone
   ========================================================= */

static void apply_zone(int zone)
{
    char color[128];
    char bright[128];
    char mode[128];
    char args[512];
    char status[256];

    if (zone < 0 ||
        zone >= ZONE_COUNT)
        return;

    get_combo_text(
        g_zoneColor[zone],
        color,
        sizeof(color)
    );

    get_combo_text(
        g_zoneBrightness[zone],
        bright,
        sizeof(bright)
    );

    get_combo_text(
        g_zoneMode[zone],
        mode,
        sizeof(mode)
    );

    if (!color[0] ||
        !bright[0] ||
        !mode[0]) {

        set_status(
            "Please select color, brightness and mode."
        );

        return;
    }

    snprintf(
        args,
        sizeof(args),
        "zone %d %s %s %s",
        zone,
        color,
        bright,
        mode
    );

    snprintf(
        status,
        sizeof(status),
        "Applying settings to Zone %d...",
        zone
    );

    set_status(status);

    if (run_backend(args) == 0) {

        snprintf(
            status,
            sizeof(status),
            "Zone %d: %s / %s / %s",
            zone,
            color,
            bright,
            mode
        );

        set_status(status);
    }
}


/* =========================================================
   Profiles
   ========================================================= */

static void apply_profile(void)
{
    char profile[128];
    char config[MAX_PATH];

    char color[128] = "";
    char bright[128] = "";
    char mode[128] = "";

    get_combo_text(
        g_profile,
        profile,
        sizeof(profile)
    );

    if (!profile[0]) {

        set_status(
            "Please select a profile."
        );

        return;
    }

    if (!get_config_path(
            config,
            sizeof(config))) {

        set_status(
            "Could not locate configuration file."
        );

        return;
    }

    GetPrivateProfileStringA(
        profile,
        "color",
        "",
        color,
        sizeof(color),
        config
    );

    GetPrivateProfileStringA(
        profile,
        "brightness",
        "",
        bright,
        sizeof(bright),
        config
    );

    GetPrivateProfileStringA(
        profile,
        "mode",
        "",
        mode,
        sizeof(mode),
        config
    );

    trim(color);
    trim(bright);
    trim(mode);

    if (!color[0] ||
        !bright[0] ||
        !mode[0]) {

        char error[256];

        snprintf(
            error,
            sizeof(error),
            "Profile '%s' is incomplete.",
            profile
        );

        set_status(error);

        return;
    }

    select_combo(
        g_globalColor,
        color
    );

    select_combo(
        g_globalBrightness,
        bright
    );

    select_combo(
        g_globalMode,
        mode
    );

    char args[512];

    snprintf(
        args,
        sizeof(args),
        "set %s %s %s",
        color,
        bright,
        mode
    );

    char status[256];

    snprintf(
        status,
        sizeof(status),
        "Applying profile '%s'...",
        profile
    );

    set_status(status);

    if (run_backend(args) == 0) {

        snprintf(
            status,
            sizeof(status),
            "Profile '%s' applied: %s / %s / %s",
            profile,
            color,
            bright,
            mode
        );

        set_status(status);
    }
}


static void load_profiles(void)
{
    char config[MAX_PATH];
    char profiles[4096];

    if (!get_config_path(
            config,
            sizeof(config)))
        return;

    ZeroMemory(
        profiles,
        sizeof(profiles)
    );

    DWORD length =
        GetPrivateProfileStringA(
            NULL,
            NULL,
            "",
            profiles,
            sizeof(profiles),
            config
        );

    if (!length)
        return;

    char *p = profiles;

    while (*p) {

        SendMessageA(
            g_profile,
            CB_ADDSTRING,
            0,
            (LPARAM)p
        );

        p += strlen(p) + 1;
    }

    if (SendMessageA(
            g_profile,
            CB_GETCOUNT,
            0,
            0) > 0) {

        SendMessageA(
            g_profile,
            CB_SETCURSEL,
            0,
            0
        );
    }
}


/* =========================================================
   Tray icon
   ========================================================= */

static void setup_tray_data(HWND hwnd)
{
    ZeroMemory(
        &g_trayIcon,
        sizeof(g_trayIcon)
    );

    g_trayIcon.cbSize =
        sizeof(g_trayIcon);

    g_trayIcon.hWnd =
        hwnd;

    g_trayIcon.uID =
        TRAY_ICON_ID;

    g_trayIcon.uFlags =
        NIF_MESSAGE |
        NIF_ICON |
        NIF_TIP;

    g_trayIcon.uCallbackMessage =
        WM_TRAYICON;

    /*
     * Use the embedded application icon.
     */
    g_trayIcon.hIcon =
        g_appIcon;

    lstrcpynA(
        g_trayIcon.szTip,
        "Legion Y720 Keyboard Backlight",
        sizeof(g_trayIcon.szTip)
    );
}


static int add_tray_icon(HWND hwnd)
{
    setup_tray_data(hwnd);

    if (!g_trayIcon.hIcon)
        return 0;

    if (Shell_NotifyIconA(
            NIM_ADD,
            &g_trayIcon)) {

        /*
         * Windows recommends setting the notification icon
         * behavior explicitly after NIM_ADD.
         */
        g_trayIcon.uVersion =
            NOTIFYICON_VERSION_4;

        if (!Shell_NotifyIconA(
                NIM_SETVERSION,
                &g_trayIcon)) {

            /*
             * If version 4 isn't accepted, the icon still
             * exists. We can continue using normal behavior.
             */
            g_trayIcon.uVersion = 0;
        }

        g_trayAdded = 1;

        return 1;
    }

    g_trayAdded = 0;

    return 0;
}


static void remove_tray_icon(void)
{
    if (!g_trayAdded)
        return;

    Shell_NotifyIconA(
        NIM_DELETE,
        &g_trayIcon
    );

    g_trayAdded = 0;
}


static void show_main_window(void)
{
    if (!g_hWnd)
        return;

    ShowWindow(
        g_hWnd,
        SW_SHOW
    );

    ShowWindow(
        g_hWnd,
        SW_RESTORE
    );

    SetForegroundWindow(
        g_hWnd
    );
}


static void hide_to_tray(void)
{
    if (!g_hWnd)
        return;

    ShowWindow(
        g_hWnd,
        SW_HIDE
    );
}


static void show_tray_menu(HWND hwnd)
{
    HMENU menu;

    menu =
        CreatePopupMenu();

    if (!menu)
        return;

    AppendMenuA(
        menu,
        MF_STRING,
        ID_TRAY_OFF,
        "Turn Lighting Off"
    );

    AppendMenuA(
        menu,
        MF_STRING,
        ID_TRAY_ON,
        "Turn Lighting On"
    );

    AppendMenuA(
        menu,
        MF_SEPARATOR,
        0,
        NULL
    );

    AppendMenuA(
        menu,
        MF_STRING,
        ID_TRAY_EXIT,
        "Exit"
    );

    POINT point;

    if (!GetCursorPos(&point)) {

        DestroyMenu(menu);

        return;
    }

    /*
     * Make the hidden controller the foreground owner.
     */
    SetForegroundWindow(hwnd);

    /*
     * Keep the popup above the taskbar.
     */
    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON |
        TPM_BOTTOMALIGN |
        TPM_LEFTALIGN |
        TPM_NOANIMATION,
        point.x,
        point.y,
        0,
        hwnd,
        NULL
    );

    PostMessageA(
        hwnd,
        WM_NULL,
        0,
        0
    );

    DestroyMenu(menu);
}


static void exit_application(HWND hwnd)
{
    g_exiting = 1;

    remove_tray_icon();

    DestroyWindow(hwnd);
}


/* =========================================================
   Control creation
   ========================================================= */

static HWND create_label(
    HWND parent,
    const char *text,
    int x,
    int y,
    int width,
    int height)
{
    HWND hwnd =
        CreateWindowExA(
            0,
            "STATIC",
            text,
            WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            parent,
            NULL,
            GetModuleHandleA(NULL),
            NULL
        );

    if (hwnd) {

        SendMessageA(
            hwnd,
            WM_SETFONT,
            (WPARAM)g_font,
            TRUE
        );
    }

    return hwnd;
}


static HWND create_group(
    HWND parent,
    const char *text,
    int x,
    int y,
    int width,
    int height)
{
    HWND hwnd =
        CreateWindowExA(
            0,
            "BUTTON",
            text,
            WS_CHILD |
            WS_VISIBLE |
            BS_GROUPBOX,
            x,
            y,
            width,
            height,
            parent,
            NULL,
            GetModuleHandleA(NULL),
            NULL
        );

    if (hwnd) {

        SendMessageA(
            hwnd,
            WM_SETFONT,
            (WPARAM)g_fontBold,
            TRUE
        );
    }

    return hwnd;
}


static HWND create_combo(
    HWND parent,
    int id,
    int x,
    int y,
    int width,
    int height)
{
    HWND hwnd =
        CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "COMBOBOX",
            "",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            CBS_DROPDOWNLIST |
            WS_VSCROLL,
            x,
            y,
            width,
            height,
            parent,
            (HMENU)(INT_PTR)id,
            GetModuleHandleA(NULL),
            NULL
        );

    if (hwnd) {

        SendMessageA(
            hwnd,
            WM_SETFONT,
            (WPARAM)g_font,
            TRUE
        );
    }

    return hwnd;
}


static HWND create_button(
    HWND parent,
    const char *text,
    int id,
    int x,
    int y,
    int width,
    int height)
{
    HWND hwnd =
        CreateWindowExA(
            0,
            "BUTTON",
            text,
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            BS_PUSHBUTTON,
            x,
            y,
            width,
            height,
            parent,
            (HMENU)(INT_PTR)id,
            GetModuleHandleA(NULL),
            NULL
        );

    if (hwnd) {

        SendMessageA(
            hwnd,
            WM_SETFONT,
            (WPARAM)g_font,
            TRUE
        );
    }

    return hwnd;
}


/* =========================================================
   Create GUI
   ========================================================= */

static void create_controls(HWND hwnd)
{
    const int color_count =
        sizeof(colors) / sizeof(colors[0]);

    const int brightness_count =
        sizeof(brightness) / sizeof(brightness[0]);

    const int mode_count =
        sizeof(modes) / sizeof(modes[0]);

    /*
     * Header
     */

    HWND title =
        create_label(
            hwnd,
            "Legion Y720 Keyboard Backlight",
            30,
            20,
            700,
            35
        );

    if (title) {

        SendMessageA(
            title,
            WM_SETFONT,
            (WPARAM)g_fontBold,
            TRUE
        );
    }

    create_label(
        hwnd,
        "Native Windows controller with independent four-zone lighting.",
        32,
        53,
        720,
        25
    );


    /*
     * Global lighting
     */

    create_group(
        hwnd,
        "Global Lighting",
        25,
        85,
        800,
        120
    );

    create_label(
        hwnd,
        "Color",
        45,
        118,
        70,
        24
    );

    g_globalColor =
        create_combo(
            hwnd,
            IDC_GLOBAL_COLOR,
            105,
            114,
            210,
            300
        );

    add_combo_items(
        g_globalColor,
        colors,
        color_count
    );

    select_combo(
        g_globalColor,
        "crimson"
    );


    create_label(
        hwnd,
        "Brightness",
        335,
        118,
        85,
        24
    );

    g_globalBrightness =
        create_combo(
            hwnd,
            IDC_GLOBAL_BRIGHTNESS,
            420,
            114,
            130,
            300
        );

    add_combo_items(
        g_globalBrightness,
        brightness,
        brightness_count
    );

    select_combo(
        g_globalBrightness,
        "high"
    );


    create_label(
        hwnd,
        "Mode",
        570,
        118,
        55,
        24
    );

    g_globalMode =
        create_combo(
            hwnd,
            IDC_GLOBAL_MODE,
            625,
            114,
            145,
            300
        );

    add_combo_items(
        g_globalMode,
        modes,
        mode_count
    );

    select_combo(
        g_globalMode,
        "always_on"
    );


    create_button(
        hwnd,
        "Apply to All Zones",
        IDC_APPLY_ALL,
        45,
        158,
        230,
        34
    );

    create_button(
        hwnd,
        "Turn Lighting Off",
        IDC_OFF,
        285,
        158,
        180,
        34
    );


    /*
     * Individual zones
     */

    create_group(
        hwnd,
        "Individual Zones",
        25,
        220,
        800,
        250
    );

    create_label(
        hwnd,
        "Zone",
        45,
        252,
        70,
        22
    );

    create_label(
        hwnd,
        "Keyboard Area",
        115,
        252,
        180,
        22
    );

    create_label(
        hwnd,
        "Color",
        305,
        252,
        130,
        22
    );

    create_label(
        hwnd,
        "Brightness",
        445,
        252,
        100,
        22
    );

    create_label(
        hwnd,
        "Mode",
        555,
        252,
        110,
        22
    );


    int zone;

    for (zone = 0;
         zone < ZONE_COUNT;
         zone++) {

        int y =
            278 + zone * 43;

        char zone_number[32];

        snprintf(
            zone_number,
            sizeof(zone_number),
            "Zone %d",
            zone
        );

        create_label(
            hwnd,
            zone_number,
            45,
            y + 7,
            65,
            25
        );

        create_label(
            hwnd,
            zone_names[zone],
            115,
            y + 7,
            180,
            25
        );


        g_zoneColor[zone] =
            create_combo(
                hwnd,
                IDC_ZONE_BASE + zone * 10 + 1,
                305,
                y,
                130,
                280
            );

        add_combo_items(
            g_zoneColor[zone],
            colors,
            color_count
        );

        select_combo(
            g_zoneColor[zone],
            "crimson"
        );


        g_zoneBrightness[zone] =
            create_combo(
                hwnd,
                IDC_ZONE_BASE + zone * 10 + 2,
                445,
                y,
                100,
                280
            );

        add_combo_items(
            g_zoneBrightness[zone],
            brightness,
            brightness_count
        );

        select_combo(
            g_zoneBrightness[zone],
            "high"
        );


        g_zoneMode[zone] =
            create_combo(
                hwnd,
                IDC_ZONE_BASE + zone * 10 + 3,
                555,
                y,
                110,
                280
            );

        add_combo_items(
            g_zoneMode[zone],
            modes,
            mode_count
        );

        select_combo(
            g_zoneMode[zone],
            "always_on"
        );


        g_zoneButton[zone] =
            create_button(
                hwnd,
                "Apply",
                IDC_ZONE_BASE + zone * 10 + 4,
                675,
                y,
                105,
                27
            );
    }


    /*
     * Profiles
     */

    create_group(
        hwnd,
        "Profiles",
        25,
        485,
        800,
        100
    );

    create_label(
        hwnd,
        "Profile",
        45,
        520,
        70,
        24
    );

    g_profile =
        create_combo(
            hwnd,
            IDC_PROFILE,
            110,
            516,
            300,
            300
        );

    load_profiles();

    create_button(
        hwnd,
        "Apply Profile",
        IDC_APPLY_PROFILE,
        430,
        516,
        160,
        30
    );


    /*
     * Status
     */

    create_label(
        hwnd,
        "Status",
        30,
        610,
        55,
        24
    );

    g_status =
        create_label(
            hwnd,
            "Ready.",
            85,
            610,
            740,
            24
        );
}


/* =========================================================
   Window procedure
   ========================================================= */

static LRESULT CALLBACK WndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    /*
     * Explorer/taskbar restart notification.
     */
    if (g_taskbarCreated &&
        message == g_taskbarCreated) {

        g_trayAdded = 0;

        add_tray_icon(hwnd);

        return 0;
    }


    switch (message) {

        case WM_CREATE:
        {
            create_controls(hwnd);

            /*
             * Add the tray icon once the window exists.
             */
            if (!add_tray_icon(hwnd)) {

                set_status(
                    "Warning: unable to create system tray icon."
                );
            }

            return 0;
        }


        case WM_COMMAND:
        {
            int id =
                LOWORD(wParam);


            /*
             * Main GUI buttons
             */

            if (id == IDC_APPLY_ALL) {

                apply_all_zones();

                return 0;
            }

            if (id == IDC_OFF) {

                turn_off();

                return 0;
            }

            if (id == IDC_APPLY_PROFILE) {

                apply_profile();

                return 0;
            }


            /*
             * Tray menu
             */

            if (id == ID_TRAY_OFF) {

                turn_off();

                return 0;
            }

            if (id == ID_TRAY_ON) {

                apply_all_zones();

                return 0;
            }

            if (id == ID_TRAY_EXIT) {

                exit_application(hwnd);

                return 0;
            }


            /*
             * Individual zone buttons
             */

            int zone;

            for (zone = 0;
                 zone < ZONE_COUNT;
                 zone++) {

                int button_id =
                    IDC_ZONE_BASE +
                    zone * 10 +
                    4;

                if (id == button_id) {

                    apply_zone(zone);

                    return 0;
                }
            }

            break;
        }


        /*
         * Minimize -> tray.
         */

        case WM_SIZE:

            if (wParam == SIZE_MINIMIZED) {

                hide_to_tray();

                return 0;
            }

            break;


        /*
         * Tray notification.
         *
         * IMPORTANT:
         *
         * Because we use NOTIFYICON_VERSION_4,
         * Windows places the event in LOWORD(lParam).
         */

        case WM_TRAYICON:
        {
            UINT event =
                LOWORD(lParam);

            UINT icon_id =
                HIWORD(lParam);

            /*
             * For version 4, the icon ID is in HIWORD(lParam).
             *
             * Keep the wParam check as a compatibility fallback.
             */
            if (icon_id != 0 &&
                icon_id != TRAY_ICON_ID &&
                (UINT)wParam != TRAY_ICON_ID)
                break;

            switch (event) {

                case WM_LBUTTONDBLCLK:

                    show_main_window();

                    return 0;


                case WM_LBUTTONUP:

                    /*
                     * Single left click restores the controller.
                     */
                    show_main_window();

                    return 0;


                case WM_RBUTTONUP:

                    show_tray_menu(hwnd);

                    return 0;


                case WM_CONTEXTMENU:

                    show_tray_menu(hwnd);

                    return 0;
            }

            break;
        }


        /*
         * Closing the GUI hides it to the tray.
         */
        case WM_CLOSE:

            if (!g_exiting) {

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

    return DefWindowProcA(
        hwnd,
        message,
        wParam,
        lParam
    );
}


/* =========================================================
   Windows entry point
   ========================================================= */

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous,
    LPSTR command_line,
    int show_command)
{
    (void)previous;
    (void)command_line;


    /*
     * Load the embedded keyboard icon.
     */
    g_appIcon =
        (HICON)LoadImageA(
            instance,
            MAKEINTRESOURCEA(IDI_Y720_KEYBOARD),
            IMAGE_ICON,
            32,
            32,
            LR_DEFAULTSIZE
        );

    if (!g_appIcon) {

        /*
         * Fallback if the embedded resource cannot be loaded.
         */
        g_appIcon =
            LoadIconA(
                NULL,
                IDI_APPLICATION
            );
    }


    /*
     * Register the Windows Explorer/taskbar restart message.
     */
    g_taskbarCreated =
        RegisterWindowMessageA(
            "TaskbarCreated"
        );


    /*
     * Fonts
     */

    g_font =
        CreateFontA(
            16,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            "Segoe UI"
        );

    g_fontBold =
        CreateFontA(
            21,
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            "Segoe UI"
        );


    /*
     * Window class
     */

    WNDCLASSA wc;

    ZeroMemory(
        &wc,
        sizeof(wc)
    );

    wc.style =
        CS_HREDRAW |
        CS_VREDRAW;

    wc.lpfnWndProc =
        WndProc;

    wc.hInstance =
        instance;

    /*
     * The embedded keyboard icon is used for both
     * the title bar and the system tray.
     */
    wc.hIcon =
        g_appIcon;

    wc.hCursor =
        LoadCursorA(
            NULL,
            IDC_ARROW
        );

    wc.hbrBackground =
        (HBRUSH)(COLOR_WINDOW + 1);

    wc.lpszClassName =
        CLASS_NAME;


    if (!RegisterClassA(&wc)) {

        MessageBoxA(
            NULL,
            "Unable to register GUI window class.",
            APP_TITLE,
            MB_ICONERROR
        );

        if (g_font)
            DeleteObject(g_font);

        if (g_fontBold)
            DeleteObject(g_fontBold);

        return 1;
    }


    /*
     * Main window
     */

    g_hWnd =
        CreateWindowExA(
            0,
            CLASS_NAME,
            "Legion Y720 Keyboard Backlight Controller",
            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            865,
            690,
            NULL,
            NULL,
            instance,
            NULL
        );


    if (!g_hWnd) {

        MessageBoxA(
            NULL,
            "Unable to create GUI window.",
            APP_TITLE,
            MB_ICONERROR
        );

        if (g_font)
            DeleteObject(g_font);

        if (g_fontBold)
            DeleteObject(g_fontBold);

        return 1;
    }


    if (g_appIcon) {

        SendMessageA(
            g_hWnd,
            WM_SETICON,
            ICON_BIG,
            (LPARAM)g_appIcon
        );

        SendMessageA(
            g_hWnd,
            WM_SETICON,
            ICON_SMALL,
            (LPARAM)g_appIcon
        );
    }


    /*
     * Explicit title.
     */

    SetWindowTextA(
        g_hWnd,
        "Legion Y720 Keyboard Backlight Controller"
    );


    /*
     * Show GUI.
     */

    ShowWindow(
        g_hWnd,
        show_command
    );

    UpdateWindow(
        g_hWnd
    );


    /*
     * Message loop
     */

    MSG message;

    while (GetMessageA(
               &message,
               NULL,
               0,
               0) > 0) {

        TranslateMessage(
            &message
        );

        DispatchMessageA(
            &message
        );
    }


    /*
     * Cleanup.
     */

    remove_tray_icon();

    if (g_font)
        DeleteObject(g_font);

    if (g_fontBold)
        DeleteObject(g_fontBold);

    if (g_appIcon)
        DestroyIcon(g_appIcon);

    return (int)message.wParam;
}