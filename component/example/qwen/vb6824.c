#include <basic_types.h>
#include <ameba_soc.h>
#include <os_wrapper.h>

#include "vb6824.h"

#include <FreeRTOS.h>
#include <message_buffer.h>
#include <queue.h>

static serial_t s_obj = { .uart_idx = VB6824_UART_IDX };

static int vb6824_uart_finite_state_machine(int prev_status, uint8_t input, uint16_t *cmd, uint8_t *data, uint16_t *data_len, uint8_t *current_checksum, int *success)
{
    int reset_status = 0;
    if (prev_status == 0 && input == 0x55) {
    }
    else if (prev_status == 1 && input == 0xaa) {
    }
    else if (prev_status == 2) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *data_len |= input_u16;
    }
    else if (prev_status == 3) {
        *data_len |= input;
    }
    else if (prev_status == 4) {
        uint16_t input_u16 = input;
        input_u16 <<= 8;
        *cmd |= input_u16;
    }
    else if (prev_status == 5) {
        *cmd |= input;
    }
    else if (prev_status < *data_len + 5) {
        data[prev_status - 5] = input;
    }
    else if (prev_status == *data_len + 5 && input == *current_checksum) {
        *success = 1;
        reset_status = 1;
    }
    else {
        reset_status = 1;
    }

    if (reset_status) {
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
        while (serial_readable(&s_obj)) {
			volatile int rc = serial_getc(&s_obj);
            
		}
    }
}

void vb6824_init(void)
{
    serial_init(&s_obj, VB6824_UART_TX, VB6824_UART_RX);
    serial_baud(&s_obj, VB6824_UART_BAUDRATE);
    serial_format(&s_obj, VB6824_UART_DATABITS, VB6824_UART_PARITY, VB6824_UART_STOPBITS);

    serial_irq_handler(&s_obj, vb6824_uart_irq_handler, NULL);
    serial_irq_set(&s_obj, RxIrq, 1);
}
