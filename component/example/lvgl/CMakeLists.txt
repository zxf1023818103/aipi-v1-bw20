ameba_internal_library(example_lvgl)

target_sources(
    ${CURRENT_LIB_NAME} PRIVATE
    example_lvgl.c
    eye_gif.c
)

target_include_directories(
    ${CURRENT_LIB_NAME} PRIVATE
    ${BASEDIR}/component/network/cJSON
)
