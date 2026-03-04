#include <stdlib.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <os_wrapper_time.h>
#include <atcmd_service.h>
#include <httpc.h>
#include <wsclient_api.h>
#include <lwip/sockets.h>
#include <lwip_netconf.h>

#include <c_mmi.h>
#include <lib_c_license.h>
#include <qwen_test.h>

#include "hal.h"
#include "config.h"
#include "ntp.h"
#include "ali_cert.h"
#include "example_qwen.h"

#include <envlock.h>
#include <sys/unistd.h>

#define TAG "QWEN"

#define MMI_END_POINT "bailian.multimodalagent.aliyuncs.com"

static rtos_sema_t s_wss_ready_sem, s_audio_ready_sem;

static uint8_t s_audio_ready;

static int32_t mmi_event_callback(uint32_t event, void *param)
{
    char *text;

    text = param;
    switch (event) {
        case C_MMI_EVENT_USER_CONFIG: {
            RTK_LOGI(TAG, "C_MMI_EVENT_USER_CONFIG\n");
            c_mmi_set_voice_id("longanyang");
            c_mmi_reset_dialog_id();
            break;
        }
        case C_MMI_EVENT_DATA_INIT: {
            RTK_LOGI(TAG, "C_MMI_EVENT_DATA_INIT\n");
            rtos_sema_give(s_wss_ready_sem);
            break;
        }
        case C_MMI_EVENT_DATA_DEINIT: {
            RTK_LOGI(TAG, "C_MMI_EVENT_DATA_DEINIT\n");
            break;
        }
        case C_MMI_EVENT_SPEECH_START: {
            RTK_LOGI(TAG, "C_MMI_EVENT_SPEECH_START\n");
            // dummy_player_stop();
            // dummy_recorder_start();
            break;
        }
        case C_MMI_EVENT_ASR_START: {
            RTK_LOGI(TAG, "C_MMI_EVENT_ASR_START\n");
            break;
        }
        case C_MMI_EVENT_ASR_INCOMPLETE: {
            RTK_LOGI(TAG, "C_MMI_EVENT_ASR_INCOMPLETE text=%s\n", text);
            break;
        }
        case C_MMI_EVENT_ASR_COMPLETE: {
            RTK_LOGI(TAG, "C_MMI_EVENT_ASR_COMPLETE text=%s\n", text ? text : "(null)");
            break;
        }
        case C_MMI_EVENT_ASR_END: {
            RTK_LOGI(TAG, "C_MMI_EVENT_ASR_END\n");
            // dummy_recorder_stop();
            break;
        }
        case C_MMI_EVENT_LLM_INCOMPLETE: {
            // RTK_LOGI(TAG, "C_MMI_EVENT_LLM_INCOMPLETE text=%s\n", text);
            break;
        }
        case C_MMI_EVENT_LLM_COMPLETE:
            RTK_LOGI(TAG, "C_MMI_EVENT_LLM_COMPLETE text=%s\n", text);
            break;
        case C_MMI_EVENT_TTS_START: {
            RTK_LOGI(TAG, "C_MMI_EVENT_TTS_START\n");
            // rtos_sema_give(s_audio_ready_sem);
            s_audio_ready = 1;
            break;
        }
        case C_MMI_EVENT_TTS_END: {
            RTK_LOGI(TAG, "C_MMI_EVENT_TTS_END\n");
            // rtos_sema_take(s_audio_ready_sem, RTOS_SEMA_MAX_COUNT);
            s_audio_ready = 0;
            break;
        }
        default: {
            break;
        }
    }

    return UTIL_SUCCESS;
}

static cJSON* mmi_http_post_json(char *host, char *resource, uint8_t *content, size_t content_len)
{
    // httpc_setup_debug(HTTPC_DEBUG_VERBOSE);
    cJSON *json = NULL;
    struct httpc_conn *conn = httpc_conn_new(HTTPC_SECURE_TLS, NULL, NULL, (char*)g_bailian_cert);
    if (conn) {
        if (httpc_conn_connect(conn, host, 443, 0) == 0) {
            httpc_request_write_header_start(conn, "POST", resource, "application/json", content_len);
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
                                // RTK_LOGI(TAG, "Read response: %s\n", response);
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

static void mmi_ws_handler(wsclient_context **wsclient, int data_len, enum opcode_type opcode)
{
	wsclient_context *ws = *wsclient;
    c_mmi_analyze_recv_data(opcode, ws->receivedData, data_len);
}

wsclient_context *mmi_wss_connect(void)
{
    char *wss_host = c_mmi_get_wss_host();
    char *wss_port = c_mmi_get_wss_port();
    char *wss_api = c_mmi_get_wss_api();
    char *wss_header = c_mmi_get_wss_header();
    // char *wss_host_global = c_mmi_get_wss_host_global();

    // RTK_LOGI(TAG, "wss_host=%s\n", wss_host);
    // RTK_LOGI(TAG, "wss_host_global=%s\n", wss_host_global);
    // RTK_LOGI(TAG, "wss_port=%s\n", wss_port);
    // RTK_LOGI(TAG, "wss_api=%s\n", wss_api);
    // RTK_LOGI(TAG, "wss_header=%s\n", wss_header);

    char url[32];
    snprintf(url, sizeof url, "wss://%s", wss_host);
    wsclient_context* ws = create_wsclient(url, atoi(wss_port), wss_api + 1, NULL, 1024 * 8, 1024 * 8, 1);
    if (ws) {
        ws_dispatch(mmi_ws_handler);
        ws->ca_cert = (char*)g_dashscope_cert;
        ws_handshake_header_custom_token(ws, wss_header, strlen(wss_header));
        int ret = ws_connect_url(ws);
        if (ret >= 0) {
            return ws;
        }
        else {
            RTK_LOGE(TAG, "Failed to connect to %s\n", url);
        }
        ws_close(&ws);
    }
    return NULL;
}

/// @brief 设备注册
/// @return 
static int device_register(char *ws_id, char *app_id, char *app_secret, char *device_name)
{
    int ret = UTIL_ERR_FAIL;
    if (c_license_device_is_registered() == 0) {
        c_mmi_storage_reset();
        c_mmi_storage_set_ws_id(ws_id);
        c_mmi_storage_set_app_id_str(app_id);
        c_license_set_app_secret_str(app_secret);
        c_mmi_set_device_name(device_name);

        char time_ms_str[C_UTIL_TIMESTAMP_MS_LEN + 1];
        snprintf(time_ms_str, sizeof time_ms_str, "%" PRId64, util_get_timestamp());

        char request[512];
        if (c_license_gen_register_str(request, sizeof request, time_ms_str) == UTIL_SUCCESS) {
            cJSON *json = mmi_http_post_json(MMI_END_POINT, "/api/device/v1/register", (uint8_t*)request, strlen(request));
            if (json) {
                cJSON *data = cJSON_GetObjectItem(json, "data");
                if (data && !cJSON_IsNull(data)) {
                    char *data_str = cJSON_Print(data);
                    if (data_str) {
                        int32_t err = c_license_analyze_register_rsp(data_str);
                        if (err == UTIL_SUCCESS) {
                            ret = c_mmi_storage_save();
                        }
                        cJSON_free(data_str);
                    }
                }
                else {
                    RTK_LOGE(TAG, "Failed to find data object\n");
                }
                cJSON_Delete(json);
            } else {
                RTK_LOGE(TAG, "Failed to get register response from license server\n");
            }
        }
    }
    else {
        ret = UTIL_SUCCESS;
    }
    return ret;
}

/// @brief 设备登录
/// @return 
static int device_login(char *api_key)
{
    int ret = UTIL_ERR_FAIL;
    if (c_license_is_token_expire(util_get_timestamp()) == 0) {
        char time_ms_str[C_UTIL_TIMESTAMP_MS_LEN + 1];
        snprintf(time_ms_str, sizeof time_ms_str, "%" PRId64, util_get_timestamp());
        char request[512];
        if (c_license_gen_get_token_str(request, sizeof request, time_ms_str, api_key) == UTIL_SUCCESS) {
            // 获取服务端返回登录信息
            cJSON *json = mmi_http_post_json(MMI_END_POINT, "/api/token/v1/getToken", (uint8_t*)request, strlen(request));
            if (json) {
                cJSON *data = cJSON_GetObjectItem(json, "data");
                if (data && !cJSON_IsNull(data)) {
                    char *data_str = cJSON_Print(data);
                    if (data_str) {
                        int32_t err = c_license_analyze_get_token_rsp(data_str);
                        if (err == UTIL_SUCCESS) {
                            ret = c_mmi_storage_save();
                        }
                        cJSON_free(data_str);
                    }
                }
                else {
                    RTK_LOGE(TAG, "Failed to find data object\n");
                }
            }
            else {
                RTK_LOGE(TAG, "Failed to get token response from license server\n");
            }
        }
    }
    else {
        ret = UTIL_SUCCESS;
    }
    return ret;
}

/// @brief License 模式初始化
/// @param  
/// @return 
int qwen_license_sdk_init(char *ws_id, char *app_id, char *app_secret, char *device_name, char *api_key)
{
    if (c_mmi_sdk_init() == UTIL_SUCCESS) {
        mmi_user_config_t mmi_config = C_MMI_CONFIG_DEFAULT();
        mmi_config.evt_cb = mmi_event_callback;
        mmi_config.text_mode = C_MMI_TEXT_MODE_LLM_ONLY;
        mmi_config.work_mode = C_MMI_MODE_PUSH2TALK;
        c_mmi_config(&mmi_config);
        c_mmi_storage_set_api_key(api_key);
        
        if (device_register(ws_id, app_id, app_secret, device_name) == UTIL_SUCCESS) {
            return device_login(api_key);
        }
    }
    return UTIL_ERR_FAIL;
}

static void wss_routine(void *args) {
    (void) args;
    for (;;) {
        rtos_sema_take(s_wss_ready_sem, RTOS_SEMA_MAX_COUNT);
        wsclient_context *ws = mmi_wss_connect();
        if (ws) {
            for (;;) {
                ws_poll(10000, &ws);
                if (ws->readyState != WSC_CLOSED) {
                    uint8_t opcode;
                    static uint8_t data[1024 * 8];
                    size_t len = c_mmi_get_send_data(&opcode, data, sizeof data);
                    if (len > 0) {
                        if (ws_send_with_opcode((char*)data, len, 1, opcode, 1, ws) != 0) {
                            RTK_LOGE(TAG, "ws_send_with_opcode failed\n");
                            break;
                        }
                    }
                }
                else {
                    break;
                }
            }
            ws_close(&ws);
            ws_free(ws);
        }
    }
}

void qwen_sdk_init_routine(void *arg)
{
    (void) arg;

    load_all_env();

    while (LwIP_Check_Connectivity(NETIF_WLAN_STA_INDEX) != CONNECTION_VALID) {
		rtos_time_delay_ms(1000);
	}

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
        if (qwen_license_sdk_init(ws_id, app_id, app_secret, device_name, api_key) == UTIL_SUCCESS) {
            RTK_LOGI(TAG, "SDK Init Done\n");
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Missing required env variables for license initialization\n");
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Please set WS_ID, APP_ID, APP_SECRET, DEVICE_NAME, and API_KEY env variables\n");
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Start qwen_sdk_test without license initialization\n");
        qwen_sdk_test_init();
        // qwen_sdk_test();
        util_storage_erase();
    }

    rtos_task_delete(NULL);
}

static void audio_routine(void *args)
{
    (void) args;

    static uint8_t audio_data[8 * 1024];
    for (;;) {
        while (s_audio_ready == 0) {
            rtos_time_delay_ms(1000);
        }
        while (1) {
            uint32_t nbytes_read = c_mmi_get_player_data(audio_data, sizeof audio_data);
            if (nbytes_read != 0) {
                RTK_LOGI(TAG, "c_mmi_get_player_data %u bytes\n", nbytes_read);
            }
            else {
                break;
            }
        }
        s_audio_ready = 0;
    }
}

void app_example(void)
{
    rtos_sema_create_binary(&s_wss_ready_sem);
    rtos_sema_create_binary(&s_audio_ready_sem);
    if (rtos_task_create(NULL, "qwen_sdk_init", qwen_sdk_init_routine, NULL, 1024 * 8, 1) != RTK_SUCCESS) {
		RTK_LOGE(TAG, "%s rtos_task_create qwen_sdk_init failed\n", __FUNCTION__);
	}
    rtos_task_create(NULL, "wss", wss_routine, NULL, 1024 * 4, 1);
    rtos_task_create(NULL, "audio", audio_routine, NULL, 1024 * 4, 2);
}

void at_chat_set(u16 argc, char **argv)
{
    if (argc == 2) {
        char *text = argv[1];
        if (c_mmi_question(text) == 0) {
            at_printf("\r\nOK\r\n");
            return;
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Invalid number of parameters\n");
    }
    at_printf("\r\nERROR\r\n");
}

void at_tts_set(u16 argc, char **argv)
{
    if (argc == 2) {
        char *text = argv[1];
        if (c_mmi_tts(text) == 0) {
            at_printf("\r\nOK\r\n");
            return;
        }
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Invalid number of parameters\n");
    }
    at_printf("\r\nERROR\r\n");
}

void at_pause_speech(u16 argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (c_mmi_speech_pause() == 0) {
        at_printf("\r\nOK\r\n");
    }
    else {
        at_printf("\r\nERROR\r\n");
    }
}
