#include <stdlib.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <httpc.h>

#include <c_mmi.h>
#include <lib_c_license.h>
#include <qwen_test.h>

#include "hal.h"
#include "config.h"
#include "ntp.h"
#include "ali_cert.h"

#include <envlock.h>
#include <sys/unistd.h>

#define TAG "QWEN"

static int32_t mmi_event_callback(uint32_t event, void *param)
{
    char *text;

    text = param;
    switch (event) {
        case C_MMI_EVENT_USER_CONFIG:
            // 开始新对话
            c_mmi_reset_dialog_id();
            break;
        case C_MMI_EVENT_DATA_INIT:
            // Mmi data ready, 开始网络连接
            // dummy_wss_init();
            break;
        case C_MMI_EVENT_DATA_DEINIT:
            RTK_LOGW(TAG, "will disconnect");
            break;
        case C_MMI_EVENT_SPEECH_START:
            RTK_LOGD(TAG, "enable recorder when send speech");
            // dummy_player_stop();
            // dummy_recorder_start();
            break;
        case C_MMI_EVENT_ASR_START:
            RTK_LOGI(TAG, "event [C_MMI_EVENT_ASR_START]");
            break;
        case C_MMI_EVENT_ASR_INCOMPLETE:
            RTK_LOGD(TAG, "ASR [%s]", text);
            break;
        case C_MMI_EVENT_ASR_COMPLETE:
            if (text) {
                RTK_LOGD(TAG, "ASR C [%s]", text);
            } else {
                RTK_LOGD(TAG, "ASR C [NULL]");
            }
            break;
        case C_MMI_EVENT_ASR_END:
            RTK_LOGD(TAG, "disable record when ASR complete");
            // dummy_recorder_stop();
            break;
        case C_MMI_EVENT_LLM_INCOMPLETE:
            RTK_LOGD(TAG, "LLM [%s]", text);
            break;
        case C_MMI_EVENT_LLM_COMPLETE:
            RTK_LOGD(TAG, "LLM C [%s]", text);
            break;
        case C_MMI_EVENT_TTS_START:
            RTK_LOGI(TAG, "enable player when dialog start");
            // dummy_player_start();
            break;
        case C_MMI_EVENT_TTS_END:
            break;
        default:
            break;
    }

    return UTIL_SUCCESS;
}

static cJSON* mmi_http_request(char *host, char *method, char *resource, char *content_type, uint8_t *content, size_t content_len)
{
    // httpc_setup_debug(HTTPC_DEBUG_VERBOSE);
    cJSON *json = NULL;
    struct httpc_conn *conn = httpc_conn_new(HTTPC_SECURE_TLS, NULL, NULL, g_ali_cert);
    if (conn) {
        if (httpc_conn_connect(conn, host, 443, 0) == 0) {
            httpc_request_write_header_start(conn, method, resource, content_type, content_len);
            httpc_request_write_header(conn, "Connection", "close");
            httpc_request_write_header_finish(conn);
            int ret = httpc_request_write_data(conn, content, content_len);
            if (ret > 0 && (size_t)ret == content_len) {
                if (httpc_response_read_header(conn) == 0) {
                    // httpc_conn_dump_header(conn);
                    if (httpc_response_is_status(conn, (char *)"200 OK")) {
                        size_t max_response_len = 1024;
                        uint8_t *response = util_malloc(max_response_len);
                        if (response) {
                            int total_size = 0;
                            memset(response, 0, max_response_len);
                            while (1) {
                                int read_size = httpc_response_read_data(conn, response + total_size, max_response_len - total_size - 1);
                                if (read_size > 0) {
                                    total_size += read_size;
                                }
                                else {
                                    break;
                                }

                                char chunk[] = "chunked";
                                /* chunked read */
                                if (conn->response.trans_enc && memcmp(conn->response.trans_enc, chunk, strlen(chunk)) == 0) {
                                    if (conn->response.trans_chunk_len == 0) {
                                        break;
                                    }
                                }
                                else {
                                    if (conn->response.content_len && (size_t)total_size >= conn->response.content_len) {
                                        break;
                                    }
                                }
                            }

                            if (total_size > 0) {
                                RTK_LOGI(TAG, "Read response: %s\n", response);
                                json = cJSON_Parse((char*)response);
                            }
                            else {
                                RTK_LOGE(TAG, "HTTP response is empty\n");
                            }

                            util_free(response);
                        }
                    } else {
                        RTK_LOGE(TAG, "HTTP request failed with status other than 200\n");
                    }
                }
                else {
                    RTK_LOGE(TAG, "Failed to read HTTP response header\n");
                }
            }
            else {
                RTK_LOGE(TAG, "Failed to send HTTP request body, ret = %d\n", ret);
            }
            httpc_conn_close(conn);
        } else {
            RTK_LOGE(TAG, "Failed to connect to server");
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to create httpc connection");
    }
    httpc_conn_free(conn);
    return json;
}

/// @brief License 模式初始化
/// @param  
/// @return 
int qwen_license_sdk_init(char *ws_id, char *app_id, char *app_secret, char *device_name, char *api_key)
{
    if (c_mmi_sdk_init() == UTIL_SUCCESS) {
        mmi_user_config_t mmi_config = C_MMI_CONFIG_DEFAULT();
        // 必须要配置evt_cb，否则会导致sdk运行异常
        mmi_config.evt_cb = mmi_event_callback;  // 注册事件回调函数，详细说明见下文
        // 配置工作模式
        mmi_config.work_mode = C_MMI_MODE_PUSH2TALK;
        mmi_config.text_mode = C_MMI_TEXT_MODE_BOTH;
        // 配置上下行音频数据格式
        mmi_config.upstream_mode = C_MMI_STREAM_MODE_OPUS_RAW;
        mmi_config.downstream_mode = C_MMI_STREAM_MODE_OPUS_RAW;
        // 配置缓冲区大小
        mmi_config.recorder_rb_size = 8 * 1024;
        mmi_config.player_rb_size = 8 * 1024;

        c_mmi_config(&mmi_config);
        // 设置音色，需要在 c_mmi_config 后调用
        c_mmi_set_voice_id("longxiaochun_v2");
        
        if (c_license_device_is_registered() == 0) {
            c_mmi_storage_reset();
            c_mmi_storage_set_ws_id(ws_id);
            c_mmi_storage_set_app_id_str(app_id);
            c_license_set_app_secret_str(app_secret);
            c_mmi_set_device_name(device_name);

            char time_ms_str[14];
            snprintf(time_ms_str, sizeof(time_ms_str), "%" PRId64, util_get_timestamp());
            // 根据时间戳timestamp，生成注册信息字串req
            char request[512];
            if (c_license_gen_register_str(request, sizeof request, time_ms_str) == UTIL_SUCCESS) {
                cJSON *json = mmi_http_request("bailian.multimodalagent.aliyuncs.com", "POST", "/api/device/v1/register", "application/json", (uint8_t*)request, strlen(request));
                if (json) {
                    cJSON *data = cJSON_GetObjectItem(json, "data");
                    if (data && !cJSON_IsNull(data)) {
                        char *data_str = cJSON_Print(data);
                        if (data_str) {
                            int32_t err = c_license_analyze_register_rsp(data_str);
                            if (err == UTIL_SUCCESS) {
                                RTK_LOGI(TAG, "Device registration successful\n");
                                c_mmi_storage_save();
                            } else {
                                RTK_LOGE(TAG, "Device registration failed with error code: %d\n", err);
                            }
                            cJSON_free(data_str);
                        }
                    }
                    else {
                        RTK_LOGE(TAG, "Failed to find data object\n");
                    }
                    cJSON_Delete(json);
                } else {
                    RTK_LOGE(TAG, "Failed to get response from license server\n");
                }
            }
        }
        c_mmi_storage_set_api_key(api_key);
        return 0;
    } else {
        return -1;
    }
}

void qwen_sdk_test_routine(void *arg)
{
    (void) arg;
    
    load_all_env();
    ntp_init();

    while (!util_timestamp_inited()) {
        util_msleep(1000);
    }

    char *ws_id = getenv("WS_ID");
    char *app_id = getenv("APP_ID");
    char *app_secret = getenv("APP_SECRET");
    char *device_name = getenv("DEVICE_NAME");
    char *api_key = getenv("API_KEY");
    if (ws_id && app_id && app_secret && device_name && api_key) {
        qwen_license_sdk_init(ws_id, app_id, app_secret, device_name, api_key);
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Missing required env variables for license initialization\n");
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Please set WS_ID, APP_ID, APP_SECRET, DEVICE_NAME, and API_KEY env variables\n");
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Start qwen_sdk_test without license initialization\n");
        qwen_sdk_test_init();
        qwen_sdk_test();
        util_storage_erase();
    }

    rtos_task_delete(NULL);
}

void app_example(void)
{
    if (rtos_task_create(NULL, "qwen_sdk_test", qwen_sdk_test_routine, NULL, 1024 * 8, 1) != RTK_SUCCESS) {
		printf("\n\r%s rtos_task_create qwen_sdk_test failed", __FUNCTION__);
	}
}
