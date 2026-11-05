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
#define RX_BUF 2048 // таблица чашки - 4 кадра, 532 байта подряд
#define TX_BUF 4096
#define SET_QUEUE 32
#define KNOB_QUEUE 128
#define POLL_TICKS 1 // тик 10 мс: опрос не чаще, иначе задача не спит

static const char *TAG = "panel";

_Static_assert(ACID_PAGE_PETRI == PL_PAGE_PETRI && ACID_BSP_PETRI == 18 && ACID_TYPE_PETRI == 25 &&
                   ACID_LAYER_OFF == 26 && ACID_PETRI_TABLE == PL_PETRI_POINTS,
               "petri numbers shared by synth and panel (pl_labels, petri_knobs, petri_task, pl_page_bsp)");
_Static_assert(ACID_PAGES == PL_PAGES && ACID_PAGE_GRIDS == PL_PAGE_GRIDS && ACID_USER_FIRST == PL_USER_FIRST,
               "synth and panel page numbering");

static QueueHandle_t s_set_q, s_knob_q, s_page_q, s_edit_q, s_scene_q, s_petri_q;
static volatile uint32_t s_petri_count;
static const acid_synth_t *s_synth;
static const acid_link_t *s_link;
static const acid_custom_t *s_custom;
static const acid_scene_t *s_scene;
static const volatile int *s_demo_on;
static const volatile int *s_demo_song;
static volatile int s_demo_due; // панель попросила следующее демо
static volatile int s_snapshot_due;
static unsigned s_custom_due; // бит своей страницы; ставит ядро 1, забирает задача связи (атомарно)
static volatile int s_scene_due;

int panel_uart_receive_edit(pl_custom_edit_t *out)
{
    return s_edit_q && xQueueReceive(s_edit_q, out, 0) == pdTRUE;
}

int panel_uart_receive_scene(pl_scene_t *out)
{
    return s_scene_q && xQueueReceive(s_scene_q, out, 0) == pdTRUE;
}

void panel_uart_custom_due(unsigned mask)
{
    __atomic_fetch_or(&s_custom_due, mask, __ATOMIC_RELAXED);
}

void panel_uart_scene_due(void)
{
    s_scene_due = 1;
}

int panel_uart_receive_petri(acid_petri_frame_t *out)
{
    return s_petri_q && xQueueReceive(s_petri_q, out, 0) == pdTRUE;
}

uint32_t panel_uart_petri_count(void)
{
    return s_petri_count;
}

int panel_uart_receive(pl_set_t *out)
{
    return s_set_q && xQueueReceive(s_set_q, out, 0) == pdTRUE;
}

int panel_uart_receive_page(int *page)
{
    uint8_t p;
    if (!s_page_q || xQueueReceive(s_page_q, &p, 0) != pdTRUE) {
        return 0;
    }
    *page = p;
    return 1;
}

void panel_uart_send_knob(const acid_link_ev_t *ev)
{
    if (s_knob_q) {
        xQueueSend(s_knob_q, ev, 0);
    }
}

static pl_load_t s_load;
static volatile int s_load_due;

void panel_uart_send_load(const pl_load_t *l)
{
    s_load = *l; // 6 байт, рваное значение - одна неверная цифра на полсекунды
    s_load_due = 1;
}

int panel_uart_take_demo(void)
{
    if (!s_demo_due) {
        return 0;
    }
    s_demo_due = 0;
    return 1;
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
    static pl_snapshot2_t snap2;
    memset(&snap, 0, sizeof snap);
    memset(&snap2, 0, sizeof snap2);
    snap.mode = *s_demo_on ? 0 : 1;
    snap.demo = (uint8_t)*s_demo_song;
    snap.bsp_page = (uint8_t)acid_link_page(s_link, s_synth);
    for (int p = 0; p < PL_PAGES; p++) {
        for (int e = 0; e < PL_KNOBS; e++) {
            snap.raw[p][e] = (uint8_t)acid_synth_knob(s_synth, p, e);
            int i = p * PL_KNOBS + e;
            snap2.held[i / 8] |= (uint8_t)((s_link->held[p][e] ? 1 : 0) << (i % 8));
        }
    }
    memcpy(snap2.bsp_pos, s_link->bsp_pos, sizeof snap2.bsp_pos);
    send(PL_SNAPSHOT, &snap, sizeof snap);
    send(PL_SNAPSHOT2, &snap2, sizeof snap2); // весь снимок с 14 страницами не влезает в один кадр
}

static void send_custom(int up)
{
    static pl_custom_t f;
    f.up = (uint8_t)up;
    memset(f.held, 0, sizeof f.held);
    for (int e = 1; e < PL_KNOBS; e++) {
        for (int i = 0; i < PL_TARGETS; i++) {
            const acid_target_t *t = &s_custom->t[up][e][i];
            f.t[e - 1][i] = t->page == ACID_CUSTOM_NONE ? (pl_target_t){PL_NONE, 0, 0}
                                                        : (pl_target_t){(uint8_t)(t->page << 4 | t->enc), t->lo, t->hi};
        }
        f.val[e - 1] = (uint8_t)acid_custom_value(s_custom, s_synth, up, e);
        f.held[(e - 1) / 8] |= (uint8_t)((s_link->held[ACID_USER_FIRST + up][e] ? 1 : 0) << ((e - 1) % 8));
    }
    send(PL_CUSTOM, &f, sizeof f);
}

static void send_scene_state(void)
{
    pl_scene_state_t st = {s_scene->used, s_scene->cur < 0 ? PL_NONE : (uint8_t)s_scene->cur, (uint8_t)s_scene->state};
    send(PL_SCENE_STATE, &st, sizeof st);
}

static void link_task(void *arg)
{
    (void)arg;
    static pl_rx_t rx;
    static pl_petri_rx_t prx;
    static acid_petri_frame_t pf;
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
                } else if (type == PL_PAGE && len == 1) {
                    xQueueSend(s_page_q, p, 0);
                } else if (type == PL_CUSTOM_EDIT && len == sizeof(pl_custom_edit_t)) {
                    xQueueSend(s_edit_q, p, 0);
                } else if (type == PL_SCENE && len == sizeof(pl_scene_t)) {
                    xQueueSend(s_scene_q, p, 0);
                } else if (type >= PL_PETRI_0 && type <= PL_PETRI_3) {
                    if (pl_petri_collect(&prx, type, p, len)) {
                        memcpy(pf.tab, prx.tab, sizeof pf.tab);
                        memcpy(pf.mods, prx.mods, sizeof pf.mods);
                        pf.jumps = prx.jumps;
                        xQueueOverwrite(s_petri_q, &pf); // синту нужна только последняя
                        s_petri_count++;
                    }
                } else if (type == PL_DEMO) {
                    s_demo_due = 1;
                } else if (type == PL_HELLO) {
                    s_snapshot_due = 1;
                }
                got = pl_rx_next(&rx, &type, &p, &len);
            }
        }
        unsigned due = __atomic_exchange_n(&s_custom_due, 0, __ATOMIC_RELAXED);
        if (s_snapshot_due) {
            s_snapshot_due = 0;
            send_snapshot();
            due = (1u << ACID_USER_PAGES) - 1; // за снимком - свои страницы и сцены
            s_scene_due = 1;
        }
        for (int up = 0; up < ACID_USER_PAGES; up++) {
            if (due & (1u << up)) {
                send_custom(up);
            }
        }
        if (s_scene_due) {
            s_scene_due = 0;
            send_scene_state();
        }
        if (s_load_due) {
            s_load_due = 0;
            pl_load_t l = s_load;
            send(PL_LOAD, &l, sizeof l);
        }
        // Шаг Grids: синт меняет его на тике клока (раз в шестнадцатую), опрос раз в 10 мс успевает.
        static int grids_sent = -2;
        int g = s_synth->grids_step;
        if (g != grids_sent) {
            grids_sent = g;
            uint8_t b = g < 0 ? 0xFF : (uint8_t)g;
            send(PL_GRIDS_STEP, &b, 1);
        }
        acid_link_ev_t ev;
        while (xQueueReceive(s_knob_q, &ev, 0) == pdTRUE) {
            pl_knob_t k = {ev.page, ev.enc, ev.value, ev.src, ev.bsp_pos, ev.held};
            send(PL_KNOB, &k, sizeof k);
        }
    }
}

esp_err_t panel_uart_start(const acid_synth_t *s, const acid_link_t *l, const acid_custom_t *c, const acid_scene_t *sc,
                           const volatile int *demo_on, const volatile int *demo_song)
{
    s_synth = s;
    s_link = l;
    s_custom = c;
    s_scene = sc;
    s_demo_on = demo_on;
    s_demo_song = demo_song;
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
    s_page_q = xQueueCreate(4, 1);
    s_edit_q = xQueueCreate(8, sizeof(pl_custom_edit_t));
    s_scene_q = xQueueCreate(4, sizeof(pl_scene_t));
    s_petri_q = xQueueCreate(1, sizeof(acid_petri_frame_t));
    if (!s_set_q || !s_knob_q || !s_page_q || !s_edit_q || !s_scene_q) {
        return ESP_ERR_NO_MEM;
    }
    // Ниже slot1 и USB: связь с экраном подождёт, звук нет.
    xTaskCreatePinnedToCore(link_task, "panel", 4096, NULL, 3, NULL, 0);
    return ESP_OK;
}
