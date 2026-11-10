#include "store.h"

#include <string.h>

#include "acid_custom.h"
#include "acid_scene.h"
#include "acid_seq.h"
#include "acid_synth.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pl_grids.h"

#define NS "acid"
#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))
#define BLOB_MAX MAX3(ACID_SYNTH_BLOB, ACID_CUSTOM_BLOB, ACID_SCENE_BLOB > ACID_SEQ_BLOB ? ACID_SCENE_BLOB : ACID_SEQ_BLOB)

_Static_assert(PL_GRIDS_BLOB <= BLOB_MAX, "grids blob fits a store slot");

static const char *TAG = "store";

typedef struct {
    const char *key;
    uint8_t buf[BLOB_MAX];
    size_t len;
    int dirty;
} slot_t;

// Ключ без слота store_save молча не пишет: каждый ключ store.h - здесь.
static slot_t s_slots[] = {{.key = STORE_KNOBS}, {.key = STORE_CUSTOM}, {.key = STORE_SCENES}, {.key = STORE_SEQ},
                           {.key = STORE_MODE}, {.key = STORE_GRIDS}};
#define NSLOTS ((int)(sizeof s_slots / sizeof s_slots[0]))

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static slot_t *slot_of(const char *key)
{
    for (int i = 0; i < NSLOTS; i++) {
        if (strcmp(s_slots[i].key, key) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static void write_one(const char *key, const uint8_t *buf, size_t len)
{
    int64_t t0 = esp_timer_get_time();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, buf, len);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    int64_t dt = esp_timer_get_time() - t0;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s saved, %lld us", key, dt);
    } else {
        ESP_LOGW(TAG, "%s save failed: %s", key, esp_err_to_name(err));
    }
}

static void store_task(void *arg)
{
    (void)arg;
    static uint8_t copy[BLOB_MAX];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (int i = 0; i < NSLOTS; i++) {
            taskENTER_CRITICAL(&s_lock);
            int dirty = s_slots[i].dirty;
            size_t len = s_slots[i].len;
            if (dirty) {
                memcpy(copy, s_slots[i].buf, len);
                s_slots[i].dirty = 0;
            }
            taskEXIT_CRITICAL(&s_lock);
            if (dirty) {
                write_one(s_slots[i].key, copy, len);
            }
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

size_t store_load(const char *key, void *buf, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    size_t len = cap;
    esp_err_t err = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    return err == ESP_OK ? len : 0;
}

void store_save(const char *key, const void *buf, size_t len)
{
    slot_t *sl = slot_of(key);
    if (!sl || len > sizeof sl->buf) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    memcpy(sl->buf, buf, len);
    sl->len = len;
    sl->dirty = 1;
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_task);
}
