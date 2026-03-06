#ifndef __VB6824_H__
#define __VB6824_H__

#include <serial_api.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define VB6824_UART_TX PA_26
#define VB6824_UART_RX PB_21
#define VB6824_UART_IDX 0
#define VB6824_UART_BAUDRATE 2000000
#define VB6824_UART_DATABITS 8
#define VB6824_UART_PARITY ParityNone
#define VB6824_UART_STOPBITS 1

void vb6824_init(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __VB6824_H__ */
