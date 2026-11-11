// Стенд цены голосов (сборка -DESPOSC_MODEL_BENCH=1, каталог build-modelbench): без звука, по кругу считает
// каждую модель Plaits, голос 303 через слот синта и перевод в PCM. В логе - мкс на блок 2 мс (96 сэмплов).
// Замер без соседей по кэшу: годится сравнивать прошивки между собой, итог проверять стендом build-stress.
#include <stdio.h>

#include "acid.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SR 48000
#define BLOCK 96
#define SECONDS 1
#define CPU_MHZ 240

static const char *TAG = "bench";
static acid_synth_t s_synth;

static uint32_t us_per_block(uint64_t cycles, int samples)
{
    return (uint32_t)(cycles * BLOCK / samples / CPU_MHZ);
}

// Модель напрямую: нота A1, гейт 125 мс через 125 мс, ручки посередине.
static uint32_t bench_model(acid_plaits_t *p, int model)
{
    static float out[ACID_PLAITS_BLOCK];
    acid_plaits_params_t pp = {
        .model = model, .note = 45.0f, .harmonics = 0.5f, .timbre = 0.5f, .morph = 0.5f,
        .decay = 0.5f, .lpg_colour = 0.5f,
    };
    acid_plaits_init(p);
    uint64_t cycles = 0;
    int n = SR * SECONDS;
    for (int i = 0; i + ACID_PLAITS_BLOCK <= n; i += ACID_PLAITS_BLOCK) {
        pp.trigger = (i % 12000) < 6000;
        pp.level = (float)pp.trigger;
        uint32_t t = esp_cpu_get_cycle_count();
        acid_plaits_render(p, &pp, out, ACID_PLAITS_BLOCK);
        cycles += esp_cpu_get_cycle_count() - t;
    }
    return us_per_block(cycles, n);
}

// Слот синта 1 целиком (тип type, 0 - 303): нота держится, новая каждые 125 мс.
static uint32_t bench_slot(int type, int drive)
{
    static float out[32];
    acid_synth_init(&s_synth, (float)SR);
    acid_synth_notes_off(&s_synth);
    acid_synth_set_knob(&s_synth, 6, 1, type); // индекс ручки с 0: энкодер 2
    acid_synth_set_knob(&s_synth, 6, 13, drive); // энкодер 14
    for (int i = 0; i < 30; i++) {
        acid_synth_render_slot(&s_synth, 0, out, 32); // смена типа: уход и приход
    }
    uint64_t cycles = 0;
    int n = SR * SECONDS;
    for (int i = 0; i + 32 <= n; i += 32) {
        if (i % 6000 < 32) {
            acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = 4, .d1 = (uint8_t)(33 + (i / 6000) % 12), .d2 = 100};
            acid_synth_midi(&s_synth, &e);
        }
        uint32_t t = esp_cpu_get_cycle_count();
        acid_synth_render_slot(&s_synth, 0, out, 32);
        cycles += esp_cpu_get_cycle_count() - t;
    }
    return us_per_block(cycles, n);
}

static uint32_t bench_pcm(void)
{
    static float l[BLOCK], r[BLOCK];
    static int16_t pcm[BLOCK * 2];
    for (int i = 0; i < BLOCK; i++) {
        l[i] = (float)(i - 48) / 50.0f; // и срез по краям тоже
        r[i] = -l[i] * 0.37f;
    }
    uint64_t cycles = 0;
    for (int i = 0; i < 2000; i++) {
        uint32_t t = esp_cpu_get_cycle_count();
        acid_pcm_lr_to_s16(l, r, pcm, BLOCK);
        cycles += esp_cpu_get_cycle_count() - t;
    }
    return (uint32_t)(cycles / 2000 / CPU_MHZ);
}

static void bench_task(void *arg)
{
    (void)arg;
    acid_plaits_t *p = acid_plaits_create();
    for (int pass = 0;; pass++) {
        char line[200];
        int n = 0;
        for (int m = 0; m < ACID_PLAITS_MODELS; m++) {
            n += snprintf(line + n, sizeof line - n, " %lu", (unsigned long)bench_model(p, m));
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "pass %d plaits us/block, models 0-23:%s", pass, line);
        uint32_t v303 = bench_slot(0, 0);
        vTaskDelay(1);
        uint32_t v303d = bench_slot(0, 100);
        vTaskDelay(1);
        uint32_t fm = bench_slot(11, 0);
        vTaskDelay(1);
        ESP_LOGI(TAG, "pass %d slot us/block: 303 %lu, 303+drive %lu, fm2 slot %lu; pcm %lu", pass, (unsigned long)v303,
                 (unsigned long)v303d, (unsigned long)fm, (unsigned long)bench_pcm());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void model_bench_start(void)
{
    xTaskCreatePinnedToCore(bench_task, "bench", 8192, NULL, 6, NULL, 1);
}
