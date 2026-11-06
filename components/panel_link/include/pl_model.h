#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pl_load.h"
#include "pl_proto.h"

// Что показывает панель: копия того, что прислал синт (правда - у синта).

#define PL_TOAST_MS 1500 // всплывашка от BSP
#define PL_QUIET_MS 2000 // тишина дольше - просим снимок заново
#define PL_HELLO_MS 500  // не чаще: ответ может быть в пути
#define PL_PAGE_MS 300   // повтор страницы экрана, если синт её не взял
#define PL_LOST_MS 6000  // тишина дольше при HELLO раз в 2 с - связи нет

typedef struct {
    int have; // снимок пришёл
    int mode, bsp_page;
    int demo; // песня демо PL_DEMO_* (в live - последняя)
    uint8_t raw[PL_PAGES][PL_KNOBS], bsp_pos[PL_KNOBS], held[PL_PAGES][PL_KNOBS];
    uint32_t last_rx_ms, last_hello_ms;
    int hello_sent;
    int toast_page, toast_enc;
    uint32_t toast_until_ms;
    uint32_t page_sent_ms;
    int page_sent;
    int goto_page; // BSP сменил страницу: экрану открыть её (-1 нет), интерфейс сбрасывает сам
    // Свои страницы 10..15 (индекс up = страница - 10), слоты по индексу энкодера с 0.
    pl_target_t user_t[PL_USER_PAGES][PL_KNOBS][PL_TARGETS];
    uint8_t user_val[PL_USER_PAGES][PL_KNOBS], user_held[PL_USER_PAGES][PL_KNOBS];
    int user_have; // бит up: кадр определений пришёл
    pl_scene_state_t scene;
    pl_load_t load; // загрузка ядер синта, последний кадр
    uint32_t load_ms;
    int have_load;
    int grids_step; // шаг паттерна Grids 0..31, -1 - нет данных или стоит
    pl_seq_dump_t seq[PL_SEQ_SYNTHS]; // секвенсеры синтов: копия с синта и свои правки
    pl_seq_pos_t seq_pos[PL_SEQ_SYNTHS];
    int seq_have; // бит синта: дамп пришёл
} pl_model_t;

void pl_model_init(pl_model_t *m);
// Кадр от синта; кадр не той длины пропускается целиком.
void pl_model_apply(pl_model_t *m, uint8_t type, const uint8_t *payload, size_t len, uint32_t ms);
int pl_model_need_hello(const pl_model_t *m, uint32_t ms);
void pl_model_hello_sent(pl_model_t *m, uint32_t ms);
// Связь жива: снимок был и синт отвечал недавно (значения при обрыве не стираются, только индикатор).
int pl_model_linked(const pl_model_t *m, uint32_t ms);
// Экран на странице screen_page, а синт (live) на другой и BSP не листал: пора повторить PL_PAGE.
int pl_model_page_resend(const pl_model_t *m, int screen_page, uint32_t ms);
// Экран сам выбрал страницу и отправил её синту; отменяет ждущее листание с BSP.
void pl_model_page_sent(pl_model_t *m, uint32_t ms);
// Своя правка: показать сразу, не дожидаясь эха; синт пометит её «ждёт BSP».
// 1 - правка принята и её надо слать синту; 0 - снимка ещё нет (значения не настоящие) или не ручка.
// page 10..15 - своя страница (нужен её кадр определений).
int pl_model_local_set(pl_model_t *m, int page, int enc, int v);

// Правки секвенсера с панели: сразу в модели. 1 - принято, слать синту; 0 - дампа синта ещё нет или вне диапазона.
int pl_model_seq_step(pl_model_t *m, int synth, int pat, int step, uint8_t v);
int pl_model_seq_oct(pl_model_t *m, int synth, int pat, int oct); // -2..+2
int pl_model_seq_row(pl_model_t *m, int synth, int row, const uint8_t cell[PL_SEQ_CELLS], int rep);
int pl_model_seq_ctrl(pl_model_t *m, int synth, int run, int edit); // run PL_SEQ_KEEP - как есть
int pl_model_seq_bpm(pl_model_t *m, int bpm); // общий у обоих синтов, 60..200
