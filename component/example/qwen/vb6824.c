#include <basic_types.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <log.h>

#include "vb6824.h"

#include <serial_ex_api.h>
#include <FreeRTOS.h>
#include <message_buffer.h>
#include <queue.h>
#include <semphr.h>

#define TAG "VB6824"

typedef struct vb6824_frame {
    uint16_t cmd;
    uint8_t data[500];
} vb6824_frame_t;

static serial_t vb6824_serial = { .uart_idx = VB6824_UART_IDX };

static MessageBufferHandle_t vb6824_recv_frame_mb;

static SemaphoreHandle_t vb6824_dma_tx_done_sem;

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

static void vb6824_uart_irq_handler(uint32_t id, SerialIrq event)
{
    (void) id;
    
    if (event == RxIrq) {
        while (serial_readable(&vb6824_serial)) {
			volatile int rc = serial_getc(&vb6824_serial);
            uint8_t input = (uint8_t)rc;

            static int status, success;
            static uint16_t data_len;
            static uint8_t current_checksum;
            static vb6824_frame_t frame;
            status = vb6824_uart_finite_state_machine(status, input, &frame.cmd, frame.data, &data_len, sizeof frame.data, &current_checksum, &success);
            if (success) {
                xMessageBufferSendFromISR(vb6824_recv_frame_mb, &frame, offsetof(vb6824_frame_t, data) + data_len, NULL);
            }
		}
    }
}

static void vb6824_on_frame_recv(uint16_t cmd, size_t data_len, uint8_t *data)
{
    (void) data;
    switch (cmd) {
        case VB6824_CMD_REPORT_ASR: {
            RTK_LOGI(TAG, "ASR data=%s\n", data);
            break;
        }
        case VB6824_CMD_REPORT_RECORD: {
            // RTK_LOGI(TAG, "VB6824_CMD_REPORT_RECORD data_len=%u\n", data_len);
            break;
        }
        case VB6824_CMD_REPORT_VERSION: {
            RTK_LOGI(TAG, "VERSION data=%s\n", data);
            break;
        }
        case VB6824_CMD_REPORT_MP: {
            RTK_LOGI(TAG, "MP data=\n");
            rtk_log_memory_dump_byte(data, data_len);
            break;
        }
        default: {
            break;
        }
    }
}

static void vb6824_recv_routine(void *args)
{
    (void) args;

    for (;;) {
        static vb6824_frame_t frame;
        memset(&frame, 0, sizeof frame);
        size_t frame_len = xMessageBufferReceive(vb6824_recv_frame_mb, &frame, sizeof frame, portMAX_DELAY);
        if (frame_len >= offsetof(vb6824_frame_t, data)) {
            size_t data_len = frame_len - offsetof(vb6824_frame_t, data);
            vb6824_on_frame_recv(frame.cmd, data_len, frame.data);
        }
    }
}

static void vb6824_on_send_comp(uint32_t id)
{
    (void) id;
    xSemaphoreGiveFromISR(vb6824_dma_tx_done_sem, NULL);
}

void vb6824_init(void)
{
    vb6824_recv_frame_mb = xMessageBufferCreate(512);
    vb6824_dma_tx_done_sem = xSemaphoreCreateBinary();
    xTaskCreate(vb6824_recv_routine, "vb6824_recv", 1024, NULL, 1, NULL);
    
    serial_init(&vb6824_serial, VB6824_UART_TX, VB6824_UART_RX);
    serial_baud(&vb6824_serial, VB6824_UART_BAUDRATE);
    serial_format(&vb6824_serial, VB6824_UART_DATABITS, VB6824_UART_PARITY, VB6824_UART_STOPBITS);
    serial_send_comp_handler(&vb6824_serial, vb6824_on_send_comp, 0);
    serial_irq_handler(&vb6824_serial, vb6824_uart_irq_handler, NULL);
    serial_irq_set(&vb6824_serial, RxIrq, 1);
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
        DCache_Clean((uint32_t)packet, packet_len);
        vTaskDelay(pdMS_TO_TICKS(8));
        serial_send_stream_dma(&vb6824_serial, (char*)packet, packet_len);
        xSemaphoreTake(vb6824_dma_tx_done_sem, portMAX_DELAY);
    }
}

void vb6824_set_volume(uint8_t volume)
{
    vb6824_send(VB6824_CMD_SET_VOL, &volume, 1);
}
