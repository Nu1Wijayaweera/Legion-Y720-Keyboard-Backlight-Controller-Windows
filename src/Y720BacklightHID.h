#ifndef Y720_BACKLIGHT_HID_H
#define Y720_BACKLIGHT_HID_H

/* Y720BacklightHID.h - Raw input (Fn+Space) API
 *
 * Provides a tiny, well-documented interface for the GUI to receive
 * notifications when the keyboard's Fn+Space control is pressed. The
 * implementation uses Raw Input and intentionally reports only the press
 * event to keep the callback model simple for learners.
 */

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*Y720FnSpaceCallback)(void *context);

    /*
     * Listen for the Legion Y720's Fn+Space consumer-control event.
     * The module only reports the press event; it does not change lighting.
     */
    int y720_input_init(HWND hwnd, Y720FnSpaceCallback callback, void *context);
    int y720_input_handle_message(UINT message, WPARAM wParam, LPARAM lParam);
    void y720_input_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
