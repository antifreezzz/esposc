#include "link.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#define PORT UART_NUM_1
#define PIN_TX 2
#define PIN_RX 40
#define BAUD 1000000

static const char *TAG = "link";
static pl_rx_t s_rx;

static void send(uint8_t type, const void *p, size_t n)
{
    uint8_t f[PL_FRAME_MAX];
    uart_write_bytes(PORT, f, pl_encode(type, p, n, f));
}

esp_err_t link_start(void)
{
    const uart_config_t cfg = {.baud_rate = BAUD, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
                               .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                               .source_clk = UART_SCLK_DEFAULT};
    ESP_RETURN_ON_ERROR(uart_driver_install(PORT, 4096, 1024, 0, NULL, 0), TAG, "install");
    ESP_RETURN_ON_ERROR(uart_param_config(PORT, &cfg), TAG, "config");
    ESP_RETURN_ON_ERROR(uart_set_pin(PORT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "pins");
    gpio_pullup_en(PIN_RX); // синт без питания: вход в покое единица
    pl_rx_init(&s_rx);
    return ESP_OK;
}

void link_poll(pl_model_t *m, uint32_t ms)
{
    uint8_t buf[256];
    int n;
    while ((n = uart_read_bytes(PORT, buf, sizeof buf, 0)) > 0) {
        for (int i = 0; i < n; i++) {
            uint8_t type;
            const uint8_t *p;
            size_t len;
            int got = pl_rx_feed(&s_rx, buf[i], &type, &p, &len);
            while (got) {
                pl_model_apply(m, type, p, len, ms);
                got = pl_rx_next(&s_rx, &type, &p, &len);
            }
        }
    }
    static int up;
    if (m->have && !up) {
        ESP_LOGI(TAG, "synth link up");
    }
    up = m->have;
    if (pl_model_need_hello(m, ms)) {
        send(PL_HELLO, NULL, 0);
        pl_model_hello_sent(m, ms);
    }
}

void link_set(int page, int enc, int v)
{
    pl_set_t s = {(uint8_t)page, (uint8_t)enc, (uint8_t)v};
    send(PL_SET, &s, sizeof s);
}

void link_page(int page)
{
    uint8_t p = (uint8_t)page;
    send(PL_PAGE, &p, 1);
}

void link_custom_edit(const pl_custom_edit_t *e)
{
    send(PL_CUSTOM_EDIT, e, sizeof *e);
}

void link_mod_edit(const pl_mod_edit_t *e)
{
    send(PL_MOD_EDIT, e, sizeof *e);
}

void link_scene(int op, int n, int bars)
{
    pl_scene_t s = {(uint8_t)op, (uint8_t)n, (uint8_t)bars};
    send(PL_SCENE, &s, sizeof s);
}

void link_mode(int mode)
{
    uint8_t m = (uint8_t)mode;
    send(PL_MODE, &m, 1);
}

void link_petri(const uint8_t out[4][PL_PETRI_PART_MAX], const size_t len[4])
{
    for (int k = 0; k < 4; k++) {
        send((uint8_t)(PL_PETRI_0 + k), out[k], len[k]); // uart_write_bytes под мьютексом: кадр не рвётся
    }
}

void link_seq_step(int synth, int pat, int step, int v)
{
    uint8_t p[4] = {(uint8_t)synth, (uint8_t)pat, (uint8_t)step, (uint8_t)v};
    send(PL_SEQ_STEP, p, sizeof p);
}

void link_seq_oct(int synth, int pat, int oct)
{
    uint8_t p[3] = {(uint8_t)synth, (uint8_t)pat, (uint8_t)(oct + 2)};
    send(PL_SEQ_OCT, p, sizeof p);
}

void link_seq_scale(int synth, int pat, int scale)
{
    uint8_t p[3] = {(uint8_t)synth, (uint8_t)pat, (uint8_t)scale};
    send(PL_SEQ_SCALE, p, sizeof p);
}

void link_seq_row(int synth, int row, const uint8_t cells[PL_SEQ_CELLS], int rep)
{
    uint8_t p[3 + PL_SEQ_CELLS];
    p[0] = (uint8_t)synth;
    p[1] = (uint8_t)row;
    memcpy(p + 2, cells, PL_SEQ_CELLS);
    p[2 + PL_SEQ_CELLS] = (uint8_t)rep;
    send(PL_SEQ_ROW, p, sizeof p);
}

void link_seq_ctrl(int synth, int run, int edit)
{
    uint8_t p[3] = {(uint8_t)synth, (uint8_t)run, (uint8_t)edit};
    send(PL_SEQ_CTRL, p, sizeof p);
}

void link_seq_bpm(int bpm)
{
    uint8_t b = (uint8_t)bpm;
    send(PL_SEQ_BPM, &b, 1);
}

void link_grids_cell(int slot, int part, int step, int lv)
{
    uint8_t p[4] = {(uint8_t)slot, (uint8_t)part, (uint8_t)step, (uint8_t)lv};
    send(PL_GRIDS_CELL, p, sizeof p);
}

void link_grids_pat(int slot, const pl_grids_pat_t *pat)
{
    pl_grids_pat_frame_t fr = {.slot = (uint8_t)slot, .pat = *pat};
    send(PL_GRIDS_PAT, &fr, sizeof fr);
}
