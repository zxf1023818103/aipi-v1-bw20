#include "modem.h"

#include <basic_types.h>
#include <ameba_soc.h>
#include <atcmd_service.h>
#include <os_wrapper.h>
#include <stdlib.h>
#include <log.h>

#include <serial_ex_api.h>

#include <FreeRTOS.h>
#include <message_buffer.h>
#include <queue.h>
#include <semphr.h>

#include <lwip_netconf.h>
#include <lwip/sockets.h>

#define TAG "MODEM"

static serial_t modem_serial = { .uart_idx = MODEM_UART_IDX };

static SemaphoreHandle_t modem_dma_tx_done_sem;

static MessageBufferHandle_t modem_recv_mb;

static void modem_on_send_comp(uint32_t id)
{
    (void) id;
    xSemaphoreGiveFromISR(modem_dma_tx_done_sem, NULL);
}

static void modem_uart_irq_handler(uint32_t id, SerialIrq event)
{
    (void) id;

    static char line[128];
    static size_t line_len = 0;

    if (event == RxIrq) {
        while (serial_readable(&modem_serial)) {
            volatile int rc = serial_getc(&modem_serial);
            if (rc == '\n' || rc == '\r' || line_len >= sizeof line - 1) {
                if (line_len > 0) {
                    xMessageBufferSendFromISR(modem_recv_mb, line, line_len, NULL);
                    line[line_len] = 0;
                    line_len = 0;
                }
            }
            line[line_len++] = (char)rc;
        }
    }
}

static void modem_recv_routine(void *args)
{
    (void) args;

    char line[128];
    for (;;) {
        size_t line_len = xMessageBufferReceive(modem_recv_mb, line, sizeof line, portMAX_DELAY);
        if (line_len > 0) {
            line[line_len] = 0;
            RTK_LOGI(TAG, "%s\n", line);
        }
    }
}

void modem_init(void)
{
    modem_recv_mb = xMessageBufferCreate(128);
    modem_dma_tx_done_sem = xSemaphoreCreateBinary();
    xTaskCreate(modem_recv_routine, "modem_recv", 1024, NULL, 1, NULL);

    serial_init(&modem_serial, MODEM_UART_TX, MODEM_UART_RX);
    serial_baud(&modem_serial, MODEM_UART_INIT_BAUDRATE);
    serial_format(&modem_serial, MODEM_UART_DATABITS, MODEM_UART_PARITY, MODEM_UART_STOPBITS);
    serial_send_comp_handler(&modem_serial, modem_on_send_comp, 0);
    serial_irq_handler(&modem_serial, modem_uart_irq_handler, NULL);
    serial_irq_set(&modem_serial, RxIrq, 1);
}

void at_modem_set(u16 argc, char **argv)
{
    char line[128];
    int len = 0;
    if (argc == 2) {
        len = snprintf(line, sizeof line, "AT%s\r\n", argv[1]);
    }
    else if (argc > 2) {
        len = snprintf(line, sizeof line, "AT%s=", argv[1]);
        for (int i = 2; i < argc; i++) {
            len += snprintf(line + len, sizeof line - len, "%s", argv[i]);
            if (i < argc - 1) {
                len += snprintf(line + len, sizeof line - len, ",");
            }
        }
        len += snprintf(line + len, sizeof line - len, "\r\n");
    }
    else {
        RTK_LOGS(TAG, RTK_LOG_ERROR, "Invalid number of parameters\n");
    }
    RTK_LOGI(TAG, "> %s", line);
    serial_send_stream(&modem_serial, line, len);
}
