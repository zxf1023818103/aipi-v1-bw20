#ifndef __SOUND_H__
#define __SOUND_H__

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const uint8_t g_start_recording_pcm[];

extern const size_t g_start_recording_pcm_len;

extern const uint8_t g_stop_recording_pcm[];

extern const size_t g_stop_recording_pcm_len;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SOUND_H__ */
