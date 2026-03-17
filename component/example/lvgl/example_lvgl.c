#include <ameba_soc.h>
#include <os_wrapper.h>

#include <lvgl.h>
#include <lv_ameba_hal.h>
#include <lv_demo_benchmark.h>

void touch_test_task(void *args)
{
    (void) args;

    lv_init();
    lv_ameba_hal_init();

    LV_IMG_DECLARE(eye_gif);
    lv_obj_t *img = lv_gif_create(lv_screen_active());
    lv_gif_set_src(img, &eye_gif);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    // extern void lv_example_gif_1(void);
    // lv_example_gif_1();

    /* To hide the memory and performance indicators in the corners
     * disable `LV_USE_MEM_MONITOR` and `LV_USE_PERF_MONITOR` in `lv_conf.h`*/

    while(1) {
        /* Periodically call the lv_task handler.
         * It could be done in a timer interrupt or an OS task too.*/
        uint32_t time_till_next = lv_task_handler();

        /* handle LV_NO_TIMER_READY. Another option is to `sleep` for longer */
        if(time_till_next == LV_NO_TIMER_READY)
            time_till_next = LV_DEF_REFR_PERIOD;

        /* delay to avoid unnecessary polling */
        rtos_time_delay_ms(time_till_next);
    }

    lv_deinit();

    rtos_task_delete(NULL);
}

void app_example(void)
{
    rtos_task_create(NULL, "touch_test_task", touch_test_task, NULL, 10240, 1);
}
