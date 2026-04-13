#ifndef __MODEM_H__
#define __MODEM_H__

#include <serial_api.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef CONFIG_AMEBADPLUS
    #define MODEM_UART_TX PA_29
    #define MODEM_UART_RX PA_28
#elif defined CONFIG_AMEBAGREEN2
    #define MODEM_UART_TX PA_5
    #define MODEM_UART_RX PA_4
#else
    #error "Unknown platform"
#endif

#define MODEM_UART_IDX 1
#define MODEM_UART_INIT_BAUDRATE 115200
#define MODEM_UART_DATABITS 8
#define MODEM_UART_PARITY ParityNone
#define MODEM_UART_STOPBITS 1

void modem_init(void);

void at_modem_set(u16 argc, char **argv);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MODEM_H__ */
