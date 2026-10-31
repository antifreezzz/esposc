// Проверка провода синт <-> панель по UART1.
// Синт (DevKit): TX IO17 -> RXD IO40 панели, RX IO18 <- TXD IO2 панели.
// Синт шлёт "ping N\n" 100 раз в секунду, панель возвращает байты как есть, синт сверяет и меряет задержку.
#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link_check.h"

#ifndef LINK_BAUD
#define LINK_BAUD 115200
#endif

#if LINK_PANEL
#define PIN_TX 2
#define PIN_RX 40
#else
#define PIN_TX 17
#define PIN_RX 18
#endif

#define PORT UART_NUM_1
#define REPORT_US (5 * 1000 * 1000)
#define PING_MS 10

static const char *TAG = "link";

static void link_uart_init(void) {
    const uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PORT, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PORT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // без подтяжки висящий RX (провод не воткнут) ловит наводки как мусор
    gpio_pullup_en(PIN_RX);
}

#if LINK_PANEL

void app_main(void) {
    link_uart_init();
    ESP_LOGI(TAG, "panel echo: RX IO%d, TX IO%d, %d baud", PIN_RX, PIN_TX, LINK_BAUD);
    uint8_t buf[128];
    uint32_t total = 0;
    int64_t t_report = esp_timer_get_time() + REPORT_US;
    for (;;) {
        int n = uart_read_bytes(PORT, buf, sizeof buf, pdMS_TO_TICKS(20));
        if (n > 0) {
            uart_write_bytes(PORT, buf, n);
            total += n;
        }
        if (esp_timer_get_time() >= t_report) {
            t_report += REPORT_US;
            ESP_LOGI(TAG, "echoed %" PRIu32 " bytes", total);
        }
    }
}

#else

#define RTT_RING 256 // время отправки по номеру: ответ дольше 2.5 с уже не сопоставить

static int64_t s_sent_at[RTT_RING];
static link_check_t s_check;

static void rx_task(void *arg) {
    (void)arg;
    uint8_t buf[128];
    int64_t rtt_sum = 0, rtt_max = 0;
    uint32_t rtt_n = 0;
    for (;;) {
        int n = uart_read_bytes(PORT, buf, sizeof buf, pdMS_TO_TICKS(20));
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < n; i++) {
            uint32_t ok = s_check.ok;
            link_check_byte(&s_check, buf[i]);
            if (s_check.ok != ok) {
                int64_t rtt = now - s_sent_at[(s_check.next - 1) % RTT_RING];
                rtt_sum += rtt;
                rtt_n++;
                if (rtt > rtt_max) rtt_max = rtt;
            }
        }
        static int64_t t_report;
        if (!t_report) t_report = now + REPORT_US;
        if (now >= t_report) {
            t_report += REPORT_US;
            ESP_LOGI(TAG, "ok %" PRIu32 " bad %" PRIu32 " lost %" PRIu32 " | rtt avg %" PRId64 " max %" PRId64 " us",
                     s_check.ok, s_check.bad, s_check.lost, rtt_n ? rtt_sum / rtt_n : 0, rtt_max);
            rtt_sum = rtt_max = 0;
            rtt_n = 0;
        }
    }
}

void app_main(void) {
    link_uart_init();
    link_check_init(&s_check);
    ESP_LOGI(TAG, "synth ping: TX IO%d, RX IO%d, %d baud", PIN_TX, PIN_RX, LINK_BAUD);
    xTaskCreate(rx_task, "link_rx", 4096, NULL, 5, NULL);
    char line[LINK_LINE_MAX];
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t n = 0;; n++) {
        int len = link_format_ping(line, sizeof line, n);
        s_sent_at[n % RTT_RING] = esp_timer_get_time();
        uart_write_bytes(PORT, line, len);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PING_MS));
    }
}

#endif
