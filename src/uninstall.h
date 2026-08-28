#ifndef Y720_UNINSTALL_H
#define Y720_UNINSTALL_H

#include <windows.h>

/* Schedule cleanup and self-delete actions for the running application.
 * This performs best-effort removal of startup registry Run value, config
 * and state files under %APPDATA%, and schedules a temporary batch to
 * delete the running executable after the process exits.
 */
void uninstall_schedule_and_cleanup(HWND owner);

#endif /* Y720_UNINSTALL_H */
