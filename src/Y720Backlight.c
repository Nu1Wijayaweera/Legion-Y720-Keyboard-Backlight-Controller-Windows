#define UNICODE
#define _UNICODE

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

#define Y720_VID        0x048D
#define Y720_PID        0x837A

#define Y720_USAGE_PAGE 0xFF89
#define Y720_USAGE       0x00CC

#define REPORT_ID       0xCC
#define REPORT_LENGTH   7

#define ZONE_COUNT      4

#define CONFIG_FILENAME "Y720Backlight.ini"


/* =========================================================
   Utility functions
   ========================================================= */

static void trim(char *s)
{
    char *start;
    char *end;

    start = s;

    while (*start && isspace((unsigned char)*start))
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    end = s + strlen(s);

    while (end > s &&
           isspace((unsigned char)end[-1]))
        end--;

    *end = '\0';
}


static int equals(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;
}


/* =========================================================
   Locate configuration file
   ========================================================= */

/*
 * Expected layout:
 *
 *   Legion-Y720-Keyboard-Backlight-Windows\
 *
 *       build\
 *           Y720Backlight.exe
 *
 *       config\
 *           Y720Backlight.ini
 *
 *       src\
 *           Y720Backlight.c
 *
 * The executable therefore looks for:
 *
 *       ..\config\Y720Backlight.ini
 *
 * relative to the executable itself.
 */

static int get_config_path(
    char *buffer,
    DWORD buffer_size)
{
    char exe_path[MAX_PATH];
    char exe_dir[MAX_PATH];
    char *last_slash;
    char *last_backslash;
    char *slash;

    DWORD length =
        GetModuleFileNameA(
            NULL,
            exe_path,
            sizeof(exe_path)
        );

    if (length == 0 ||
        length >= sizeof(exe_path)) {

        return 0;
    }

    strcpy(exe_dir, exe_path);

    last_slash =
        strrchr(exe_dir, '\\');

    last_backslash =
        strrchr(exe_dir, '/');

    slash = last_slash;

    if (last_backslash &&
        (!slash || last_backslash > slash)) {

        slash = last_backslash;
    }

    if (!slash)
        return 0;

    *slash = '\0';

    /*
     * Executable is inside build\
     * Configuration is in ..\config\
     */

    snprintf(
        buffer,
        buffer_size,
        "%s\\..\\config\\%s",
        exe_dir,
        CONFIG_FILENAME
    );

    return 1;
}


/* =========================================================
   Value tables
   ========================================================= */

static int color_value(const char *name)
{
    if (equals(name, "crimson"))              return 0;
    if (equals(name, "torch_red"))            return 1;
    if (equals(name, "hollywood_cerise"))     return 2;
    if (equals(name, "magenta"))              return 3;
    if (equals(name, "electric_violet"))      return 4;
    if (equals(name, "electric_violet_2"))    return 5;
    if (equals(name, "blue"))                 return 6;
    if (equals(name, "blue_ribbon"))          return 7;
    if (equals(name, "azure_radiance"))       return 8;
    if (equals(name, "cyan"))                 return 9;
    if (equals(name, "spring_green"))         return 10;
    if (equals(name, "spring_green_2"))       return 11;
    if (equals(name, "green"))                return 12;
    if (equals(name, "bright_green"))         return 13;
    if (equals(name, "lime"))                 return 14;
    if (equals(name, "yellow"))               return 15;
    if (equals(name, "web_orange"))           return 16;
    if (equals(name, "international_orange")) return 17;
    if (equals(name, "white"))                return 18;
    if (equals(name, "nocolor"))              return 19;

    return -1;
}


static int brightness_value(const char *name)
{
    if (equals(name, "off"))    return 0;
    if (equals(name, "low"))    return 1;
    if (equals(name, "medium")) return 2;
    if (equals(name, "high"))   return 3;
    if (equals(name, "ultra"))  return 4;
    if (equals(name, "enough")) return 5;

    return -1;
}


static int mode_value(const char *name)
{
    if (equals(name, "heartbeat")) return 0;
    if (equals(name, "breath"))    return 1;
    if (equals(name, "smooth"))    return 2;
    if (equals(name, "always_on")) return 3;
    if (equals(name, "wave"))      return 4;

    return -1;
}


/* =========================================================
   HID device discovery
   ========================================================= */

static HANDLE find_y720(void)
{
    GUID hid_guid;

    HidD_GetHidGuid(&hid_guid);

    HDEVINFO devices =
        SetupDiGetClassDevsW(
            &hid_guid,
            NULL,
            NULL,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

    if (devices == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    for (DWORD index = 0;; index++) {

        SP_DEVICE_INTERFACE_DATA interface_data;

        ZeroMemory(
            &interface_data,
            sizeof(interface_data)
        );

        interface_data.cbSize =
            sizeof(interface_data);

        if (!SetupDiEnumDeviceInterfaces(
                devices,
                NULL,
                &hid_guid,
                index,
                &interface_data)) {

            if (GetLastError() ==
                ERROR_NO_MORE_ITEMS)
                break;

            continue;
        }

        DWORD required_size = 0;

        SetupDiGetDeviceInterfaceDetailW(
            devices,
            &interface_data,
            NULL,
            0,
            &required_size,
            NULL
        );

        if (!required_size)
            continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)
            malloc(required_size);

        if (!detail)
            continue;

        detail->cbSize =
            sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
                devices,
                &interface_data,
                detail,
                required_size,
                NULL,
                NULL)) {

            free(detail);
            continue;
        }

        HANDLE device =
            CreateFileW(
                detail->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

        if (device == INVALID_HANDLE_VALUE) {
            free(detail);
            continue;
        }

        HIDD_ATTRIBUTES attributes;

        ZeroMemory(
            &attributes,
            sizeof(attributes)
        );

        attributes.Size =
            sizeof(attributes);

        if (!HidD_GetAttributes(
                device,
                &attributes)) {

            CloseHandle(device);
            free(detail);
            continue;
        }

        if (attributes.VendorID != Y720_VID ||
            attributes.ProductID != Y720_PID) {

            CloseHandle(device);
            free(detail);
            continue;
        }

        PHIDP_PREPARSED_DATA preparsed = NULL;

        if (!HidD_GetPreparsedData(
                device,
                &preparsed)) {

            CloseHandle(device);
            free(detail);
            continue;
        }

        HIDP_CAPS caps;

        NTSTATUS status =
            HidP_GetCaps(
                preparsed,
                &caps
            );

        HidD_FreePreparsedData(preparsed);

        if (status != HIDP_STATUS_SUCCESS) {
            CloseHandle(device);
            free(detail);
            continue;
        }

        if (caps.UsagePage != Y720_USAGE_PAGE ||
            caps.Usage != Y720_USAGE) {

            CloseHandle(device);
            free(detail);
            continue;
        }

        free(detail);

        SetupDiDestroyDeviceInfoList(devices);

        return device;
    }

    SetupDiDestroyDeviceInfoList(devices);

    return INVALID_HANDLE_VALUE;
}


/* =========================================================
   HID commands
   ========================================================= */

static int send_feature(
    HANDLE device,
    unsigned char report[REPORT_LENGTH])
{
    if (!HidD_SetFeature(
            device,
            report,
            REPORT_LENGTH)) {

        printf(
            "HidD_SetFeature failed: %lu\n",
            GetLastError()
        );

        return -1;
    }

    return 0;
}


/*
 * Y720 lighting report:
 *
 * Byte 0 = Report ID       0xCC
 * Byte 1 = Command         0x00
 * Byte 2 = Mode
 * Byte 3 = Color
 * Byte 4 = Brightness
 * Byte 5 = Zone
 * Byte 6 = Reserved
 */

static int set_zone(
    HANDLE device,
    int mode,
    int color,
    int brightness,
    int zone)
{
    unsigned char report[REPORT_LENGTH] = {
        REPORT_ID,
        0x00,
        (unsigned char)mode,
        (unsigned char)color,
        (unsigned char)brightness,
        (unsigned char)zone,
        0x00
    };

    return send_feature(
        device,
        report
    );
}


static int apply_final(HANDLE device)
{
    unsigned char report[REPORT_LENGTH] = {
        REPORT_ID,
        0x09,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00
    };

    return send_feature(
        device,
        report
    );
}


/*
 * Apply the same configuration to all four zones.
 */

static int apply_all(
    HANDLE device,
    int mode,
    int color,
    int brightness)
{
    for (int zone = 0;
         zone < ZONE_COUNT;
         zone++) {

        if (set_zone(
                device,
                mode,
                color,
                brightness,
                zone) < 0) {

            return -1;
        }
    }

    return apply_final(device);
}


/*
 * Apply configuration to ONE zone only.
 */

static int apply_one_zone(
    HANDLE device,
    int zone,
    int mode,
    int color,
    int brightness)
{
    if (zone < 0 ||
        zone >= ZONE_COUNT) {

        printf(
            "Invalid zone: %d\n"
            "Valid zones are 0, 1, 2 and 3.\n",
            zone
        );

        return -1;
    }

    if (set_zone(
            device,
            mode,
            color,
            brightness,
            zone) < 0) {

        return -1;
    }

    return apply_final(device);
}


/* =========================================================
   Zone test
   ========================================================= */

static int zone_test(HANDLE device)
{
    /*
     * We deliberately use four different colors
     * so the physical zones can easily be identified.
     */

    const int colors[ZONE_COUNT] = {
        0,   /* crimson */
        12,  /* green */
        6,   /* blue */
        18   /* white */
    };

    const char *names[ZONE_COUNT] = {
        "CRIMSON",
        "GREEN",
        "BLUE",
        "WHITE"
    };

    printf("\n");
    printf("============================================\n");
    printf(" Y720 FOUR-ZONE TEST\n");
    printf("============================================\n");
    printf("\n");

    printf(
        "Each zone will be illuminated individually.\n"
        "Watch the keyboard and note which physical\n"
        "section lights for each zone number.\n"
        "\n"
    );

    for (int zone = 0;
         zone < ZONE_COUNT;
         zone++) {

        printf(
            "Zone %d -> %s\n",
            zone,
            names[zone]
        );

        if (set_zone(
                device,
                3,              /* always_on */
                colors[zone],
                3,              /* high */
                zone) < 0) {

            return -1;
        }

        if (apply_final(device) < 0)
            return -1;

        Sleep(1500);

        /*
         * Turn this zone off before moving
         * to the next one.
         */

        if (set_zone(
                device,
                3,
                18,
                0,
                zone) < 0) {

            return -1;
        }

        if (apply_final(device) < 0)
            return -1;

        Sleep(300);
    }

    printf("\n");
    printf("Zone test complete.\n");

    return 0;
}


/* =========================================================
   Profile structure
   ========================================================= */

typedef struct {
    char name[64];

    char color[64];
    char brightness[64];
    char mode[64];

} profile_t;


/* =========================================================
   Read profile from INI
   ========================================================= */

static int load_profile(
    const char *filename,
    const char *profile_name,
    profile_t *profile)
{
    FILE *file;

    char line[512];
    char current_section[128];

    int found = 0;

    current_section[0] = '\0';

    file = fopen(filename, "r");

    if (!file) {
        printf(
            "Cannot open profile file:\n"
            "  %s\n",
            filename
        );

        return -1;
    }

    while (fgets(line, sizeof(line), file)) {

        char *eq;

        trim(line);

        if (!line[0])
            continue;

        if (line[0] == '#' ||
            line[0] == ';')
            continue;

        if (line[0] == '[') {

            char *end =
                strchr(line, ']');

            if (!end)
                continue;

            *end = '\0';

            strcpy(
                current_section,
                line + 1
            );

            trim(current_section);

            if (equals(
                    current_section,
                    profile_name)) {

                found = 1;

                strcpy(
                    profile->name,
                    profile_name
                );
            }

            continue;
        }

        if (!found)
            continue;

        eq = strchr(line, '=');

        if (!eq)
            continue;

        *eq = '\0';

        char *key = line;
        char *value = eq + 1;

        trim(key);
        trim(value);

        if (equals(key, "color")) {

            strncpy(
                profile->color,
                value,
                sizeof(profile->color) - 1
            );

            profile->color[
                sizeof(profile->color) - 1
            ] = '\0';
        }

        else if (equals(key, "brightness")) {

            strncpy(
                profile->brightness,
                value,
                sizeof(profile->brightness) - 1
            );

            profile->brightness[
                sizeof(profile->brightness) - 1
            ] = '\0';
        }

        else if (equals(key, "mode")) {

            strncpy(
                profile->mode,
                value,
                sizeof(profile->mode) - 1
            );

            profile->mode[
                sizeof(profile->mode) - 1
            ] = '\0';
        }
    }

    fclose(file);

    if (!found) {

        printf(
            "Profile not found: %s\n",
            profile_name
        );

        return -1;
    }

    if (!profile->color[0] ||
        !profile->brightness[0] ||
        !profile->mode[0]) {

        printf(
            "Profile '%s' is incomplete.\n"
            "Required: color, brightness, mode\n",
            profile_name
        );

        return -1;
    }

    return 0;
}


/* =========================================================
   List profiles
   ========================================================= */

static void list_profiles(
    const char *filename)
{
    FILE *file;
    char line[512];

    file = fopen(filename, "r");

    if (!file) {
        printf(
            "Cannot open %s\n",
            filename
        );

        return;
    }

    printf(
        "Profiles in:\n"
        "  %s\n\n",
        filename
    );

    while (fgets(line, sizeof(line), file)) {

        trim(line);

        if (line[0] == '[') {

            char *end =
                strchr(line, ']');

            if (end) {

                *end = '\0';

                printf(
                    "  %s\n",
                    line + 1
                );
            }
        }
    }

    fclose(file);
}


/* =========================================================
   Help
   ========================================================= */

static void print_help(
    const char *program)
{
    printf(
        "\n"
        "============================================\n"
        " Legion Y720 Keyboard Backlight Controller\n"
        " Windows HID controller\n"
        "============================================\n"
        "\n"

        "USAGE\n"
        "-----\n"
        "  %s set COLOR BRIGHTNESS MODE\n"
        "  %s profile NAME\n"
        "  %s color COLOR\n"
        "  %s brightness LEVEL\n"
        "  %s mode MODE\n"
        "  %s zone ZONE COLOR BRIGHTNESS MODE\n"
        "  %s zone-test\n"
        "  %s PROFILE\n"
        "\n"

        "PROFILE COMMANDS\n"
        "----------------\n"
        "  %s list-profiles\n"
        "  %s profile NAME\n"
        "\n"

        "COLORS\n"
        "------\n"
        "  crimson\n"
        "  torch_red\n"
        "  hollywood_cerise\n"
        "  magenta\n"
        "  electric_violet\n"
        "  electric_violet_2\n"
        "  blue\n"
        "  blue_ribbon\n"
        "  azure_radiance\n"
        "  cyan\n"
        "  spring_green\n"
        "  spring_green_2\n"
        "  green\n"
        "  bright_green\n"
        "  lime\n"
        "  yellow\n"
        "  web_orange\n"
        "  international_orange\n"
        "  white\n"
        "  nocolor\n"
        "\n"

        "BRIGHTNESS\n"
        "----------\n"
        "  off\n"
        "  low\n"
        "  medium\n"
        "  high\n"
        "  ultra\n"
        "  enough\n"
        "\n"

        "MODES\n"
        "-----\n"
        "  heartbeat\n"
        "  breath\n"
        "  smooth\n"
        "  always_on\n"
        "  wave\n"
        "\n"

        "ZONES\n"
        "-----\n"
        "  0\n"
        "  1\n"
        "  2\n"
        "  3\n"
        "\n"

        "EXAMPLES\n"
        "--------\n"
        "  %s red\n"
        "  %s blue\n"
        "  %s off\n"
        "  %s set red high always_on\n"
        "  %s set blue low breath\n"
        "  %s set green medium smooth\n"
        "  %s set white high wave\n"
        "\n"

        "FOUR-ZONE EXAMPLES\n"
        "------------------\n"
        "  %s zone 0 red high always_on\n"
        "  %s zone 1 blue high always_on\n"
        "  %s zone 2 green high always_on\n"
        "  %s zone 3 white high always_on\n"
        "\n"

        "  %s zone-test\n"
        "\n"

        "PROFILES\n"
        "--------\n"
        "  %s profile gaming\n"
        "  %s profile night\n"
        "  %s list-profiles\n"
        "\n",

        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,

        program,
        program,

        program,
        program,
        program,

        program,
        program,
        program,
        program,

        program,

        program,
        program,
        program
    );
}


/* =========================================================
   Open Y720
   ========================================================= */

static HANDLE open_y720_or_report(void)
{
    HANDLE device = find_y720();

    if (device == INVALID_HANDLE_VALUE) {

        printf(
            "Y720 lighting device not found.\n"
            "\n"
            "Make sure the keyboard is connected and\n"
            "the Y720 HID lighting device is present.\n"
        );
    }

    return device;
}


/* =========================================================
   Main
   ========================================================= */

int main(
    int argc,
    char *argv[])
{
    char config[MAX_PATH];

    int mode;
    int color;
    int brightness;

    HANDLE device;


    /*
     * Locate configuration file.
     */

    if (!get_config_path(
            config,
            sizeof(config))) {

        /*
         * Fallback for unusual situations.
         */

        strcpy(
            config,
            "config\\" CONFIG_FILENAME
        );
    }


    /* -----------------------------------------
       No arguments / help
       ----------------------------------------- */

    if (argc < 2) {

        print_help(argv[0]);

        return 0;
    }


    if (equals(argv[1], "help") ||
        equals(argv[1], "--help") ||
        equals(argv[1], "-h")) {

        print_help(argv[0]);

        return 0;
    }


    /* -----------------------------------------
       List profiles
       ----------------------------------------- */

    if (equals(argv[1], "list-profiles")) {

        list_profiles(config);

        return 0;
    }


    /* -----------------------------------------
       Direct set command
       ----------------------------------------- */

    if (equals(argv[1], "set")) {

        if (argc < 5) {

            printf(
                "Usage: %s set COLOR BRIGHTNESS MODE\n",
                argv[0]
            );

            return 1;
        }

        color =
            color_value(argv[2]);

        brightness =
            brightness_value(argv[3]);

        mode =
            mode_value(argv[4]);

        if (color < 0) {

            printf(
                "Unknown color: %s\n",
                argv[2]
            );

            return 1;
        }

        if (brightness < 0) {

            printf(
                "Unknown brightness: %s\n",
                argv[3]
            );

            return 1;
        }

        if (mode < 0) {

            printf(
                "Unknown mode: %s\n",
                argv[4]
            );

            return 1;
        }

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                mode,
                color,
                brightness
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Four-zone command
       ----------------------------------------- */

    if (equals(argv[1], "zone")) {

        int zone;

        if (argc < 6) {

            printf(
                "Usage: %s zone ZONE COLOR BRIGHTNESS MODE\n"
                "\n"
                "ZONE must be 0, 1, 2 or 3.\n",
                argv[0]
            );

            return 1;
        }

        zone =
            atoi(argv[2]);

        if (zone < 0 ||
            zone >= ZONE_COUNT) {

            printf(
                "Invalid zone: %d\n"
                "Valid zones: 0, 1, 2, 3\n",
                zone
            );

            return 1;
        }

        color =
            color_value(argv[3]);

        brightness =
            brightness_value(argv[4]);

        mode =
            mode_value(argv[5]);

        if (color < 0) {

            printf(
                "Unknown color: %s\n",
                argv[3]
            );

            return 1;
        }

        if (brightness < 0) {

            printf(
                "Unknown brightness: %s\n",
                argv[4]
            );

            return 1;
        }

        if (mode < 0) {

            printf(
                "Unknown mode: %s\n",
                argv[5]
            );

            return 1;
        }

        printf(
            "Setting zone %d:\n"
            "  Color:      %s\n"
            "  Brightness: %s\n"
            "  Mode:       %s\n",
            zone,
            argv[3],
            argv[4],
            argv[5]
        );

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_one_zone(
                device,
                zone,
                mode,
                color,
                brightness
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Four-zone test
       ----------------------------------------- */

    if (equals(argv[1], "zone-test")) {

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            zone_test(device);

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Profile
       ----------------------------------------- */

    if (equals(argv[1], "profile")) {

        if (argc < 3) {

            printf(
                "Usage: %s profile NAME\n",
                argv[0]
            );

            return 1;
        }

        profile_t profile;

        ZeroMemory(
            &profile,
            sizeof(profile)
        );

        if (load_profile(
                config,
                argv[2],
                &profile) < 0) {

            return 1;
        }

        color =
            color_value(profile.color);

        brightness =
            brightness_value(profile.brightness);

        mode =
            mode_value(profile.mode);

        if (color < 0) {

            printf(
                "Invalid profile color: %s\n",
                profile.color
            );

            return 1;
        }

        if (brightness < 0) {

            printf(
                "Invalid profile brightness: %s\n",
                profile.brightness
            );

            return 1;
        }

        if (mode < 0) {

            printf(
                "Invalid profile mode: %s\n",
                profile.mode
            );

            return 1;
        }

        printf(
            "Profile: %s\n"
            "  Color:      %s\n"
            "  Brightness: %s\n"
            "  Mode:       %s\n",
            profile.name,
            profile.color,
            profile.brightness,
            profile.mode
        );

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                mode,
                color,
                brightness
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Convenience colors
       ----------------------------------------- */

    if (equals(argv[1], "red") ||
        equals(argv[1], "blue") ||
        equals(argv[1], "green") ||
        equals(argv[1], "white") ||
        equals(argv[1], "off")) {

        color =
            color_value(argv[1]);

        brightness =
            equals(argv[1], "off") ? 0 : 3;

        mode = 3;

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                mode,
                color,
                brightness
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Color shortcut
       ----------------------------------------- */

    if (equals(argv[1], "color")) {

        if (argc < 3) {

            printf(
                "Usage: %s color COLOR\n",
                argv[0]
            );

            return 1;
        }

        color =
            color_value(argv[2]);

        if (color < 0) {

            printf(
                "Unknown color: %s\n",
                argv[2]
            );

            return 1;
        }

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                3,
                color,
                3
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Brightness shortcut
       ----------------------------------------- */

    if (equals(argv[1], "brightness")) {

        if (argc < 3) {

            printf(
                "Usage: %s brightness LEVEL\n",
                argv[0]
            );

            return 1;
        }

        brightness =
            brightness_value(argv[2]);

        if (brightness < 0) {

            printf(
                "Unknown brightness: %s\n",
                argv[2]
            );

            return 1;
        }

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                3,
                18,
                brightness
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Mode shortcut
       ----------------------------------------- */

    if (equals(argv[1], "mode")) {

        if (argc < 3) {

            printf(
                "Usage: %s mode MODE\n",
                argv[0]
            );

            return 1;
        }

        mode =
            mode_value(argv[2]);

        if (mode < 0) {

            printf(
                "Unknown mode: %s\n",
                argv[2]
            );

            return 1;
        }

        device =
            open_y720_or_report();

        if (device == INVALID_HANDLE_VALUE)
            return 1;

        int result =
            apply_all(
                device,
                mode,
                18,
                3
            );

        CloseHandle(device);

        return result < 0 ? 1 : 0;
    }


    /* -----------------------------------------
       Profile shortcut
       ----------------------------------------- */

    {
        profile_t profile;

        ZeroMemory(
            &profile,
            sizeof(profile)
        );

        if (load_profile(
                config,
                argv[1],
                &profile) == 0) {

            color =
                color_value(profile.color);

            brightness =
                brightness_value(profile.brightness);

            mode =
                mode_value(profile.mode);

            if (color >= 0 &&
                brightness >= 0 &&
                mode >= 0) {

                device =
                    open_y720_or_report();

                if (device ==
                    INVALID_HANDLE_VALUE)
                    return 1;

                int result =
                    apply_all(
                        device,
                        mode,
                        color,
                        brightness
                    );

                CloseHandle(device);

                return result < 0 ? 1 : 0;
            }
        }
    }


    /* -----------------------------------------
       Unknown command
       ----------------------------------------- */

    printf(
        "Unknown command: %s\n",
        argv[1]
    );

    print_help(argv[0]);

    return 1;
}