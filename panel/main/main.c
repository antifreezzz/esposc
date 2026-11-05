#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link.h"
#include "petri_bench.h"
#include "ui.h"

#define STEP_MS 10 // опрос тача и UART

static pl_model_t s_model;

void app_main(void)
{
    ESP_ERROR_CHECK(display_start());
    ESP_ERROR_CHECK(link_start());
    pl_model_init(&s_model);
    ui_start();
#ifdef PETRI_BENCH
    petri_bench_start();
#endif
    ESP_LOGI("panel", "ready");
    TickType_t wake = xTaskGetTickCount();
    int64_t step_us = 0, step_max = 0;
    int steps = 0;
    uint32_t stats_ms = 0;
    for (;;) {
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
        int x = 0, y = 0;
        int down = display_touch(&x, &y);
        link_poll(&s_model, ms);
        int64_t t0 = esp_timer_get_time();
        ui_step(&s_model, down, x, y, ms);
        int64_t dt = esp_timer_get_time() - t0;
        step_us += dt;
        step_max = dt > step_max ? dt : step_max;
        steps++;
        if (ms - stats_ms >= 10000) {
            int f, mean, worst;
            display_stats(&f, &mean, &worst);
            ESP_LOGI("panel", "10 s: frames %d, redraw mean %d worst %d us; ui step mean %lld worst %lld us; touch %d",
                     f, mean, worst, step_us / steps, step_max, down);
            stats_ms = ms;
            step_us = step_max = 0;
            steps = 0;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(STEP_MS));
    }
}
