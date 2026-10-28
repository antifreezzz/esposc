#include "acid.h"
#include "audio_out.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midi_in.h"
#include "usb_host_task.h"

#define SR 48000
#define BLOCK 480 // 10 мс
#define CHUNK 32  // MIDI применяется между кусками: джиттер ~0.7 мс
#define STATS_PERIOD_US (10 * 1000 * 1000)

static const char *TAG = "main";

static acid_synth_t s_synth;

// Блок кусками по CHUNK, события MIDI перед каждым куском.
static void render_block(float *out)
{
    for (int off = 0; off < BLOCK; off += CHUNK) {
        acid_midi_event_t ev;
        while (midi_in_receive(&ev)) {
            acid_synth_midi(&s_synth, &ev);
        }
        acid_synth_render(&s_synth, out + off, CHUNK);
    }
}

static void synth_task(void *arg)
{
    (void)arg;
    static float mono[BLOCK];
    static int16_t pcm[BLOCK * 2];
    uint32_t last_underruns = 0;
    int64_t worst_us = 0;
    int64_t next_stats = esp_timer_get_time() + STATS_PERIOD_US;
    int live = 0;

    acid_synth_init(&s_synth, (float)SR);
    for (;;) {
        if (!audio_out_connected()) {
            // Ждём свисток молча, без перебора в цикле.
            audio_out_wait_connected(portMAX_DELAY);
            ESP_LOGI(TAG, "speaker ready");
            acid_midi_event_t stale;
            while (midi_in_receive(&stale)) {
                // Пока свистка не было, накопились старые события: выбрасываем.
            }
        }
        if (midi_in_connected() != live) {
            live = midi_in_connected();
            acid_synth_set_live(&s_synth, live);
            ESP_LOGI(TAG, "%s", live ? "midi controller: live mode" : "no midi controller: playing acid track");
        }
        int64_t t0 = esp_timer_get_time();
        render_block(mono);
        acid_pcm_to_s16_stereo(mono, pcm, BLOCK);
        int64_t dt = esp_timer_get_time() - t0;
        worst_us = dt > worst_us ? dt : worst_us;

        if (audio_out_write(pcm, BLOCK, 50) == 0) {
            // Запись не прошла (устройство пропало или в ошибке): не крутиться вхолостую.
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (t0 >= next_stats) {
            uint32_t u = audio_out_underruns();
            if (u != last_underruns || worst_us > 7000) {
                ESP_LOGW(TAG, "underruns %lu (+%lu), xfer errors %lu, worst render %lld us of 10000",
                         (unsigned long)u, (unsigned long)(u - last_underruns),
                         (unsigned long)audio_out_transfer_errors(), worst_us);
            } else {
                ESP_LOGI(TAG, "ok, worst render %lld us of 10000", worst_us);
            }
            last_underruns = u;
            worst_us = 0;
            next_stats = t0 + STATS_PERIOD_US;
        }
    }
}

void app_main(void)
{
    usb_host_start();
    ESP_ERROR_CHECK(midi_in_start());
    ESP_ERROR_CHECK(audio_out_start());
    xTaskCreatePinnedToCore(synth_task, "synth", 4096, NULL, 6, NULL, 1);
}
