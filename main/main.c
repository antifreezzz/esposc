#include <math.h>

#include "audio_out.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_host_task.h"

#define BLOCK 480

static const char *TAG = "main";

// Тестовый тон 440 Гц, -12 dBFS.
static void tone_task(void *arg)
{
    (void)arg;
    static int16_t buf[BLOCK * 2];
    float phase = 0.0f;
    uint32_t block_no = 0;
    const float amp = 0.25f * 32767.0f;
    for (;;) {
        if (!audio_out_wait_connected(1000)) {
            continue;
        }
        // Диагностика: 1 с тон / 1 с тишина, высота чередуется 440 и 880 Гц.
        uint32_t sec = block_no++ / 100;
        int on = (sec % 2) == 0;
        float inc = 2.0f * (float)M_PI * ((sec / 2) % 2 ? 880.0f : 440.0f) / 48000.0f;
        for (int i = 0; i < BLOCK; i++) {
            int16_t v = on ? (int16_t)(amp * sinf(phase)) : 0;
            buf[2 * i] = buf[2 * i + 1] = v;
            phase += inc;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        audio_out_write(buf, BLOCK, 50);
    }
}

void app_main(void)
{
    usb_host_start();
    ESP_ERROR_CHECK(audio_out_start());
    xTaskCreatePinnedToCore(tone_task, "tone", 4096, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "440 Hz test tone");
}
