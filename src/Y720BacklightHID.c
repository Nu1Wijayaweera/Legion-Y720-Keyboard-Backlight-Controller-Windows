#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Y720BacklightHID.h"

#define Y720_INPUT_VID             0x048D
#define Y720_INPUT_PID             0xC100
#define Y720_INPUT_USAGE_PAGE     0x000C
#define Y720_INPUT_USAGE          0x0001
#define Y720_INPUT_REPORT_ID      0x02
#define Y720_INPUT_VALUE          0x50

static HWND g_hwnd = NULL;
static Y720FnSpaceCallback g_callback = NULL;
static void *g_context = NULL;
static int g_initialized = 0;
static int g_pressed = 0;
static HANDLE g_cached_device = NULL;
static int g_cached_device_valid = 0;
static int g_cached_device_match = 0;

static int is_y720_consumer_device(HANDLE device)
{
    UINT size = 0;
    RID_DEVICE_INFO info;
    char *name = NULL;
    int matched;

    if (!device || device == INVALID_HANDLE_VALUE)
        return 0;

    if (g_cached_device_valid && g_cached_device == device)
        return g_cached_device_match;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);

    size = sizeof(info);
    if (GetRawInputDeviceInfoA(device, RIDI_DEVICEINFO,
                               &info, &size) == (UINT)-1)
        return 0;

    if (info.dwType != RIM_TYPEHID ||
        info.hid.dwVendorId != Y720_INPUT_VID ||
        info.hid.dwProductId != Y720_INPUT_PID ||
        info.hid.usUsagePage != Y720_INPUT_USAGE_PAGE ||
        info.hid.usUsage != Y720_INPUT_USAGE) {
        g_cached_device = device;
        g_cached_device_valid = 1;
        g_cached_device_match = 0;
        return 0;
    }

    /*
     * Windows identifies the keyboard's consumer-control collection as
     * COL04 on the tested Y720. Keep this additional check so another
     * 048D:C100 consumer collection cannot accidentally trigger it.
     */
    size = 0;
    if (GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, NULL, &size) == (UINT)-1 ||
        size == 0)
        return 0;

    name = (char *)malloc(size + 1);
    if (!name)
        return 0;

    if (GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, name, &size) == (UINT)-1) {
        free(name);
        return 0;
    }

    name[size] = '\0';
    matched = strstr(name, "VID_048D&PID_C100&Col04") != NULL ||
              strstr(name, "VID_048d&PID_C100&Col04") != NULL ||
              strstr(name, "VID_048D&PID_C100&COL04") != NULL;
    free(name);

    g_cached_device = device;
    g_cached_device_valid = 1;
    g_cached_device_match = matched;
    return matched;
}

static void process_raw_input(HRAWINPUT raw_input)
{
    UINT size = 0;
    BYTE *buffer = NULL;
    RAWINPUT *raw = NULL;

    if (!g_initialized || !g_callback)
        return;

    if (GetRawInputData(raw_input, RID_INPUT, NULL, &size,
                        sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        return;

    buffer = (BYTE *)malloc(size);
    if (!buffer)
        return;

    if (GetRawInputData(raw_input, RID_INPUT, buffer, &size,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        free(buffer);
        return;
    }

    raw = (RAWINPUT *)buffer;

    if (raw->header.dwType == RIM_TYPEHID &&
        is_y720_consumer_device(raw->header.hDevice)) {

        RAWHID *hid = &raw->data.hid;

        if (hid->dwSizeHid >= 3 && hid->dwCount > 0) {
            DWORD report;

            for (report = 0; report < hid->dwCount; ++report) {
                BYTE *data = hid->bRawData + report * hid->dwSizeHid;

                if (data[0] == Y720_INPUT_REPORT_ID &&
                    data[1] == Y720_INPUT_VALUE &&
                    data[2] == 0x00) {

                    /* One callback per physical press, even if a device
                       happens to repeat the consumer report while held. */
                    if (!g_pressed) {
                        g_pressed = 1;
                        g_callback(g_context);
                    }
                }
                else if (data[0] == Y720_INPUT_REPORT_ID &&
                         data[1] == 0x00 &&
                         data[2] == 0x00) {

                    g_pressed = 0;
                }
            }
        }
    }

    free(buffer);
}

int y720_input_init(HWND hwnd, Y720FnSpaceCallback callback, void *context)
{
    RAWINPUTDEVICE device;

    if (!hwnd || !callback)
        return 0;

    ZeroMemory(&device, sizeof(device));

    device.usUsagePage = Y720_INPUT_USAGE_PAGE;
    device.usUsage = Y720_INPUT_USAGE;
    device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    device.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
        return 0;

    g_hwnd = hwnd;
    g_callback = callback;
    g_context = context;
    g_initialized = 1;
    g_pressed = 0;

    return 1;
}

int y720_input_handle_message(UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    if (!g_initialized)
        return 0;

    if (message == WM_INPUT_DEVICE_CHANGE) {
        g_cached_device_valid = 0;
        return 1;
    }

    if (message != WM_INPUT)
        return 0;

    process_raw_input((HRAWINPUT)lParam);
    return 1;
}

void y720_input_shutdown(void)
{
    /*
     * We intentionally do not unregister the usage globally here.
     * RegisterRawInputDevices is process-scoped and the target window is
     * going away with this process. Clearing our callback state is enough.
     */
    g_initialized = 0;
    g_pressed = 0;
    g_cached_device = NULL;
    g_cached_device_valid = 0;
    g_cached_device_match = 0;
    g_callback = NULL;
    g_context = NULL;
    g_hwnd = NULL;
}
