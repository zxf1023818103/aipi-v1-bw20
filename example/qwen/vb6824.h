#ifndef __VB6824_H__
#define __VB6824_H__

#include <serial_api.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef CONFIG_AMEBADPLUS
    #define VB6824_UART_TX PA_26
    #define VB6824_UART_RX PB_21
#elif CONFIG_AMEBAGREEN2
    #define VB6824_UART_TX PA_18
    #define VB6824_UART_RX PA_3
#else
    #error "Unknown platform"
#endif

#define VB6824_UART_IDX 0
#define VB6824_UART_BAUDRATE 2000000
#define VB6824_UART_DATABITS 8
#define VB6824_UART_PARITY ParityNone
#define VB6824_UART_STOPBITS 1

#define VB6824_MAX_VOLUME 31
#define VB6824_MIN_VOLUME 5

/// 方向：VB6824 -> 上位机
/// 作用：传输麦克风数据
/// 编码格式：OPUS RAW
/// 帧长：20ms
/// 采样率：16000
/// 单声道
#define VB6824_CMD_REPORT_RECORD 0x2080

/// 方向：VB6824 -> 上位机
/// 作用：发送识别结构的数据格式
#define VB6824_CMD_REPORT_ASR 0x0180

/// 方向：上位机 -> VB6824
/// 作用：播放音频数据
/// 编码格式：PCM
/// 采样率：16000
/// 单声道
#define VB6824_CMD_PLAY 0x2081

/// 方向：上位机 -> VB6824
/// 作用：停止录音
#define VB6824_CMD_STOP_RECORD 0x0201

/// 方向：上位机 -> VB6824
/// 作用：设置音量
#define VB6824_CMD_SET_VOL 0x0203

/// 方向：上位机 -> VB6824
/// 作用：进入 OTA 模式
#define VB6824_CMD_REQUEST_UPGRADE 0x0205

/// 方向：上位机 -> VB6824
/// 作用：获取固件信息
#define VB6824_CMD_REQUEST_VERSION 0x0207

/// 方向：VB6824 -> 上位机
/// 作用：固件信息回复
#define VB6824_CMD_REPORT_VERSION 0x0107

/// 方向：上位机 -> VB6824
/// 作用：进入产测模式
#define VB6824_CMD_REQUEST_MP 0x0208

/// 方向：VB6824 -> 上位机
/// 作用：产测能量测试回复
#define VB6824_CMD_REPORT_MP 0x2088

#define JL_OTA_RPC_PORT 23333

#define JL_OTA_INIT_BAUDRATE 9600

#define JL_OTA_UPDATE_BAUDRATE 921600

#define JL_OTA_UPDATE_START 0x01

#define JL_OTA_UPDATE_READ 0x02

#define JL_OTA_UPDATE_STOP 0x03

#define JL_OTA_UPDATE_LEN 0x04

#define JL_OTA_UPDATE_KEEPALIVE 0x05

void vb6824_init(void);
void vb6824_send(uint16_t cmd, const uint8_t *data, uint16_t data_len);
void vb6824_set_volume(uint8_t volume);
void vb6824_wait_for_ota_exited(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __VB6824_H__ */
