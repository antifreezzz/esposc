#include "audio_out.h"

#include <math.h>

#include "acid_pcm.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ESPOSC_I2S_TONE
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#endif

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

#ifdef ESPOSC_I2S_TONE
// Самопроверка проводки: читаем свои же выходы. Пин, замкнутый на землю или питание, не переключается.
static void pin_probe_task(void *arg)
{
    (void)arg;
    static const int pins[3] = {PIN_BCK, PIN_WS, PIN_DOUT};
    for (int k = 0; k < 3; k++) {
        gpio_input_enable(pins[k]);
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        int high[3] = {0}, edges[3] = {0}, last[3] = {0};
        for (int n = 0; n < 20000; n++) {
            for (int k = 0; k < 3; k++) {
                int v = gpio_get_level(pins[k]);
                high[k] += v;
                edges[k] += n && v != last[k];
                last[k] = v;
            }
        }
        ESP_LOGI(TAG, "probe of 20000: bck high %d edges %d, ws high %d edges %d, dout high %d edges %d",
                 high[0], edges[0], high[1], edges[1], high[2], edges[2]);
    }
}
#endif

#ifdef ESPOSC_I2S_TONE
// До I2S: по очереди одна нога в 1, две в 0. Замкнутая пара читается одинаково.
static void pin_short_test(void)
{
    static const int pins[3] = {PIN_BCK, PIN_WS, PIN_DOUT};
    for (int k = 0; k < 3; k++) {
        gpio_reset_pin(pins[k]);
        gpio_set_direction(pins[k], GPIO_MODE_INPUT_OUTPUT);
    }
    for (int hi = 0; hi < 3; hi++) {
        for (int k = 0; k < 3; k++) {
            gpio_set_level(pins[k], k == hi);
        }
        esp_rom_delay_us(50);
        ESP_LOGI(TAG, "short test: drive %d high -> read gpio%d=%d gpio%d=%d gpio%d=%d", pins[hi],
                 pins[0], gpio_get_level(pins[0]), pins[1], gpio_get_level(pins[1]), pins[2], gpio_get_level(pins[2]));
    }
    for (int k = 0; k < 3; k++) {
        gpio_reset_pin(pins[k]);
    }
}
#endif

esp_err_t audio_out_start(void)
{
#ifdef ESPOSC_I2S_TONE
    pin_short_test();
#endif
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = DMA_DESCS;
    chan.dma_frame_num = DMA_FRAMES;
    chan.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&chan, &s_tx, NULL);
    if (err != ESP_OK) {
        return err;
    }
    i2s_std_config_t std = {
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
    // Слот 32 бита при данных 16: BCK = 64 fs. PLL PCM5102A без SCK на 32 fs цепляется плохо (треск).
    // WS - полкадра: макрос ставит ширину по данным (16), а слот теперь 32. Иначе левый канал короткий и молчит.
    std.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std.slot_cfg.ws_width = 32;
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
#ifdef ESPOSC_I2S_TONE
    xTaskCreatePinnedToCore(pin_probe_task, "pin_probe", 3072, NULL, 1, NULL, 0);
#endif
    return ESP_OK;
}

size_t audio_out_write(const int16_t *stereo, size_t frames, uint32_t timeout_ms)
{
    frames = frames > MAX_FRAMES ? MAX_FRAMES : frames;
#ifdef ESPOSC_I2S_TONE
    // Проверка ЦАПа без синта: синус 375 Гц (128 сэмплов на период), -12 дБ до громкости, по кругу L, R, оба, тишина.
    static uint32_t ph;
    for (size_t i = 0; i < frames; i++, ph++) {
        int16_t v = (int16_t)lrintf(8192.0f * sinf((float)(ph & 127) * (6.2831853f / 128.0f)));
        uint32_t sec = (ph / SAMPLE_RATE) & 3; // по секунде: левый, правый, оба, тишина
        s_buf[2 * i] = (sec == 0 || sec == 2) ? v : 0;
        s_buf[2 * i + 1] = (sec == 1 || sec == 2) ? v : 0;
    }
    stereo = s_buf;
#endif
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
