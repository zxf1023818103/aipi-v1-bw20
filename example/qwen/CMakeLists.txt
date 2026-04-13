ameba_internal_library(example_qwen)

target_sources(
    ${CURRENT_LIB_NAME} PRIVATE
    ali_cert.c
    config.c
    at_command.c
    hal.c
    ntp.c
    ota.c
    example_qwen.c
    vb6824.c
    sound.c
    modem.c
)

target_include_directories(
    ${CURRENT_LIB_NAME} PRIVATE
    ${c_FREERTOS_DIR}/include
    ${c_FREERTOS_DIR}/portable/GCC/ARM_CA7
    ${c_COMPONENT_DIR}/os/freertos/freertos_posix/include
    ${c_COMPONENT_DIR}/os/freertos/freertos_posix/FreeRTOS-Plus-POSIX/include
    ${c_COMPONENT_DIR}/os/freertos/freertos_posix/FreeRTOS-Plus-POSIX/include/portable/realtek
    ${BASEDIR}/component/network/cJSON
    ${BASEDIR}/component/network/sntp
    ${BASEDIR}/component/network/httpc
    ${BASEDIR}/component/network/websocket
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/c_mmi_cmd
    ${CMAKE_CURRENT_LIST_DIR}/include/c_utils
)

target_link_libraries(
    ${CURRENT_LIB_NAME} PRIVATE
    -Wl,--no-warn-mismatch
    -Wl,--allow-multiple-definition
    -L${CMAKE_CURRENT_LIST_DIR}/libs/${c_SOC_TYPE}
    qwen_sdk
    c_mmi_cmd
    qwen_test
    # hal_dummy
    c_mmi_cmd_voice_translate
    -Wl,--whole-archive
    c_license
    -Wl,--no-whole-archive
    c_mmi_cmd_application
    c_mmi_cmd_volume
    tinycrypt
)

target_compile_definitions(
    ${CURRENT_LIB_NAME} PUBLIC
    -DCONFIG_PLATFORM_ESP32=0
    -DCONFIG_PLATFORM_AIW626X=0
    -DCONFIG_PLATFORM_XR872=0
    -DCONFIG_PLATFORM_AC7911=0
    -DCONFIG_PLATFORM_JL7014=0
    -DCONFIG_PLATFORM_LM620=0
    -DCONFIG_PLATFORM_RTL8711=1
)
