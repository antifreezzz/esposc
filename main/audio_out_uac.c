#include "audio_out.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/uac_host.h"

#define SAMPLE_RATE 48000
#define FRAME_BYTES 4                  // 2 канала по 16 бит
#define BUF_BYTES (SAMPLE_RATE / 10 * FRAME_BYTES)   // 100 мс в драйвере
#define LOW_WATER_BYTES (SAMPLE_RATE / 100 * FRAME_BYTES) // 10 мс
#define VOLUME_DB (-12)                // громкость свистка, с запасом для ушей
#define CONNECTED_BIT BIT0

static const char *TAG = "audio_out";

typedef struct {
    int is_driver;
    union {
        struct {
            uint8_t addr;
            uint8_t iface;
            uac_host_driver_event_t event;
        } drv;
        struct {
            uac_host_device_handle_t handle;
            uac_host_device_event_t event;
        } dev;
    };
} uac_evt_t;

static QueueHandle_t s_queue;
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_lock;
static uac_host_device_handle_t s_spk;
static volatile uint32_t s_underruns;
static volatile int s_streaming;

static void device_cb(uac_host_device_handle_t handle, const uac_host_device_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DEVICE_EVENT_TX_DONE) {
        // Срабатывает, когда в буфере драйвера <= LOW_WATER_BYTES.
        if (s_streaming) {
            s_underruns++;
        }
        return;
    }
    uac_evt_t e = {.is_driver = 0, .dev = {handle, event}};
    xQueueSend(s_queue, &e, 0);
}

static void driver_cb(uint8_t addr, uint8_t iface, const uac_host_driver_event_t event, void *arg)
{
    (void)arg;
    uac_evt_t e = {.is_driver = 1, .drv = {addr, iface, event}};
    xQueueSend(s_queue, &e, 0);
}

static void open_speaker(uint8_t addr, uint8_t iface)
{
    const uac_host_device_config_t cfg = {
        .addr = addr,
        .iface_num = iface,
        .buffer_size = BUF_BYTES,
        .buffer_threshold = LOW_WATER_BYTES,
        .callback = device_cb,
        .callback_arg = NULL,
    };
    uac_host_device_handle_t h = NULL;
    esp_err_t err = uac_host_device_open(&cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        return;
    }
    const uac_host_stream_config_t stream = {
        .channels = 2,
        .bit_resolution = 16,
        .sample_freq = SAMPLE_RATE,
        .flags = 0,
    };
    err = uac_host_device_start(h, &stream);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start 48000/2/16 failed: %s", esp_err_to_name(err));
        uac_host_device_close(h);
        return;
    }
    uac_host_device_set_mute(h, false);
    err = uac_host_device_set_volume_db(h, VOLUME_DB * 256);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set volume: %s", esp_err_to_name(err));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_spk = h;
    s_streaming = 0;
    xSemaphoreGive(s_lock);
    xEventGroupSetBits(s_events, CONNECTED_BIT);
    ESP_LOGI(TAG, "speaker addr %d iface %d: streaming 48000 Hz 2 ch 16 bit, volume %d dB",
             addr, iface, VOLUME_DB);
}

static void close_speaker(uac_host_device_handle_t h)
{
    xEventGroupClearBits(s_events, CONNECTED_BIT);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_spk == h) {
        s_spk = NULL;
        s_streaming = 0;
    }
    xSemaphoreGive(s_lock);
    uac_host_device_close(h);
    ESP_LOGW(TAG, "speaker disconnected, waiting for it to come back");
}

static void event_task(void *arg)
{
    (void)arg;
    uac_evt_t e;
    for (;;) {
        if (!xQueueReceive(s_queue, &e, portMAX_DELAY)) {
            continue;
        }
        if (e.is_driver) {
            if (e.drv.event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED && s_spk == NULL) {
                open_speaker(e.drv.addr, e.drv.iface);
            }
            // RX (микрофон свистка) игнорируем.
        } else if (e.dev.event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
            if (e.dev.handle == s_spk) {
                close_speaker(e.dev.handle);
            } else {
                uac_host_device_close(e.dev.handle);
            }
        } else if (e.dev.event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
            ESP_LOGW(TAG, "transfer error");
        }
    }
}

esp_err_t audio_out_start(void)
{
    s_queue = xQueueCreate(10, sizeof(uac_evt_t));
    s_events = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();
    if (!s_queue || !s_events || !s_lock) {
        return ESP_ERR_NO_MEM;
    }
    const uac_host_driver_config_t cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = driver_cb,
        .callback_arg = NULL,
    };
    esp_err_t err = uac_host_install(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    xTaskCreatePinnedToCore(event_task, "uac_evt", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "uac host installed, waiting for speaker");
    return ESP_OK;
}

size_t audio_out_write(const int16_t *stereo, size_t frames, uint32_t timeout_ms)
{
    size_t done = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_spk) {
        esp_err_t err = uac_host_device_write(s_spk, (uint8_t *)stereo, frames * FRAME_BYTES,
                                              pdMS_TO_TICKS(timeout_ms));
        if (err == ESP_OK) {
            done = frames;
            s_streaming = 1;
        }
    }
    xSemaphoreGive(s_lock);
    return done;
}

int audio_out_connected(void)
{
    return (xEventGroupGetBits(s_events) & CONNECTED_BIT) != 0;
}

int audio_out_wait_connected(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (b & CONNECTED_BIT) != 0;
}

uint32_t audio_out_underruns(void)
{
    return s_underruns;
}
