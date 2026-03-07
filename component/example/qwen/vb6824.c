#include <basic_types.h>
#include <ameba_soc.h>
#include <os_wrapper.h>
#include <log.h>

#include "vb6824.h"

#include <FreeRTOS.h>
#include <message_buffer.h>
#include <queue.h>

#define TAG "VB6824"

typedef struct vb6824_frame {
    uint16_t cmd;
    uint8_t data[510];
} vb6824_frame_t;

static serial_t vb6824_serial = { .uart_idx = VB6824_UART_IDX };

static MessageBufferHandle_t vb6824_recv_frame_mb;

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

void vb6824_init(void)
{
    vb6824_recv_frame_mb = xMessageBufferCreate(1024);
    xTaskCreate(vb6824_recv_routine, "vb6824_recv", 1024, NULL, 1, NULL);

    serial_init(&vb6824_serial, VB6824_UART_TX, VB6824_UART_RX);
    serial_baud(&vb6824_serial, VB6824_UART_BAUDRATE);
    serial_format(&vb6824_serial, VB6824_UART_DATABITS, VB6824_UART_PARITY, VB6824_UART_STOPBITS);

    serial_irq_handler(&vb6824_serial, vb6824_uart_irq_handler, NULL);
    serial_irq_set(&vb6824_serial, RxIrq, 1);
}
