#include "audio_out.h"

#include <math.h>

#include "acid_pcm.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// ЦАП PCM5102A на I2S: SCK модуля на земле, такт он берёт из BCK (MCLK не нужен).
#define PIN_BCK 4
#define PIN_WS 5
#define PIN_DOUT 6
#define SAMPLE_RATE 48000
#define DMA_FRAMES 96 // один блок синта (2 мс) на дескриптор
#define DMA_DESCS 4   // 8 мс в DMA: задержка ноты от BSP меньше, чем 12 мс у свистка
// У PCM5102A громкость не крутится, полная шкала - 2.1 В rms (громче свистка):
// ослабляем цифрой. Подобрать на слух, как у свистка.
#define VOLUME_DB (-18)
#define MAX_FRAMES 256

static const char *TAG = "audio_out";

static i2s_chan_handle_t s_tx;
static int32_t s_gain_q15;
static int16_t s_buf[MAX_FRAMES * 2];
static volatile int s_streaming;
static volatile uint32_t s_underruns;

// DMA дошёл до дескриптора без новых данных и играет тишину (auto_clear): недогруз.
static IRAM_ATTR bool on_send_q_ovf(i2s_chan_handle_t handle, i2s_event_data_t *event, void *ctx)
{
    (void)handle;
    (void)event;
    (void)ctx;
    if (s_streaming) {
        s_underruns++;
    }
    return false;
}

esp_err_t audio_out_start(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = DMA_DESCS;
    chan.dma_frame_num = DMA_FRAMES;
    chan.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&chan, &s_tx, NULL);
    if (err != ESP_OK) {
        return err;
    }
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_BCK,
            .ws = PIN_WS,
            .dout = PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &std);
    if (err != ESP_OK) {
        return err;
    }
    const i2s_event_callbacks_t cbs = {.on_send_q_ovf = on_send_q_ovf};
    err = i2s_channel_register_event_callback(s_tx, &cbs, NULL);
    if (err != ESP_OK) {
        return err;
    }
    s_gain_q15 = (int32_t)lrintf(32768.0f * powf(10.0f, VOLUME_DB / 20.0f));
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "i2s pcm5102a: bck %d ws %d dout %d, %d Hz 2 ch 16 bit, volume %d dB",
             PIN_BCK, PIN_WS, PIN_DOUT, SAMPLE_RATE, VOLUME_DB);
    return ESP_OK;
}

size_t audio_out_write(const int16_t *stereo, size_t frames, uint32_t timeout_ms)
{
    frames = frames > MAX_FRAMES ? MAX_FRAMES : frames;
    acid_pcm_gain_s16(stereo, s_buf, frames * 2, s_gain_q15);
    size_t bytes = 0;
    i2s_channel_write(s_tx, s_buf, frames * 4, &bytes, timeout_ms);
    s_streaming = 1;
    return bytes / 4;
}

// ЦАП припаян: всегда на месте.
int audio_out_connected(void)
{
    return 1;
}

int audio_out_wait_connected(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return 1;
}

uint32_t audio_out_underruns(void)
{
    return s_underruns;
}

uint32_t audio_out_transfer_errors(void)
{
    return 0;
}
