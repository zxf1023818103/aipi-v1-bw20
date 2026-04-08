#include "ntp.h"
#include "config.h"

#include <c_utils/hal_util_time.h>
#include <stdlib.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <log.h>
#include <sntp_api.h>
#include <lwip/apps/sntp.h>

#define TAG "NTP"

int64_t util_get_timestamp(void)
{
    uint32_t sec;
	uint32_t us;
	SNTP_GET_SYSTEM_TIME(sec, us);
    int64_t timestamp_ms = (int64_t)sec * 1000 + us / 1000;
    return timestamp_ms;
}

int64_t util_get_timestamp_ms(void)
{
    return util_get_timestamp();
}

static uint8_t timestamp_inited;

uint8_t util_timestamp_inited(void)
{
    return timestamp_inited;
}

static void load_timezone(void)
{
    const char *tz_env_key = "TZ";
    const char *default_tz_env_value = "CST-8";

    const char *tz_env_value = getenv(tz_env_key);
    if (tz_env_value) {
        RTK_LOGS(TAG, RTK_LOG_INFO, "Loaded timezone from env %s: %s\n", tz_env_key, tz_env_value);
        tzset();
    } else {
        RTK_LOGS(TAG, RTK_LOG_INFO, "Reset timezone env %s to default: %s\n", tz_env_key, default_tz_env_value);
        setenv(tz_env_key, default_tz_env_value, 1);
        load_timezone();
    }
}

static void load_ntp_server(void)
{
    const char *ntp_server_env_key = "NTP_SERVER";
    const char *default_ntp_server_env_value = "ntp.aliyun.com";
    char *ntp_server_env_value = getenv(ntp_server_env_key);
    if (ntp_server_env_value) {
        RTK_LOGS(TAG, RTK_LOG_INFO, "Loaded NTP server from env %s: %s\n", ntp_server_env_key, ntp_server_env_value);
        sntp_setservername(0, ntp_server_env_value);
    } else {
        RTK_LOGS(TAG, RTK_LOG_INFO, "Reset NTP server env %s to default: %s\n", ntp_server_env_key, default_ntp_server_env_value);
        setenv(ntp_server_env_key, default_ntp_server_env_value, 1);
        load_ntp_server();
    }
}

void on_sntp_time_updated(uint32_t sec, uint32_t us)
{
    (void) us;
    if (!timestamp_inited) {
        timestamp_inited = 1;
        time_t now = (time_t)sec;
        struct tm timeinfo = *(localtime(&now));
        char *tz = getenv("TZ");
        RTK_LOGS(TAG, RTK_LOG_INFO, "SNTP Time Updated: %04d-%02d-%02d %02d:%02d:%02d %s\n",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, tz ? tz : "NULL");
    }
}

void ntp_init(void)
{
    load_timezone();
    load_ntp_server();
}

void ntp_start(void)
{
    sntp_init();
}
