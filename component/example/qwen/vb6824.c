#include <basic_types.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <log.h>

#include "vb6824.h"
#include "ota.h"

#include <serial_ex_api.h>
#include <FreeRTOS.h>
#include <message_buffer.h>
#include <queue.h>
#include <semphr.h>

#include <httpc.h>
#include <lwip_netconf.h>
#include <lwip/sockets.h>
#include <cJSON.h>

#define TAG "VB6824"

typedef struct vb6824_frame {
    uint16_t cmd;
    uint8_t data[500];
} vb6824_frame_t;

static serial_t vb6824_serial = { .uart_idx = VB6824_UART_IDX };

static MessageBufferHandle_t vb6824_recv_frame_mb;

static SemaphoreHandle_t vb6824_dma_tx_done_sem;

static QueueHandle_t vb6824_version_queue;

static int f_is_ota_mode;

static int vb6824_uart_finite_state_machine(int prev_status, uint8_t input, uint16_t *cmd, uint8_t *data, uint16_t *data_len, uint16_t max_data_len, uint8_t *current_checksum, int *success)
{
    int reset_status = 0;
    if (prev_status == 0) {
        if (input != 0x55) {
            reset_status = 1;
        }
    }
    else if (prev_status == 1) {
        if (input != 0xaa) {
            reset_status = 1;
        }
    }
    else if (prev_status == 2) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *data_len = input_u16;
    }
    else if (prev_status == 3) {
        *data_len |= input;
        if (*data_len > max_data_len) {
            reset_status = 1;
        }
    }
    else if (prev_status == 4) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *cmd = input_u16;
    }
    else if (prev_status == 5) {
        *cmd |= input;
    }
    else if (prev_status < *data_len + 6) {
        data[prev_status - 6] = input;
    }
    else if (prev_status == *data_len + 6) {
        if (input == *current_checksum) {
            *success = 1;
            *current_checksum = 0;
            return 0;
        }
        else {
            reset_status = 1;
        }
    }
    else {
        reset_status = 1;
    }

    *success = 0;
    if (reset_status) {
        *current_checksum = 0;
        return 0;
    }
    else {
        *current_checksum += input;
        return prev_status + 1;
    }
}

static int jl_ota_uart_finite_state_machine(int prev_status, uint8_t input, uint8_t *data, uint16_t *data_len, uint16_t max_data_len, uint16_t *current_checksum, uint16_t *target_checksum, int *success)
{
    int reset_status = 0;
    if (prev_status == 0) {
        if (input != 0xaa) {
            reset_status = 1;
        }
    }
    else if (prev_status == 1) {
        if (input != 0x55) {
            reset_status = 1;
        }
    }
    else if (prev_status == 2) {
        *data_len = input;
    }
    else if (prev_status == 3) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *data_len |= input_u16;
        if (*data_len > max_data_len) {
            reset_status = 1;
        }
    }
    else if (prev_status < *data_len + 4) {
        data[prev_status - 4] = input;
    }
    else if (prev_status == *data_len + 4) {
        *target_checksum = input;
    }
    else if (prev_status == *data_len + 5) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *target_checksum |= input_u16;
        if (*target_checksum == *current_checksum) {
            *success = 1;
            *current_checksum = 0;
            return 0;
        }
        else {
            reset_status = 1;
        }
    }
    else {
        reset_status = 1;
    }

    *success = 0;
    if (reset_status) {
        *current_checksum = 0;
        return 0;
    }
    else {
        if (prev_status <= 3 || prev_status < *data_len + 4) {
            uint16_t data_u16 = input;
            data_u16 <<= 8;
            *current_checksum ^= data_u16;
            for (int i = 0; i < 8; i++) {
                if (*current_checksum & 0x8000) {
                    *current_checksum = (*current_checksum << 1) ^ 0x1021;
                }
                else {
                    *current_checksum <<= 1;
                }
            }
        }
        return prev_status + 1;
    }
}

static void vb6824_uart_irq_handler(uint32_t id, SerialIrq event)
{
    (void) id;
    
    if (event == RxIrq) {
        while (serial_readable(&vb6824_serial)) {
			volatile int rc = serial_getc(&vb6824_serial);
            uint8_t input = (uint8_t)rc;

            static int status, success;
            static uint16_t data_len;
            static union {
                vb6824_frame_t frame;
                uint8_t data[16];
            } buffer;
            if (!f_is_ota_mode) {
                static uint8_t current_checksum;
                status = vb6824_uart_finite_state_machine(status, input, &buffer.frame.cmd, buffer.frame.data, &data_len, sizeof buffer.frame.data, &current_checksum, &success);
                if (success) {
                    xMessageBufferSendFromISR(vb6824_recv_frame_mb, &buffer.frame, offsetof(vb6824_frame_t, data) + data_len, NULL);
                }
            }
            else {
                static uint16_t current_checksum, target_checksum;
                status = jl_ota_uart_finite_state_machine(status, input, buffer.data, &data_len, sizeof buffer.data, &current_checksum, &target_checksum, &success);
                if (success) {
                    xMessageBufferSendFromISR(vb6824_recv_frame_mb, buffer.data, data_len, NULL);
                }
            }
		}
    }
}

__weak void vb6824_on_report_record(uint8_t *data, size_t data_len)
{
    (void) data;
    (void) data_len;
}

__weak void vb6824_on_report_asr(uint8_t *data, size_t data_len)
{
    (void) data;
    (void) data_len;
}

static void vb6824_on_frame_recv(uint16_t cmd, size_t data_len, uint8_t *data)
{
    switch (cmd) {
        case VB6824_CMD_REPORT_ASR: {
            RTK_LOGI(TAG, "ASR data=%s\n", data);
            vb6824_on_report_asr(data, data_len);
            break;
        }
        case VB6824_CMD_REPORT_RECORD: {
            // RTK_LOGI(TAG, "RECORD data_len=%u\n", data_len);
            vb6824_on_report_record(data, data_len);
            break;
        }
        case VB6824_CMD_REPORT_VERSION: {
            RTK_LOGI(TAG, "VERSION data=%s\n", data);
            data_len = strlen((char*)data);
            char *version = pvPortMalloc(data_len + 1);
            if (version) {
                memcpy(version, data, data_len);
                version[data_len] = 0;
                while (xQueueSend(vb6824_version_queue, &version, 0) != pdTRUE) {
                    char *discard = NULL;
                    while (xQueueReceive(vb6824_version_queue, &discard, 0) == pdTRUE) {
                        vPortFree(discard);
                    }
                };
            }
            break;
        }
        case VB6824_CMD_REPORT_MP: {
            RTK_LOGI(TAG, "MP data=%s\n", data);
            break;
        }
        default: {
            break;
        }
    }
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        uint16_t data_u16 = data[i];
        data_u16 <<= 8;
        checksum ^= data_u16;
        for (int j = 0; j < 8; j++) {
            if (checksum & 0x8000) {
                checksum = (checksum << 1) ^ 0x1021;
            }
            else {
                checksum <<= 1;
            }
        }
    }
    return checksum;
}

static void jl_uart_send_packet(uint8_t opcode, void *data, uint16_t data_len)
{
    uint8_t buffer[32] = { 0xaa, 0x55, 0x00, 0x00, opcode };
    uint16_t cmd_len = data_len + 1;
    memcpy(buffer + 2, &cmd_len, 2);
    memcpy(buffer + 5, data, data_len);
    uint16_t checksum = crc16_ccitt(buffer, data_len + 5);
    memcpy(buffer + data_len + 5, &checksum, 2);
    serial_send_stream_dma(&vb6824_serial, (char*)buffer, data_len + 7);
    xSemaphoreTake(vb6824_dma_tx_done_sem, portMAX_DELAY);
}

static int jl_ota_http_read(struct httpc_conn *conn, char *path, uint32_t addr, uint8_t *data, uint32_t len)
{
    httpc_request_write_header_start(conn, "GET", path, NULL, 0);
    char range_header[32];
    snprintf(range_header, sizeof range_header, "bytes=%" PRIu32 "-%" PRIu32, addr, addr + len - 1);
    httpc_request_write_header(conn, "Range", range_header);
    httpc_request_write_header_finish(conn);
    if (httpc_response_read_header(conn) == 0) {
        if (httpc_response_is_status(conn, (char *)"206 Partial Content")) {
            uint32_t received_len = 0;
            while (received_len < len) {
                int read_size = httpc_response_read_data(conn, data + received_len, len - received_len);
                if (read_size > 0) {
                    received_len += read_size;
                }
                else {
                    RTK_LOGE(TAG, "Failed to read HTTP response body\n");
                    break;
                }
            }
            if (received_len == len) {
                return received_len;
            }
        }
        else {
            RTK_LOGE(TAG, "HTTP request failed with status other than 206 Partial Content\n");
        }
    }
    else {
        RTK_LOGE(TAG, "Failed to read HTTP response header\n");
    }

    return -1;
}

static void vb6824_recv_routine(void *args)
{
    (void) args;

    int rpc_socket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (rpc_socket >= 0) {
        struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons(JL_OTA_RPC_PORT),
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        };
        if (lwip_connect(rpc_socket, (const struct sockaddr*)&address, sizeof address) == 0) {
            RTK_LOGI(TAG, "RPC client is connected\n");
            for (;;) {
                if (!f_is_ota_mode) {
                    static vb6824_frame_t frame;
                    memset(&frame, 0, sizeof frame);
                    size_t frame_len = xMessageBufferReceive(vb6824_recv_frame_mb, &frame, sizeof frame, portMAX_DELAY);
                    if (frame_len >= offsetof(vb6824_frame_t, data)) {
                        size_t data_len = frame_len - offsetof(vb6824_frame_t, data);
                        vb6824_on_frame_recv(frame.cmd, data_len, frame.data);
                    }
                }
                else {
                    static uint8_t data[16];
                    memset(data, 0, sizeof data);
                    size_t data_len = xMessageBufferReceive(vb6824_recv_frame_mb, data, sizeof data, portMAX_DELAY);
                    if (data_len > 0) {
                        lwip_send(rpc_socket, data, data_len, 0);
                    }
                }
            }
        }
        else {
            RTK_LOGE(TAG, "Failed to connect RPC socket\n");
        }
        lwip_close(rpc_socket);
    }
}

static void vb6824_on_send_comp(uint32_t id)
{
    (void) id;
    xSemaphoreGiveFromISR(vb6824_dma_tx_done_sem, NULL);
}

static void jl_ota_init(void)
{
    f_is_ota_mode = 1;
    uint8_t mode = 1;
    vb6824_send(VB6824_CMD_REQUEST_UPGRADE, &mode, sizeof mode);
    serial_baud(&vb6824_serial, JL_OTA_INIT_BAUDRATE);
}

static int get_axk_ota_firmware_offset(struct httpc_conn *conn, char *path)
{
    char buffer[64];
    if (jl_ota_http_read(conn, path, 0, (uint8_t*)buffer, sizeof buffer) == sizeof buffer) {
        if (memcmp("axk", buffer, 3) == 0) {
            int space_count = 0;
            int i, success = 0;
            for (i = 0; i < (int)sizeof buffer; i++) {
                if (buffer[i] == ' ') {
                    space_count++;
                    if (space_count == 5) {
                        buffer[i] = 0;
                        success = 1;
                        break;
                    }
                }
            }
            if (success) {
                RTK_LOGI(TAG, "OTA head=%s\n", buffer);
                return i + 1;
            }
            else {
                return -1;
            }
        }
        else {
            return 0;
        }
    }
    return -1;
}

static void jl_do_ota_update(int rpc_responder_socket, char *host, uint16_t port, char *path, int use_tls)
{
    RTK_LOGI(TAG, "OTA host=%s port=%u path=%s\n", host, port, path);

    // httpc_setup_debug(HTTPC_DEBUG_VERBOSE);
    int ota_completed = 0;
    while (!ota_completed) {
        struct httpc_conn *conn = httpc_conn_new(use_tls ? HTTPC_SECURE_TLS : HTTPC_SECURE_NONE, NULL, NULL, NULL);
        if (conn) {
            if (httpc_conn_connect(conn, host, port, 0) == 0) {
                RTK_LOGI(TAG, "Connected to OTA server\n");
                jl_ota_init();
                int offset = get_axk_ota_firmware_offset(conn, path);
                if (offset >= 0) {
                    int ota_mode_entered = 0;
                    uint32_t baudrate = JL_OTA_INIT_BAUDRATE;
                    int retry_count = 0;
                    for (;;) {
                        if (!ota_mode_entered) {
                            RTK_LOGI(TAG, "Waiting for OTA update...\n");
                            jl_uart_send_packet(JL_OTA_UPDATE_START, &baudrate, sizeof baudrate);
                            retry_count++;
                            if (retry_count == 10) {
                                baudrate = JL_OTA_INIT_BAUDRATE;
                                serial_baud(&vb6824_serial, baudrate);
                            }
                        }
                        fd_set read_fds, except_fds;
                        FD_ZERO(&read_fds);
                        FD_SET(rpc_responder_socket, &read_fds);
                        FD_SET(conn->sock, &read_fds);
                        memcpy(&except_fds, &read_fds, sizeof except_fds);
                        struct timeval timeout = {
                            .tv_sec = ota_mode_entered ? 60 : 1,
                        };
                        int ret = select(conn->sock > rpc_responder_socket ? conn->sock + 1 : rpc_responder_socket + 1, &read_fds, NULL, &except_fds, &timeout);
                        if (ret >= 0) {
                            if (FD_ISSET(conn->sock, &except_fds)) {
                                RTK_LOGE(TAG, "HTTP connection error\n");
                                break;
                            }
                            if (FD_ISSET(conn->sock, &read_fds)) {
                                RTK_LOGI(TAG, "HTTP connection is readable\n");
                                if (httpc_response_read_header(conn) == 0) {
                                    uint8_t buffer[32];
                                    while (httpc_response_read_data(conn, buffer, sizeof buffer) > 0) {
                                        RTK_LOGI(TAG, "Received data: %.*s\n", (int)sizeof buffer, buffer);
                                    }
                                }
                                else {
                                    RTK_LOGE(TAG, "Failed to read HTTP response header\n");
                                    break;
                                }
                            }
                            if (FD_ISSET(rpc_responder_socket, &except_fds)) {
                                RTK_LOGE(TAG, "RPC connection error\n");
                                break;
                            }
                            if (FD_ISSET(rpc_responder_socket, &read_fds)) {
                                RTK_LOGI(TAG, "RPC connection is readable\n");
                                uint8_t buffer[32];
                                int buffer_len = lwip_recvfrom(rpc_responder_socket, buffer, sizeof buffer, 0, NULL, NULL);
                                if (buffer_len > 0) {
                                    rtk_log_memory_dump_byte(buffer, buffer_len);
                                    const uint8_t opcode = buffer[0];
                                    const uint8_t *data = buffer + 1;
                                    const int data_len = buffer_len - 1;
                                    switch (opcode) {
                                        case JL_OTA_UPDATE_START: {
                                            ota_mode_entered = 0;
                                            RTK_LOGI(TAG, "JL_OTA_UPDATE_START\n");
                                            baudrate = JL_OTA_UPDATE_BAUDRATE;
                                            retry_count = 0;
                                            jl_uart_send_packet(JL_OTA_UPDATE_START, &baudrate, sizeof baudrate);
                                            serial_baud(&vb6824_serial, baudrate);
                                            break;
                                        }
                                        case JL_OTA_UPDATE_READ: {
                                            if (data_len == 8) {
                                                ota_mode_entered = 1;
                                                uint32_t addr, len;
                                                memcpy(&addr, data, 4);
                                                memcpy(&len, data + 4, 4);
                                                RTK_LOGI(TAG, "JL_OTA_UPDATE_READ addr=%u len=%u\n", addr, len);
                                                const size_t buffer_len = len + 6 + 9;
                                                uint8_t *buffer = pvPortMalloc(buffer_len);
                                                if (buffer) {
                                                    buffer[0] = 0xaa;
                                                    buffer[1] = 0x55;
                                                    uint16_t cmd_len = len + 9;
                                                    memcpy(buffer + 2, &cmd_len, 2);
                                                    buffer[4] = JL_OTA_UPDATE_READ;
                                                    memcpy(buffer + 5, &addr, 4);
                                                    memcpy(buffer + 9, &len, 4);
                                                    if (jl_ota_http_read(conn, path, addr + offset, buffer + 13, len) >= 0) {
                                                        uint16_t checksum = crc16_ccitt((uint8_t*)buffer, len + 13);
                                                        memcpy(buffer + len + 13, &checksum, 2);
                                                        serial_send_stream_dma(&vb6824_serial, (char*)buffer, len + 15);
                                                        xSemaphoreTake(vb6824_dma_tx_done_sem, portMAX_DELAY);
                                                    }
                                                    vPortFree(buffer);
                                                }
                                            }
                                            break;
                                        }
                                        case JL_OTA_UPDATE_STOP: {
                                            if (data_len == 1) {
                                                ota_mode_entered = 1;
                                                int code = data[0];
                                                RTK_LOGI(TAG, "JL_OTA_UPDATE_STOP code=0x%02x\n", code);
                                                jl_uart_send_packet(JL_OTA_UPDATE_STOP, NULL, 0);
                                                ota_completed = 1;
                                            }
                                            else {
                                                RTK_LOGE(TAG, "Invalid data length for JL_OTA_UPDATE_STOP\n");
                                            }
                                            break;
                                        }
                                        case JL_OTA_UPDATE_LEN: {
                                            if (data_len == 4) {
                                                ota_mode_entered = 1;
                                                uint32_t len;
                                                memcpy(&len, data, 4);
                                                RTK_LOGI(TAG, "JL_OTA_UPDATE_LEN len=%u\n", len);
                                            }
                                            else {
                                                RTK_LOGE(TAG, "Invalid data length for JL_OTA_UPDATE_LEN\n");
                                            }
                                            break;
                                        }
                                        case JL_OTA_UPDATE_KEEPALIVE: {
                                            ota_mode_entered = 1;
                                            RTK_LOGI(TAG, "JL_OTA_UPDATE_KEEPALIVE\n");
                                            jl_uart_send_packet(JL_OTA_UPDATE_KEEPALIVE, NULL, 0);
                                            break;
                                        }
                                        default: {
                                            RTK_LOGE(TAG, "Unknown opcode %d\n", opcode);
                                            break;
                                        }
                                    }
                                }
                                else {
                                    RTK_LOGE(TAG, "Failed to receive RPC message\n");
                                }
                            }
                        }
                        else {
                            RTK_LOGE(TAG, "select error\n");
                            break;
                        }
                    }
                }
                else {
                    RTK_LOGE(TAG, "Failed to get OTA firmware offset\n");
                }
            }
            else {
                RTK_LOGE(TAG, "Failed to connect to server");
            }
        }
        else {
            RTK_LOGE(TAG, "Failed to create httpc connection");
        }
        httpc_conn_free(conn);
    }
}

/// OTA API 请求格式：
/// POST <path> HTTP/1.1
/// Content-Type: application/json
/// Connection: close
/// {"device_name": "<device_name>", "type": "vb6824", "version": "<version>"}
/// 响应（当前版本需要升级）：
/// HTTP/1.1 200 OK
/// Content-Type: application/json
/// Connection: close
/// {"success": 1, "data": {"host": "<http_host>", "port": <http_port>, "path": "<http_path>", "tls": <0_or_1>}}
/// 响应（当前版本不需要升级）：
/// HTTP/1.1 200 OK
/// Content-Type: application/json
/// Connection: close
/// {"success": 1, "data": null}
static cJSON *jl_ota_request_update_info(char *host, uint16_t port, char *path, int use_tls, char *device_name, char *version)
{
    cJSON *result = NULL;
    struct httpc_conn *conn = httpc_conn_new(use_tls ? HTTPC_SECURE_TLS : HTTPC_SECURE_NONE, NULL, NULL, NULL);
    if (conn) {
        if (httpc_conn_connect(conn, host, port, 0) == 0) {
            cJSON *json = cJSON_CreateObject();
            if (json) {
                cJSON_AddStringToObject(json, "device_name", device_name);
                cJSON_AddStringToObject(json, "type", "vb6824");
                cJSON_AddStringToObject(json, "version", version);
                char *request_body = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                size_t request_body_len = strlen(request_body);
                if (request_body) {
                    httpc_request_write_header_start(conn, "POST", path, "application/json", request_body_len);
                    httpc_request_write_header(conn, "Connection", "close");
                    httpc_request_write_header_finish(conn);
                    if (httpc_request_write_data(conn, (uint8_t*)request_body, request_body_len) == (int)request_body_len) {
                        httpc_response_read_header(conn);
                        if (httpc_response_is_status(conn, (char *)"200 OK")) {
                            size_t max_response_len = conn->response.content_len ? conn->response.content_len : 1024;
                            uint8_t *response = pvPortMalloc(max_response_len);
                            if (response) {
                                int total_size = 0;
                                while (1) {
                                    int read_size = httpc_response_read_data(conn, response + total_size, max_response_len - total_size - 1);
                                    if (read_size > 0) {
                                        total_size += read_size;
                                    }
                                    else {
                                        break;
                                    }

                                    char chunk[] = "chunked";
                                    if (conn->response.trans_enc && memcmp(conn->response.trans_enc, chunk, sizeof chunk - 1) == 0) {
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
                                    result = cJSON_ParseWithLength((char*)response, (size_t)total_size);
                                }
                                else {
                                    RTK_LOGE(TAG, "HTTP response is empty\n");
                                }
                                vPortFree(response);
                            }
                            else {
                                RTK_LOGE(TAG, "Failed to allocate memory for HTTP response\n");
                            }
                        }
                        else {
                            RTK_LOGE(TAG, "HTTP request failed with status other than 200\n");
                        }
                    }
                    cJSON_free(request_body);
                }
                else {
                    RTK_LOGE(TAG, "Failed to create OTA request body\n");
                }
            }
            else {
                RTK_LOGE(TAG, "Failed to create OTA request JSON object\n");
            }
        }
        else {
            RTK_LOGE(TAG, "Failed to connect to server");
        }
        httpc_conn_free(conn);
    }
    else {
        RTK_LOGE(TAG, "Failed to create httpc connection");
    }
    return result;                               
}

static void jl_ota_routine(void *args)
{
    (void) args;

    char *device_name = getenv("DEVICE_NAME");
    char *host = getenv("API_HOST");
    char *port_str = getenv("API_PORT");
    char *path = getenv("API_PATH");
    char *tls = getenv("API_TLS_ENABLED");

    int use_tls = 0;
    if (tls) {
        use_tls = atoi(tls);
    }

    uint16_t port = use_tls ? 443 : 80;
    if (port_str) {
        port = atoi(port_str);
    }

    if (host == NULL) {
        host = "aipi-v-bw-drdnfahoxy.cn-beijing.fcapp.run";
    }

    if (path == NULL) {
        path = "/ota";
    }

    if (host && path && device_name) {
        int rpc_responder_socket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
        if (rpc_responder_socket >= 0) {
            int reuse = 1;
            if (lwip_setsockopt(rpc_responder_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) == 0) {
                struct sockaddr_in address = {
                    .sin_family = AF_INET,
                    .sin_port = htons(JL_OTA_RPC_PORT),
                    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                };
                if (lwip_bind(rpc_responder_socket, (const struct sockaddr*)&address, sizeof address) == 0) {
                    RTK_LOGI(TAG, "RPC responder is starting\n");
                    char *version = NULL;
                    xQueueReceive(vb6824_version_queue, &version, portMAX_DELAY);
                    cJSON *result = jl_ota_request_update_info(host, port, path, use_tls, device_name, version);
                    vPortFree(version);
                    if (result) {
                        if (cJSON_IsObject(result)) {
                            cJSON *data = cJSON_GetObjectItem(result, "data");
                            if (cJSON_IsObject(data)) {
                                char *host = cJSON_GetObjectItem(data, "host")->valuestring;
                                uint16_t port = (uint16_t)cJSON_GetObjectItem(data, "port")->valuedouble;
                                char *path = cJSON_GetObjectItem(data, "path")->valuestring;
                                int tls = cJSON_GetObjectItem(data, "tls")->valueint;
                                jl_do_ota_update(rpc_responder_socket, host, port, path, tls);
                            }
                            else {
                                RTK_LOGI(TAG, "No OTA update needed\n");
                            }
                        }
                        else {
                            RTK_LOGE(TAG, "Invalid OTA response format\n");
                        }
                        cJSON_Delete(result);
                    }
                    else {
                        RTK_LOGE(TAG, "Failed to get OTA update info\n");
                    }
                }
                else {
                    RTK_LOGE(TAG, "Failed to bind RPC socket\n");
                }
            }
            else {
                RTK_LOGE(TAG, "Failed to set SO_REUSEADDR\n");
            }
            lwip_close(rpc_responder_socket);
        }
        else {
            RTK_LOGE(TAG, "Failed to create RPC socket\n");
        }
    }
    else {
        RTK_LOGE(TAG, "Missing OTA configuration\n");
    }

    vTaskDelete(NULL);
}

void vb6824_init(void)
{
    vb6824_recv_frame_mb = xMessageBufferCreate(512);
    vb6824_dma_tx_done_sem = xSemaphoreCreateBinary();
    vb6824_version_queue = xQueueCreate(1, sizeof(char*));
    xTaskCreate(vb6824_recv_routine, "vb6824_recv", 1024, NULL, 1, NULL);

    serial_init(&vb6824_serial, VB6824_UART_TX, VB6824_UART_RX);
    serial_baud(&vb6824_serial, VB6824_UART_BAUDRATE);
    serial_format(&vb6824_serial, VB6824_UART_DATABITS, VB6824_UART_PARITY, VB6824_UART_STOPBITS);
    serial_send_comp_handler(&vb6824_serial, vb6824_on_send_comp, 0);
    serial_irq_handler(&vb6824_serial, vb6824_uart_irq_handler, NULL);
    serial_irq_set(&vb6824_serial, RxIrq, 1);
    xTaskCreate(jl_ota_routine, "jl_ota", 1024, NULL, 1, NULL);
}

void vb6824_send(uint16_t cmd, const uint8_t *data, uint16_t data_len)
{
    static uint8_t packet[384] __attribute__((aligned(CACHE_LINE_SIZE)));

    if (data_len <= sizeof packet - 7) {
        uint8_t frame_header[6] = { 0x55, 0xaa, (uint8_t)(data_len >> 8), (uint8_t)data_len, (uint8_t)(cmd >> 8), (uint8_t)cmd };
        uint8_t checksum = frame_header[0] + frame_header[1] + frame_header[2] + frame_header[3] + frame_header[4] + frame_header[5];
        for (uint16_t i = 0; i < data_len; i++) {
            checksum += data[i];
        }

        size_t packet_len = data_len + sizeof frame_header + 1;
        memcpy(packet, frame_header, sizeof frame_header);
        memcpy(packet + sizeof frame_header, data, data_len);
        packet[sizeof frame_header + data_len] = checksum;
        vTaskDelay(pdMS_TO_TICKS(8));
        serial_send_stream_dma(&vb6824_serial, (char*)packet, packet_len);
        xSemaphoreTake(vb6824_dma_tx_done_sem, portMAX_DELAY);
    }
}

void vb6824_set_volume(uint8_t volume)
{
    vb6824_send(VB6824_CMD_SET_VOL, &volume, 1);
}
