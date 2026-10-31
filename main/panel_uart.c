#include "panel_uart.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define PORT UART_NUM_1
#define PIN_TX 17
#define PIN_RX 18
#define BAUD 1000000
#define RX_BUF 1024
#define TX_BUF 4096
#define SET_QUEUE 32
#define KNOB_QUEUE 128
#define POLL_TICKS 1 // тик 10 мс: опрос не чаще, иначе задача не спит

static const char *TAG = "panel";

static QueueHandle_t s_set_q, s_knob_q;
static const acid_synth_t *s_synth;
static const acid_link_t *s_link;
static const volatile int *s_demo_on;
static volatile int s_snapshot_due;

int panel_uart_receive(pl_set_t *out)
{
    return s_set_q && xQueueReceive(s_set_q, out, 0) == pdTRUE;
}

void panel_uart_send_knob(const acid_link_ev_t *ev)
{
    if (s_knob_q) {
        xQueueSend(s_knob_q, ev, 0);
    }
}

void panel_uart_snapshot_due(void)
{
    s_snapshot_due = 1;
}

static void send(uint8_t type, const void *p, size_t n)
{
    uint8_t f[PL_FRAME_MAX];
    size_t len = pl_encode(type, p, n, f);
    uart_write_bytes(PORT, f, len);
}

static void send_snapshot(void)
{
    static pl_snapshot_t snap;
    memset(&snap, 0, sizeof snap);
    snap.mode = *s_demo_on ? 0 : 1;
    snap.bsp_page = (uint8_t)s_synth->page;
    for (int p = 0; p < PL_PAGES; p++) {
        for (int e = 0; e < PL_KNOBS; e++) {
            snap.raw[p][e] = (uint8_t)acid_synth_knob(s_synth, p, e);
            int i = p * PL_KNOBS + e;
            snap.held[i / 8] |= (uint8_t)((s_link->held[p][e] ? 1 : 0) << (i % 8));
        }
    }
    memcpy(snap.bsp_pos, s_link->bsp_pos, sizeof snap.bsp_pos);
    send(PL_SNAPSHOT, &snap, sizeof snap);
}

static void link_task(void *arg)
{
    (void)arg;
    static pl_rx_t rx;
    pl_rx_init(&rx);
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(PORT, buf, sizeof buf, POLL_TICKS);
        for (int i = 0; i < n; i++) {
            uint8_t type;
            const uint8_t *p;
            size_t len;
            int got = pl_rx_feed(&rx, buf[i], &type, &p, &len);
            while (got) {
                if (type == PL_SET && len == sizeof(pl_set_t)) {
                    pl_set_t set;
                    memcpy(&set, p, sizeof set);
                    xQueueSend(s_set_q, &set, 0);
                } else if (type == PL_HELLO) {
                    s_snapshot_due = 1;
                }
                got = pl_rx_next(&rx, &type, &p, &len);
            }
        }
        if (s_snapshot_due) {
            s_snapshot_due = 0;
            send_snapshot();
        }
        acid_link_ev_t ev;
        while (xQueueReceive(s_knob_q, &ev, 0) == pdTRUE) {
            pl_knob_t k = {ev.page, ev.enc, ev.value, ev.src, ev.bsp_pos, ev.held};
            send(PL_KNOB, &k, sizeof k);
        }
    }
}

esp_err_t panel_uart_start(const acid_synth_t *s, const acid_link_t *l, const volatile int *demo_on)
{
    s_synth = s;
    s_link = l;
    s_demo_on = demo_on;
    const uart_config_t cfg = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(PORT, RX_BUF, TX_BUF, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(PORT, &cfg), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(PORT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pins");
    gpio_pullup_en(PIN_RX); // панель без питания: вход в покое единица, меньше мусора
    s_set_q = xQueueCreate(SET_QUEUE, sizeof(pl_set_t));
    s_knob_q = xQueueCreate(KNOB_QUEUE, sizeof(acid_link_ev_t));
    if (!s_set_q || !s_knob_q) {
        return ESP_ERR_NO_MEM;
    }
    // Ниже slot1 и USB: связь с экраном подождёт, звук нет.
    xTaskCreatePinnedToCore(link_task, "panel", 4096, NULL, 3, NULL, 0);
    return ESP_OK;
}
