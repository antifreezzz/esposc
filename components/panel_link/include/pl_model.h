#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pl_proto.h"

// Что показывает панель: копия того, что прислал синт (правда - у синта).

#define PL_TOAST_MS 1500 // всплывашка от BSP
#define PL_QUIET_MS 2000 // тишина дольше - просим снимок заново
#define PL_HELLO_MS 500  // не чаще: ответ может быть в пути

typedef struct {
    int have; // снимок пришёл
    int mode, bsp_page;
    uint8_t raw[PL_PAGES][PL_KNOBS], bsp_pos[PL_KNOBS], held[PL_PAGES][PL_KNOBS];
    uint32_t last_rx_ms, last_hello_ms;
    int hello_sent;
    int toast_page, toast_enc;
    uint32_t toast_until_ms;
} pl_model_t;

void pl_model_init(pl_model_t *m);
// Кадр от синта; кадр не той длины пропускается целиком.
void pl_model_apply(pl_model_t *m, uint8_t type, const uint8_t *payload, size_t len, uint32_t ms);
int pl_model_need_hello(const pl_model_t *m, uint32_t ms);
void pl_model_hello_sent(pl_model_t *m, uint32_t ms);
// Своя правка: показать сразу, не дожидаясь эха; синт пометит её «ждёт BSP».
void pl_model_local_set(pl_model_t *m, int page, int enc, int v);
