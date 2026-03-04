#include "config.h"

#include <ameba_soc.h>
#include <atcmd_service.h>
#include <os_wrapper.h>
#include <stdlib.h>
#include <vfs.h>
#include <sys/unistd.h>
#include <envlock.h>
#include <c_utils/hal_util_storage.h>

#define TAG "CONFIG"
#define ENV_DIR "ENV"

int load_all_env(void)
{
    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        char path[MAX_PATH_LEN];
        DiagSnPrintf(path, MAX_PATH_LEN, "%s:%s", prefix, ENV_DIR);
        void *dir = opendir(path);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || entry->d_type != DT_REG) {
                    continue;
                }
                char file_path[MAX_PATH_LEN];
                DiagSnPrintf(file_path, MAX_PATH_LEN, "%s/%s", path, entry->d_name);
                FILE *f = fopen(file_path, "r");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long file_size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    char value[MAX_ENV_VALUE_LENGTH];
                    if (file_size < MAX_ENV_VALUE_LENGTH) {
                        fread(value, 1, file_size, f);
                        value[file_size] = '\0';
                        setenv(entry->d_name, value, 1);
                        RTK_LOGS(TAG, RTK_LOG_INFO, "Loaded env %s: %s\n", entry->d_name, value);
                    } else {
                        RTK_LOGS(TAG, RTK_LOG_ERROR, "Value for key %s exceeds max length %d\n", entry->d_name, MAX_ENV_VALUE_LENGTH);
                    }
                    fclose(f);
                } else {
                    RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to open env file %s for reading\n", file_path);
                }
            }
            closedir(dir);
            return 0;
        } else {
            RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to open env directory %s\n", path);
            return -1;
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to find VFS region for env file\n");
        return -1;
    }
}

int save_all_env(void)
{
    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        char path[MAX_ENV_VALUE_LENGTH];
        DiagSnPrintf(path, MAX_ENV_VALUE_LENGTH, "%s:%s", prefix, ENV_DIR);
        mkdir(path, 0);
        __env_lock(_REENT);
        for (char **env = environ; *env; env++) {
            char *equal_sign = strchr(*env, '=');
            if (equal_sign) {
                char *value_end = strchr(equal_sign + 1, '\n');
                size_t key_len = equal_sign - *env;
                size_t value_len = value_end ? (size_t)(value_end - (equal_sign + 1)) : strlen(equal_sign + 1);
                if (key_len > MAX_PATH_LEN || value_len > MAX_ENV_VALUE_LENGTH) {
                    RTK_LOGS(TAG, RTK_LOG_ERROR, "Skipping env with key length %d or value length %d exceeding limits\n", key_len, value_len);
                    continue;
                }
                char key[MAX_PATH_LEN];
                char value[MAX_ENV_VALUE_LENGTH];
                strncpy(key, *env, key_len);
                key[key_len] = '\0';
                strncpy(value, equal_sign + 1, value_len);
                value[value_len] = '\0';
                char file_path[MAX_PATH_LEN];
                DiagSnPrintf(file_path, MAX_PATH_LEN, "%s/%s", path, key);
                FILE *f = fopen(file_path, "w");
                if (f) {
                    fwrite(value, 1, value_len, f);
                    fclose(f);
                    RTK_LOGS(TAG, RTK_LOG_INFO, "Saved env %s: %s\n", key, value);
                }
                else {
                    RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to open env file %s for writing\n", file_path);
                }
            } else {
                RTK_LOGS(TAG, RTK_LOG_ERROR, "Skipping invalid env entry: %s\n", *env);
            }
        }
        __env_unlock(_REENT);
        return 0;
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to find VFS region for env file\n");
        return -1;
    }
}

static int save_linkkeyconfig(char *ws_id, char *app_id, char *app_secret, char *device_name, char *api_key)
{
    util_storage_erase();
    setenv("WS_ID", ws_id, 1);
    setenv("APP_ID", app_id, 1);
    setenv("APP_SECRET", app_secret, 1);
    setenv("DEVICE_NAME", device_name, 1);
    setenv("API_KEY", api_key, 1);
    return save_all_env();
}

/// AT+LINKKEYCONFIG=<ws_id>,<app_id>,<app_secret>,<device_name>,<api_key>
void at_linkkeyconfig(u16 argc, char **argv)
{
	char *ws_id = argv[1];
    char *app_id = argv[2];
    char *app_secret = argv[3];
    char *device_name = argv[4];
    char *api_key = argv[5];

    if (argc == 6) {
        if (save_linkkeyconfig(ws_id, app_id, app_secret, device_name, api_key) == 0) {
            at_printf("\r\nOK\r\n");
            return;
        } else {
            RTK_LOGS(TAG, RTK_LOG_ERROR, "Failed to save linkkeyconfig to env\n");
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Invalid number of parameters\n");
    }
    at_printf("\r\nERROR\r\n");
}

void at_linkkeyconfig_query(u16 argc, char **argv)
{
    (void) argc;
    (void) argv;

    char *ws_id = getenv("WS_ID");
    char *app_id = getenv("APP_ID");
    char *app_secret = getenv("APP_SECRET");
    char *device_name = getenv("DEVICE_NAME");
    char *api_key = getenv("API_KEY");
    if (ws_id && app_id && app_secret && device_name && api_key) {
        at_printf("\r\n+LINKKEYCONFIG:\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\r\nOK\r\n", ws_id, app_id, app_secret, device_name, api_key);
        return;
    }
    
    at_printf("\r\nERROR\r\n");
}

void at_env_query(u16 argc, char **argv)
{
    (void) argc;
    (void) argv;

    __env_lock(_REENT);
    for (char **env = environ; *env; env++) {
        at_printf("+ENV:%s\n", *env);
    }
    __env_unlock(_REENT);
    at_printf("\r\nOK\r\n");
}

void at_env_set(u16 argc, char **argv)
{
    if (argc == 3) {
        const char *key = argv[1];
        const char *value = argv[2];

        size_t key_len = strlen(key);
        size_t value_len = strlen(value);

        if (key_len == 0) {
            RTK_LOGS(TAG, RTK_LOG_ERROR, "Key is empty\n");
            at_printf("\r\nERROR\r\n");
            return;
        }

        if (key_len > MAX_PATH_LEN) {
            RTK_LOGS(TAG, RTK_LOG_ERROR, "Key length exceeds limit %d: %s\n", MAX_PATH_LEN, key);
            at_printf("\r\nERROR\r\n");
            return;
        }

        if (value_len > MAX_ENV_VALUE_LENGTH) {
            RTK_LOGS(TAG, RTK_LOG_ERROR, "Value length exceeds limit %d: %s\n", MAX_ENV_VALUE_LENGTH, value);
            at_printf("\r\nERROR\r\n");
            return;
        }

        if (value_len == 0) {
            unsetenv(key);
        } else {
            setenv(key, value, 1);
        }

        if (save_all_env() == 0) {
            at_printf("\r\nOK\r\n");
            return;
        }
    }
    else if (argc == 2) {
        const char *key = argv[1];
        const char *value = getenv(key);
        if (value) {
            at_printf("\r\n+ENV:\"%s\",\"%s\"\r\nOK\r\n", key, value);
            return;
        } else {
            at_printf("\r\n+ENV:\"%s\",\"\"\r\nOK\r\n", key);
            return;
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Invalid number of parameters\n");
    }
    at_printf("\r\nERROR\r\n");
}
