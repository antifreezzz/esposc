#include "petri_task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "link.h"
#include "petri_knobs.h"

#define TAG "petri"
#define FRAME_MS 50
#define IDLE_MS 200  // нет PETRI: проверяем реже
#define WARMUP 600   // шагов до первой таблицы: на экране сразу живая структура
#define DROP_R 2.0f

typedef struct {
    float x, y;
} drop_t;

static const pl_model_t *s_model;
static petri_t *volatile s_dish;
static QueueHandle_t s_drops;
static volatile uint32_t s_frames;

void petri_task_drop(float x, float y)
{
    drop_t d = {x, y};
    if (s_drops) {
        xQueueSend(s_drops, &d, 0);
    }
}

const petri_t *petri_task_dish(void)
{
    return s_dish;
}

uint32_t petri_task_frames(void)
{
    return s_frames;
}

static petri_t *alloc_dish(void)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t nn = PETRI_N * PETRI_N;
    petri_t *p = heap_caps_malloc(sizeof *p, caps);
    int16_t *u = heap_caps_malloc(nn * 2, caps), *v = heap_caps_malloc(nn * 2, caps);
    int8_t *dk = heap_caps_malloc(nn, caps);
    if (!(p && u && v && dk)) {
        ESP_LOGE(TAG, "no internal RAM for the dish");
        heap_caps_free(p);
        heap_caps_free(u);
        heap_caps_free(v);
        heap_caps_free(dk);
        return NULL;
    }
    petri_bind(p, u, v, dk);
    petri_init(p, esp_random());
    for (int s = 0; s < WARMUP; s++) {
        petri_step(p);
        if (s % 20 == 19) {
            vTaskDelay(1); // разгон ~3 с: не держим ядро интерфейса
        }
    }
    ESP_LOGI(TAG, "dish ready; internal RAM free %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return p;
}

static void task(void *arg)
{
    (void)arg;
    static petri_knobs_t knobs;
    static int16_t tab[PETRI_TABLE];
    static int8_t mods[PETRI_MODS];
    static uint8_t out[4][PL_PETRI_PART_MAX];
    static size_t len[4];
    petri_knobs_init(&knobs);
    uint8_t seq = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        int on = s_model->have && pl_petri_synth(s_model->raw) >= 0;
        if (!on) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            wake = xTaskGetTickCount();
            continue;
        }
        if (!s_dish) {
            s_dish = alloc_dish();
            if (!s_dish) {
                vTaskDelete(NULL);
            }
            wake = xTaskGetTickCount();
        }
        petri_t *p = s_dish;
        petri_knobs_apply(p, &knobs, s_model->raw);
        drop_t d;
        while (xQueueReceive(s_drops, &d, 0) == pdTRUE) {
            petri_drop(p, d.x, d.y, DROP_R);
        }
        petri_frame(p, knobs.steps);
        petri_capture(p, tab, mods);
        pl_petri_split(tab, mods, p->jumps, seq++, out, len);
        link_petri(out, len);
        s_frames++;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS)); // не успели за 50 мс - кадр реже, звук не ломается
    }
}

void petri_task_start(const pl_model_t *m)
{
    s_model = m;
    s_drops = xQueueCreate(16, sizeof(drop_t));
    // Ядро 1 с LVGL, приоритет ниже его (4): интерфейс вытесняет чашку.
    xTaskCreatePinnedToCore(task, "petri", 4096, NULL, 1, NULL, 1);
}
