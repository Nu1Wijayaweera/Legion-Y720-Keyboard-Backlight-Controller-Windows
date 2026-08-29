#ifndef Y720_BACKLIGHT_CORE_H
#define Y720_BACKLIGHT_CORE_H

/* Y720BacklightCore.h - Public API for Y720 HID core helpers
 *
 * Purpose:
 * - Define constants, types and public functions used by the GUI to control
 *   the Lenovo Legion Y720 keyboard backlight. Keep this header small and
 *   focused so students can see the public surface quickly.
 */

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define Y720_VID 0x048D
#define Y720_PID 0x837A
#define Y720_USAGE_PAGE 0xFF89
#define Y720_USAGE 0x00CC
#define Y720_REPORT_ID 0xCC
#define Y720_REPORT_LENGTH 7
#define Y720_ZONE_COUNT 4

    int color_value(const char *name);
    int brightness_value(const char *name);
    int mode_value(const char *name);

    HANDLE find_y720(void);
    int send_feature(HANDLE device, unsigned char report[Y720_REPORT_LENGTH]);
    int set_zone(HANDLE device, int mode, int color, int brightness, int zone);
    int apply_final(HANDLE device);
    int apply_all(HANDLE device, int mode, int color, int brightness);
    int apply_one_zone(HANDLE device, int zone, int mode, int color, int brightness);

    /* High-level helpers: open the Y720 HID device, perform the operation, and close it. */
    int y720_apply_all(int mode, int color, int brightness);
    int y720_apply_zone(int zone, int mode, int color, int brightness);
    int y720_apply_zones(const int *modes, const int *colors, const int *brightness);
    int y720_turn_off(void);

#ifdef __cplusplus
}
#endif

#endif
