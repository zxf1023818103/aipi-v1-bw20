#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MAX_PATH_LEN 256
#define MAX_ENV_VALUE_LENGTH 256

int load_all_env(void);
int save_all_env(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CONFIG_H__ */