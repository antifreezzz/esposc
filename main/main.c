#include <math.h>

#include "acid.h"
#include "audio_out.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midi_in.h"
#include "store.h"
#include "usb_host_task.h"

#define SR 48000
#define BLOCK 96 // 2 мс: короткий блок - меньше задержка от ноты до звука
#define BLOCK_US (BLOCK * 1000000 / SR)
#define CHUNK 32  // MIDI применяется между кусками; рендер идёт рывками, так что реальный джиттер ~5 мс
#define STATS_PERIOD_US (10 * 1000 * 1000)
#define QUIET_BLOCKS 500   // 1 с тишины на выходе: можно писать во flash
#define QUIET_PEAK 1e-4f

static const char *TAG = "main";

static acid_synth_t s_synth;

// Синт 2 считается на ядре 0 на кусок вперёд: пока ядро 1 считает кусок c, ядро 0 считает
// кусок c + 1 синта 2. События MIDI до синта 2 доходят на кусок позже (0.67 мс).
static float s_slot1[2][CHUNK];
static int s_slot1_next; // какой буфер считает ядро 0
static TaskHandle_t s_synth_task;
static TaskHandle_t s_slot1_task;
static int64_t s_slot1_worst_us;

static void slot1_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        acid_synth_render_slot(&s_synth, 1, s_slot1[s_slot1_next], CHUNK);
        int64_t dt = esp_timer_get_time() - t0;
        s_slot1_worst_us = dt > s_slot1_worst_us ? dt : s_slot1_worst_us;
        xTaskNotifyGive(s_synth_task);
    }
}

// Блок кусками по CHUNK, события MIDI перед каждым куском. Пока ядро 0 считает синт 2,
// состояние синта трогает только рендер ядра 1 (без синта 2); к концу блока ядро 0 свободно.
static void render_block(float *l, float *r)
{
    for (int off = 0; off < BLOCK; off += CHUNK) {
        acid_midi_event_t ev;
        while (midi_in_receive(&ev)) {
            acid_synth_midi(&s_synth, &ev);
        }
        int ready = s_slot1_next;
        s_slot1_next ^= 1;
        xTaskNotifyGive(s_slot1_task);
        acid_synth_render_with_lr(&s_synth, l + off, r + off, CHUNK, s_slot1[ready]);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static float block_peak(const float *l, const float *r)
{
    float p = 0.0f;
    for (int i = 0; i < BLOCK; i++) {
        p = fabsf(l[i]) > p ? fabsf(l[i]) : p;
        p = fabsf(r[i]) > p ? fabsf(r[i]) : p;
    }
    return p;
}

static void synth_task(void *arg)
{
    (void)arg;
    static float out_l[BLOCK], out_r[BLOCK];
    static int16_t pcm[BLOCK * 2];
    static uint8_t blob[ACID_SYNTH_BLOB];
    uint32_t last_underruns = 0;
    int64_t worst_us = 0;
    int64_t next_stats = esp_timer_get_time() + STATS_PERIOD_US;
    int live = 0;
    int save_pending = 0;
    int quiet = 0;

    acid_synth_init(&s_synth, (float)SR);
    ESP_LOGI(TAG, "free: internal %u, psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (store_load(blob, sizeof blob) == ACID_SYNTH_BLOB && acid_synth_import(&s_synth, blob, sizeof blob)) {
        ESP_LOGI(TAG, "knobs restored");
    }
    xTaskCreatePinnedToCore(slot1_task, "slot1", 4096, NULL, 6, &s_slot1_task, 0);
    for (;;) {
        if (!audio_out_connected()) {
            // Ждём свисток молча, без перебора в цикле.
            audio_out_wait_connected(portMAX_DELAY);
            ESP_LOGI(TAG, "speaker ready");
            acid_midi_event_t stale;
            while (midi_in_receive(&stale)) {
                // Пока свистка не было, накопились старые события: выбрасываем.
            }
            if (live) {
                acid_synth_set_live(&s_synth, 1); // вместе с событиями ушли и note off: чистим стек нот
            }
        }
        if (midi_in_connected() != live) {
            live = midi_in_connected();
            acid_synth_set_live(&s_synth, live);
            ESP_LOGI(TAG, "%s", live ? "midi controller: live mode" : "no midi controller: playing acid track");
        }
        int64_t t0 = esp_timer_get_time();
        render_block(out_l, out_r);
        acid_pcm_lr_to_s16(out_l, out_r, pcm, BLOCK);
        int64_t dt = esp_timer_get_time() - t0;
        worst_us = dt > worst_us ? dt : worst_us;

        // Сменили тип синтеза: память ручек пишем, когда выход помолчит (flash стопорит кэш).
        save_pending |= acid_synth_take_save(&s_synth);
        quiet = block_peak(out_l, out_r) < QUIET_PEAK ? quiet + 1 : 0;
        if (save_pending && quiet >= QUIET_BLOCKS) {
            store_save(blob, acid_synth_export(&s_synth, blob, sizeof blob));
            save_pending = 0;
        }

        if (audio_out_write(pcm, BLOCK, 50) == 0) {
            // Запись не прошла (устройство пропало или в ошибке): не крутиться вхолостую.
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (t0 >= next_stats) {
            uint32_t u = audio_out_underruns();
            if (u != last_underruns || worst_us > BLOCK_US * 7 / 10) {
                ESP_LOGW(TAG, "underruns %lu (+%lu), xfer errors %lu, worst render %lld us of %d, synth 2 %lld us of %d",
                         (unsigned long)u, (unsigned long)(u - last_underruns),
                         (unsigned long)audio_out_transfer_errors(), worst_us, BLOCK_US,
                         s_slot1_worst_us, CHUNK * 1000000 / SR);
            } else {
                ESP_LOGI(TAG, "ok, worst render %lld us of %d, synth 2 %lld us of %d", worst_us, BLOCK_US,
                         s_slot1_worst_us, CHUNK * 1000000 / SR);
            }
            last_underruns = u;
            worst_us = 0;
            s_slot1_worst_us = 0;
            next_stats = t0 + STATS_PERIOD_US;
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(store_start());
    usb_host_start();
    ESP_ERROR_CHECK(midi_in_start());
    ESP_ERROR_CHECK(audio_out_start());
    xTaskCreatePinnedToCore(synth_task, "synth", 4096, NULL, 6, &s_synth_task, 1);
}
