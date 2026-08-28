#define WIN32_LEAN_AND_MEAN

#include "Y720BacklightCore.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_ID       Y720_REPORT_ID
#define REPORT_LENGTH   Y720_REPORT_LENGTH
#define ZONE_COUNT      Y720_ZONE_COUNT

static int equals(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;
}

int color_value(const char *name)
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


int brightness_value(const char *name)
{
    if (equals(name, "off"))    return 0;
    if (equals(name, "low"))    return 1;
    if (equals(name, "medium")) return 2;
    if (equals(name, "high"))   return 3;
    if (equals(name, "ultra"))  return 4;
    if (equals(name, "enough")) return 5;

    return -1;
}


int mode_value(const char *name)
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

HANDLE find_y720(void)
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

    DWORD index;

    for (index = 0;; index++) {

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

int send_feature(
    HANDLE device,
    unsigned char report[REPORT_LENGTH])
{
    if (!HidD_SetFeature(
            device,
            report,
            REPORT_LENGTH)) {

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

int set_zone(
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


int apply_final(HANDLE device)
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

int apply_all(
    HANDLE device,
    int mode,
    int color,
    int brightness)
{
    int zone;

    for (zone = 0; zone < ZONE_COUNT; zone++) {

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

int apply_one_zone(
    HANDLE device,
    int zone,
    int mode,
    int color,
    int brightness)
{
    if (zone < 0 ||
        zone >= ZONE_COUNT) {

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
   High-level operations

   These helpers keep HID handle lifetime inside the shared core so callers
   such as the GUI never need to know about device discovery or command
   strings.
   ========================================================= */

int y720_apply_all(int mode, int color, int brightness)
{
    HANDLE device = find_y720();
    int result;

    if (device == INVALID_HANDLE_VALUE)
        return -1;

    result = apply_all(device, mode, color, brightness);
    CloseHandle(device);
    return result;
}

int y720_apply_zone(int zone, int mode, int color, int brightness)
{
    HANDLE device = find_y720();
    int result;

    if (device == INVALID_HANDLE_VALUE)
        return -1;

    result = apply_one_zone(device, zone, mode, color, brightness);
    CloseHandle(device);
    return result;
}

int y720_apply_zones(const int *modes, const int *colors, const int *brightness)
{
    HANDLE device;
    int zone;

    if (!modes || !colors || !brightness)
        return -1;

    device = find_y720();
    if (device == INVALID_HANDLE_VALUE)
        return -1;

    for (zone = 0; zone < ZONE_COUNT; ++zone) {
        if (set_zone(device, modes[zone], colors[zone], brightness[zone], zone) < 0) {
            CloseHandle(device);
            return -1;
        }
    }

    zone = apply_final(device);
    CloseHandle(device);
    return zone;
}

int y720_turn_off(void)
{
    return y720_apply_all(
        mode_value("always_on"),
        color_value("crimson"),
        brightness_value("off")
    );
}
