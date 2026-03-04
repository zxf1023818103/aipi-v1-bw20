#include <atcmd_service.h>

#include "config.h"
#include "example_qwen.h"

ATCMD_APONLY_TABLE_DATA_SECTION
const log_item_t at_custom_items[] = {
	{"+LINKKEYCONFIG", at_linkkeyconfig},
	{"+LINKKEYCONFIG?", at_linkkeyconfig_query},
    {"+ENV?", at_env_query},
    {"+ENV", at_env_set},
    {"+CHAT", at_chat_set},
    {"+TTS", at_tts_set},
    {"+SPEECHPAUSE", at_pause_speech},
};
