#include "ota.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#define TAG "OTA"

static SemaphoreHandle_t wifi_is_connected_sem;

void ota_init(void)
{
    wifi_is_connected_sem = xSemaphoreCreateBinary();
}

void ota_set_wifi_connected(int connected)
{
    if (connected) {
        xSemaphoreGive(wifi_is_connected_sem);
    }
    else {
        xSemaphoreTake(wifi_is_connected_sem, portMAX_DELAY);
    }
}

void ota_wait_for_wifi_connection(void)
{
    xSemaphoreTake(wifi_is_connected_sem, portMAX_DELAY);
    xSemaphoreGive(wifi_is_connected_sem);
}
