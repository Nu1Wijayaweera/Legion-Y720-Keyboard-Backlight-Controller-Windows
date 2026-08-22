#ifndef Y720_BACKLIGHT_HID_H
#define Y720_BACKLIGHT_HID_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
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
