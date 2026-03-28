#ifndef __OTA_H__
#define __OTA_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void ota_init(void);
void ota_set_wifi_connected(int is_connected);
void ota_wait_for_wifi_connection(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __OTA_H__ */
