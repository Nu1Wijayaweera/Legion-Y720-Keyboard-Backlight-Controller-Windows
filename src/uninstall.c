#include "uninstall.h"
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

/* Local helpers (independent copies so this module is self-contained). */
static int uc_get_exe_directory(char *buffer, DWORD buffer_size)
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

static int uc_build_path(char *buffer, size_t buffer_size, const char *base, const char *suffix)
{
    int written;
    if (!buffer || buffer_size == 0 || !base || !suffix) return 0;
    written = snprintf(buffer, buffer_size, "%s\\%s", base, suffix);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[0] = '\0';
        return 0;
    }
    return 1;
}

/* Basic validation to check for dangerous characters in paths */
static int uc_is_safe_path(const char *path)
{
    int i;
    if (!path) return 0;
    
    for (i = 0; path[i]; ++i) {
        unsigned char c = (unsigned char)path[i];
        /* Allow normal path characters, reject control chars and potential injection chars */
        if (c < 32 || c == 127) return 0; /* Control characters */
        /* Reject shell metacharacters that could be exploited in batch files */
        if (c == '&' || c == '|' || c == ';' || c == '`' || c == '$') return 0;
        /* Reject redirect characters */
        if (c == '<' || c == '>') return 0;
        /* Reject batch special characters */
        if (c == '^' || c == '%') return 0;
    }
    return 1;
}

static int uc_get_config_path(char *buffer, DWORD buffer_size)
{
    char directory[MAX_PATH], candidate[MAX_PATH], appdata[MAX_PATH], folder[MAX_PATH], fallback[MAX_PATH];
    HANDLE file;
    DWORD length;
    if (!buffer || buffer_size == 0) return 0;

    if (!uc_get_exe_directory(directory, sizeof(directory))) goto fallback;
    if (!uc_build_path(candidate, sizeof(candidate), directory, "Y720Backlight.ini")) goto fallback;
    file = CreateFileA(candidate, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        snprintf(buffer, buffer_size, "%s", candidate);
        buffer[buffer_size - 1] = '\0';
        return 1;
    }

fallback:
    length = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (!length || length >= sizeof(appdata) || !appdata[0]) return 0;
    if (!uc_build_path(folder, sizeof(folder), appdata, "LegionY720Backlight")) return 0;
    CreateDirectoryA(folder, NULL);
    if (!uc_build_path(fallback, sizeof(fallback), folder, "Y720Backlight.ini")) return 0;
    /* If exe-local candidate existed earlier, copying is skipped here; keep best-effort semantics. */
    snprintf(buffer, buffer_size, "%s", fallback);
    buffer[buffer_size - 1] = '\0';
    return 1;
}

static int uc_get_state_path(char *buffer, DWORD buffer_size)
{
    char appdata[MAX_PATH];
    DWORD length;
    if (!buffer || buffer_size == 0) return 0;
    length = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (!length || length >= sizeof(appdata) || !appdata[0]) return 0;
    if (!uc_build_path(buffer, buffer_size, appdata, "LegionY720Backlight")) return 0;
    CreateDirectoryA(buffer, NULL);
    if (!uc_build_path(buffer, buffer_size, appdata, "LegionY720Backlight\\state.ini")) return 0;
    return 1;
}

/* Remove the HKCU Run entry named LegionY720Backlight (best-effort). */
static void uc_clear_startup_entry(void)
{
    HKEY key;
    const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char *value_name = "LegionY720Backlight";
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS) return;
    RegDeleteValueA(key, value_name);
    RegCloseKey(key);
}

void uninstall_schedule_and_cleanup(HWND owner)
{
    (void)owner; /* owner is currently unused */
    char path[MAX_PATH];
    char exe[MAX_PATH];
    char temp_path[MAX_PATH];
    char bat_path[MAX_PATH];
    char appdata_folder[MAX_PATH];
    DWORD len;
    HANDLE f;
    DWORD written;

    /* Remove startup entry */
    uc_clear_startup_entry();

    /* Delete config file if present */
    if (uc_get_config_path(path, sizeof(path))) {
        if (uc_is_safe_path(path)) {
            DeleteFileA(path);
        }
    }

    /* Delete state file and attempt to remove folder */
    if (uc_get_state_path(path, sizeof(path))) {
        if (uc_is_safe_path(path)) {
            DeleteFileA(path);
            char *slash = strrchr(path, '\\');
            if (slash) {
                *slash = '\0';
                RemoveDirectoryA(path);
            }
        }
    }

    /* Prepare temp batch to remove the exe after exit */
    exe[0] = '\0'; temp_path[0] = '\0'; bat_path[0] = '\0'; appdata_folder[0] = '\0';

    if (!GetModuleFileNameA(NULL, exe, sizeof(exe))) exe[0] = '\0';
    if (!uc_is_safe_path(exe)) exe[0] = '\0'; /* Validate exe path before use */
    if (!GetTempPathA(sizeof(temp_path), temp_path)) temp_path[0] = '\0';
    if (!uc_is_safe_path(temp_path)) temp_path[0] = '\0'; /* Validate temp path before use */

    /* Construct bat filename safely into bat_path */
    {
        const char *fname = "Y720_uninstall.bat";
        size_t tp = strlen(temp_path);
        size_t fn = strlen(fname);
        if (tp && tp + fn + 1 <= sizeof(bat_path)) {
            memcpy(bat_path, temp_path, tp);
            memcpy(bat_path + tp, fname, fn + 1);
        } else if (tp && tp < sizeof(bat_path)) {
            /* temp path present but original filename won't fit: fallback to temp_path + short shortname */
            {
                const char *sname = "Y720_uninst.bat";
                size_t sn = strlen(sname);
                if (tp + sn + 1 <= sizeof(bat_path)) {
                    memcpy(bat_path, temp_path, tp);
                    memcpy(bat_path + tp, sname, sn + 1);
                } else if (sn + 1 <= sizeof(bat_path)) {
                    /* place just the short name in the buffer */
                    memcpy(bat_path, sname, sn + 1);
                } else {
                    bat_path[0] = '\0';
                }
            }
        } else {
            /* fallback to just filename in CWD */
            snprintf(bat_path, sizeof(bat_path), "%s", fname);
        }
    }

    /* Determine appdata folder used earlier */
    len = GetEnvironmentVariableA("APPDATA", appdata_folder, sizeof(appdata_folder));
    if (len && len < sizeof(appdata_folder)) {
        char folder[MAX_PATH];
        if (uc_build_path(folder, sizeof(folder), appdata_folder, "LegionY720Backlight"))
            snprintf(appdata_folder, sizeof(appdata_folder), "%s", folder);
        else
            appdata_folder[0] = '\0';
    } else appdata_folder[0] = '\0';

    /* Build batch content */
    {
        char contents[2048];
        if (appdata_folder[0]) {
            snprintf(contents, sizeof(contents),
                     "@echo off\r\n"
                     "ping 127.0.0.1 -n 6 >nul\r\n"
                     "del /f /q \"%s\"\r\n"
                     "rd /s /q \"%s\"\r\n"
                     "del /f /q \"%%~f0\"\r\n",
                     exe, appdata_folder);
        } else {
            snprintf(contents, sizeof(contents),
                     "@echo off\r\n"
                     "ping 127.0.0.1 -n 6 >nul\r\n"
                     "del /f /q \"%s\"\r\n"
                     "del /f /q \"%%~f0\"\r\n",
                     exe);
        }

        /* Validate batch content doesn't contain dangerous characters from exe path */
        if (!uc_is_safe_path(exe)) {
            /* If exe path is unsafe, don't create batch file */
            return;
        }
        if (appdata_folder[0] && !uc_is_safe_path(appdata_folder)) {
            /* If appdata path is unsafe, don't create batch file */
            return;
        }

        f = CreateFileA(bat_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, contents, (DWORD)strlen(contents), &written, NULL);
            CloseHandle(f);
            ShellExecuteA(NULL, "open", bat_path, NULL, NULL, SW_HIDE);
        } else {
            /* If we couldn't create the batch in temp, try writing to current dir as a last resort */
            char cwd_bat[MAX_PATH];
            if (GetCurrentDirectoryA(sizeof(cwd_bat), cwd_bat)) {
                char fallback[MAX_PATH];
                /* Build fallback = cwd_bat + "\\Y720_uninstall.bat" safely */
                {
                    const char *name = "\\Y720_uninstall.bat";
                    size_t cb = strlen(cwd_bat);
                    size_t nn = strlen(name);
                    if (cb + nn + 1 <= sizeof(fallback)) {
                        memcpy(fallback, cwd_bat, cb);
                        memcpy(fallback + cb, name, nn + 1);
                    } else if (nn + 1 <= sizeof(fallback)) {
                        /* Only the filename fits */
                        memcpy(fallback, name + 1, nn); /* skip leading backslash */
                        fallback[nn] = '\0';
                    } else {
                        fallback[0] = '\0';
                    }
                }
                f = CreateFileA(fallback, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    WriteFile(f, contents, (DWORD)strlen(contents), &written, NULL);
                    CloseHandle(f);
                    ShellExecuteA(NULL, "open", fallback, NULL, NULL, SW_HIDE);
                }
            }
        }
    }
}
