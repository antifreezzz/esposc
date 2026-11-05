#include "petri_bench.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "petri.h"
#include "esp_cpu.h"

#define TAG "petri"
#define FRAME_MS 50 // 20 кадров/с
#define STEPS 4     // шагов на кадр по умолчанию
#ifndef PETRI_BENCH_CORE
#define PETRI_BENCH_CORE 1
#endif

// ~51 КБ во внутренней RAM. Не в .bss: статикой съедает память до старта экрана, и буфер отрисовки LVGL
// (~46 КБ одним куском) не выделяется. Выделяется после интерфейса, сетки по отдельности (память фрагментирована).
static petri_t *s_dish;
static int16_t s_table[PETRI_TABLE];
static int8_t s_mods[PETRI_MODS];

static void bench_task(void *arg)
{
    (void)arg;
    unsigned before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned biggest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t nn = PETRI_N * PETRI_N;
    s_dish = heap_caps_malloc(sizeof *s_dish, caps);
    int16_t *u = heap_caps_malloc(nn * 2, caps), *v = heap_caps_malloc(nn * 2, caps);
    int8_t *dk = heap_caps_malloc(nn, caps);
    ESP_LOGI(TAG, "petri bench: dish %u + %u bytes; internal free %u, biggest block %u before; %s",
             (unsigned)sizeof *s_dish, (unsigned)(nn * 5), before, biggest,
             s_dish && u && v && dk ? "allocated" : "NO MEMORY");
    if (!(s_dish && u && v && dk)) {
        vTaskDelete(NULL);
    }
    petri_bind(s_dish, u, v, dk);
    petri_init(s_dish, 1);
    uint32_t c0 = esp_cpu_get_cycle_count();
    petri_step(s_dish);
    uint32_t c1 = esp_cpu_get_cycle_count();
    ESP_LOGI(TAG, "petri bench: one step %u cycles (%u per cell)", (unsigned)(c1 - c0), (unsigned)((c1 - c0) / (PETRI_N * PETRI_N)));
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 100; i++) {
        petri_step(s_dish);
    }
    int64_t burst = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "petri bench: 100 steps in a row %lld us (%lld us/step); internal RAM free %u", burst, burst / 100,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    int64_t step_sum = 0, step_max = 0, agents_sum = 0, cap_sum = 0, cap_max = 0;
    int frames = 0;
    int64_t next_log = esp_timer_get_time() + 5000000;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        int64_t a = esp_timer_get_time();
        for (int s = 0; s < STEPS; s++) {
            petri_step(s_dish);
        }
        int64_t b = esp_timer_get_time();
        petri_frame(s_dish, 0); // жители, Δk, центр орбиты
        int64_t c = esp_timer_get_time();
        petri_capture(s_dish, s_table, s_mods);
        int64_t d = esp_timer_get_time();
        int64_t st = (b - a) / STEPS;
        step_sum += st;
        step_max = st > step_max ? st : step_max;
        agents_sum += c - b;
        cap_sum += d - c;
        cap_max = d - c > cap_max ? d - c : cap_max;
        frames++;
        if (d >= next_log) {
            ESP_LOGI(TAG,
                     "petri bench: step mean %lld us, worst %lld us; agents %lld us; capture %lld us (worst %lld); "
                     "frames %d; grazers %d; internal RAM free %u; stack free %u",
                     step_sum / frames, step_max, agents_sum / frames, cap_sum / frames, cap_max, frames,
                     petri_count(s_dish, PETRI_GRAZER), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            step_sum = step_max = agents_sum = cap_sum = cap_max = 0;
            frames = 0;
            next_log = d + 5000000;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
    }
}

void petri_bench_start(void)
{
    // Приоритет как у петли интерфейса (1): та спит между опросами, стенд - между кадрами.
    xTaskCreatePinnedToCore(bench_task, "petri", 4096, NULL, 1, NULL, PETRI_BENCH_CORE);
}
