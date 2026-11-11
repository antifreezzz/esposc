// Стенд отклика интерфейса: idf.py -C panel -B panel/build-uibench -DUI_BENCH=1 -p /dev/ttyUSB0 -b 460800 flash
// 1. Раз в 1.5 с касается очередной кнопки дока и пишет в лог перерисовки после касания: строка
//    «+120:630» - перерисовка началась через 12.0 мс после касания и шла 63.0 мс.
// 2. Профиль задачи LVGL выборкой: таймер на ядре 1 раз в ~0.5 мс записывает, где прервали задачу LVGL. Когда
//    набралось SAMPLES, адреса уходят в лог строками «PC ...»; функции по ним - xtensa-esp32s3-elf-addr2line -f -e
//    panel/build-uibench/esposc_panel.elf. Адреса 0x4005xxxx - копирование памяти из ПЗУ.
#ifdef UI_BENCH
#include "ui_bench.h"

#include <string.h>

#include "display.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pl_ia.h"
#include "xtensa_context.h"

#define SAMPLES 6000

static uint32_t *s_pc;
static volatile int s_n;
static TaskHandle_t s_lvgl;

static bool IRAM_ATTR on_alarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *ctx)
{
    (void)t, (void)e, (void)ctx;
    TaskHandle_t cur = xTaskGetCurrentTaskHandleForCore(1);
    if (cur == s_lvgl && s_n < SAMPLES) {
        // Порт FreeRTOS при входе в прерывание кладёт указатель на сохранённый кадр в начало TCB.
        const XtExcFrame *f = *(XtExcFrame *const *)cur;
        s_pc[s_n++] = (uint32_t)f->pc;
    }
    return false;
}

// Звать с ядра 1: прерывание таймера садится на ядро вызвавшей задачи.
void ui_bench_start(void)
{
    s_pc = heap_caps_malloc(SAMPLES * sizeof *s_pc, MALLOC_CAP_SPIRAM);
    s_lvgl = xTaskGetHandle("taskLVGL");
    gptimer_config_t cfg = {.clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000};
    gptimer_handle_t t;
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &t));
    gptimer_event_callbacks_t cbs = {.on_alarm = on_alarm};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(t, &cbs, NULL));
    gptimer_alarm_config_t alarm = {.alarm_count = 487, .flags.auto_reload_on_alarm = 1};
    ESP_ERROR_CHECK(gptimer_set_alarm_action(t, &alarm));
    ESP_ERROR_CHECK(gptimer_enable(t));
    ESP_ERROR_CHECK(gptimer_start(t));
}

// Раз, когда буфер полон: выгрузить адреса в лог.
static void dump(void)
{
    static int done;
    if (done || s_n < SAMPLES) {
        return;
    }
    done = 1;
    for (int i = 0; i < SAMPLES; i += 12) {
        char line[12 * 9 + 1];
        int n = 0;
        for (int j = i; j < i + 12 && j < SAMPLES; j++) {
            n += snprintf(line + n, sizeof line - n, "%08x ", (unsigned)s_pc[j]);
        }
        printf("PC %s\n", line);
        if (i % 600 == 0) {
            vTaskDelay(1);
        }
    }
    printf("PC done\n");
}

void ui_bench_touch(uint32_t ms, int *down, int *x, int *y)
{
    static uint32_t tap_ms;
    static int64_t tap_us;
    static int grp;
    if (ms - tap_ms >= 1500) {
        char line[DISPLAY_LOG * 24] = "";
        int n = 0, f, mean, worst;
        for (int i = 0; i < DISPLAY_LOG; i++) {
            int64_t start;
            int dt = display_frame_log(i, &start);
            if (dt >= 0) {
                n += snprintf(line + n, sizeof line - n, " +%d:%d", (int)(start - tap_us) / 100, dt / 100);
            }
        }
        display_stats(&f, &mean, &worst);
        ESP_LOGI("bench", "group %d, redraws (after touch : took, 0.1 ms):%s", grp, line);
        grp = (grp + 1) % PL_GROUPS;
        tap_ms = ms;
        tap_us = esp_timer_get_time();
    }
    dump();
    *down = ms - tap_ms < 60;
    *x = grp * PL_IA_DOCK_W + PL_IA_DOCK_W / 2;
    *y = 480 - PL_IA_DOCK_H / 2;
}
#endif
