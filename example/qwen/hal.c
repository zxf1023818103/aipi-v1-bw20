#include <c_utils/hal_util_mem.h>
#include <c_utils/hal_util_random.h>
#include <c_utils/hal_util_storage.h>
#include <c_utils/hal_util_time.h>
#include <c_utils/hal_util_mutex.h>
#include <qwen_test.h>
#include <stdlib.h>
#include <vfs.h>
#include <ameba_soc.h>

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include "config.h"

#define TAG "HAL"

#define STORAGE_FILE_NAME "qwen_data"

void* util_malloc(int32_t size)
{
    if (size <= 0) {
        return NULL;
    }
    return pvPortMalloc(size);
}

void util_free(void *ptr)
{
    if (ptr != NULL) {
        vPortFree(ptr);
    }
}

void* util_realloc(void *ptr, int32_t size)
{
    if (size <= 0) {
        return NULL;
    }
    return pvPortReAlloc(ptr, size);
}

int32_t util_random_init(uint32_t seed)
{
    srand(seed);
    return UTIL_SUCCESS;
}

uint32_t util_random(void)
{
    uint32_t ret = rand();
    return ret;
}

void qwen_sdk_test_init(void)
{
    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        char path[MAX_PATH_LEN];
        DiagSnPrintf(path, MAX_PATH_LEN, "%s:%s", prefix, STORAGE_FILE_NAME);
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
        } else {
            RTK_LOGI(TAG, "Reset %s for test\n", path);
            static uint8_t data[512];
            memset(data, 0xff, 256);
            memset(data + 256, 0xa5, 256);
            util_storage_storage(data, 512);
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to find VFS region for file\n");
    }
}

int32_t util_storage_erase(void)
{
    RTK_LOGD(TAG, "Erasing storage data\n");

    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        char path[MAX_PATH_LEN];
        DiagSnPrintf(path, MAX_PATH_LEN, "%s:%s", prefix, STORAGE_FILE_NAME);
        FILE *f = fopen(path, "w");
        if (f) {
            fclose(f);
            return UTIL_SUCCESS;
        } else {
            RTK_LOGE(TAG, "Failed to open storage file %s for erasing\n", path);
            return UTIL_ERR_FAIL;
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to find VFS region for file\n");
        return UTIL_ERR_FAIL;
    }
}

int32_t util_storage_storage(uint8_t *data, uint32_t size)
{
    RTK_LOGD(TAG, "Attempting to store data to storage with size %d\n", size);
    // rtk_log_memory_dump_byte(data, size);

    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        char path[MAX_PATH_LEN];
        DiagSnPrintf(path, MAX_PATH_LEN, "%s:%s", prefix, STORAGE_FILE_NAME);
        FILE *f = fopen(path, "w");
        if (f) {
            fwrite(data, 1, size, f);
            fclose(f);
            return UTIL_SUCCESS;
        } else {
            RTK_LOGE(TAG, "Failed to open storage file %s for writing\n", path);
            return UTIL_ERR_FAIL;
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to find VFS region for file\n");
        return UTIL_ERR_FAIL;
    }
}

int32_t util_storage_load(uint8_t *data, uint32_t size)
{
    RTK_LOGD(TAG, "Attempting to load data from storage with buffer size %d\n", size);

    char *prefix = find_vfs_tag(VFS_REGION_1);
    if (prefix) {
        // char dir_path[MAX_PATH_LEN];
        // DiagSnPrintf(dir_path, MAX_PATH_LEN, "%s:%s", prefix, STORAGE_DIR_NAME);
        // mkdir(dir_path, 0); // Ensure the directory exists before trying to read the file
        char path[MAX_PATH_LEN];
        DiagSnPrintf(path, MAX_PATH_LEN, "%s:%s", prefix, STORAGE_FILE_NAME);
        FILE *f = fopen(path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (file_size <= (long)size) {
                fread(data, 1, file_size, f);
                fclose(f);
                return UTIL_SUCCESS;
            } else {
                RTK_LOGE(TAG, "Data size in storage %d exceeds buffer size %d\n", file_size, size);
                fclose(f);
                return UTIL_ERR_FAIL;
            }
        } else {
            RTK_LOGE(TAG, "Failed to open storage file %s for reading\n", path);
            return UTIL_ERR_FAIL;
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to find VFS region for file\n");
    }
    return UTIL_ERR_FAIL;
}

int64_t util_now_ms(void)
{
    return util_get_timestamp();
}

void util_msleep(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

util_mutex_t* util_mutex_create(void)
{
    return (util_mutex_t*)xSemaphoreCreateMutex();
}

void util_mutex_delete(util_mutex_t *mutex)
{
    vSemaphoreDelete(mutex);
}

int32_t util_mutex_lock(util_mutex_t *mutex, int32_t timeout)
{
    if (mutex == NULL) {
        return UTIL_ERR_FAIL;
    }

    TickType_t tick;
    if (timeout == MUTEX_WAIT_FOREVER) {
        tick = portMAX_DELAY;
    }
    else if (timeout >= 0) {
        uint32_t t = (uint32_t)timeout;
        tick = pdMS_TO_TICKS(t);
    }
    else {
        return UTIL_ERR_FAIL;
    }
    return xSemaphoreTake((SemaphoreHandle_t)mutex, tick) == pdTRUE ? UTIL_SUCCESS : UTIL_ERR_FAIL;
}

int32_t util_mutex_unlock(util_mutex_t *mutex)
{
    return xSemaphoreGive((SemaphoreHandle_t)mutex) == pdTRUE ? UTIL_SUCCESS : UTIL_ERR_FAIL;
}

int util_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    return ret;
}

uint8_t g_util_log_lv = UTIL_LOG_LV_INFO;
