#include "store.h"

#include <string.h>

#include "acid_synth.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS "acid"
#define KEY "knobs"

static const char *TAG = "store";

static TaskHandle_t s_task;
static uint8_t s_buf[ACID_SYNTH_BLOB];
static size_t s_len;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void store_task(void *arg)
{
    (void)arg;
    static uint8_t copy[ACID_SYNTH_BLOB];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        taskENTER_CRITICAL(&s_lock);
        size_t len = s_len;
        memcpy(copy, s_buf, len);
        taskEXIT_CRITICAL(&s_lock);

        int64_t t0 = esp_timer_get_time();
        nvs_handle_t h;
        esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, KEY, copy, len);
            if (err == ESP_OK) {
                err = nvs_commit(h);
            }
            nvs_close(h);
        }
        int64_t dt = esp_timer_get_time() - t0;
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "knobs saved, %lld us", dt);
        } else {
            ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t store_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    xTaskCreatePinnedToCore(store_task, "store", 3072, NULL, 1, &s_task, 0);
    return ESP_OK;
}

size_t store_load(void *buf, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    size_t len = cap;
    esp_err_t err = nvs_get_blob(h, KEY, buf, &len);
    nvs_close(h);
    return err == ESP_OK ? len : 0;
}

void store_save(const void *buf, size_t len)
{
    if (len > sizeof s_buf) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    memcpy(s_buf, buf, len);
    s_len = len;
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
}
