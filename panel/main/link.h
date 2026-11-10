#pragma once
#include <stdint.h>

#include "esp_err.h"
#include "pl_model.h"

// UART1 к синту: IO2 TX, IO40 RX, 1 Мбод. Опрос из цикла интерфейса, без своей задачи.
esp_err_t link_start(void);
// Разобрать пришедшее в модель, при нужде попросить снимок.
void link_poll(pl_model_t *m, uint32_t ms);
void link_set(int page, int enc, int v);
// Открыли страницу: энкодеры BSP крутят её.
void link_page(int page);
// Правка цели слота своей страницы.
void link_custom_edit(const pl_custom_edit_t *e);
// Правка цели модулятора (PL_MOD_EDIT, page PL_NONE - убрать).
void link_mod_edit(const pl_mod_edit_t *e);
// Сцена: op PL_SCENE_RECALL или PL_SCENE_STORE, слот n, переход bars тактов.
void link_scene(int op, int n, int bars);
// Поставить режим: PL_DEMO_* без BSP, с BSP - PL_MODE_LIVE или PL_DEMO_DRONE.
void link_mode(int mode);
// Таблица чашки Петри синту: 4 кадра (из задачи чашки, ядро 1).
void link_petri(const uint8_t out[4][PL_PETRI_PART_MAX], const size_t len[4]);
// Секвенсер: шаг такта, окно, строка песни, ▶/■ и такт экрана, темп (pl_proto.h, PL_SEQ_*).
void link_seq_step(int synth, int pat, int step, int v);
void link_seq_oct(int synth, int pat, int oct);
void link_seq_scale(int synth, int pat, int scale); // 0 - лад страницы 9, 1 + k - лад k
void link_seq_row(int synth, int row, const uint8_t cells[PL_SEQ_CELLS], int rep);
void link_seq_ctrl(int synth, int run, int edit);
void link_seq_bpm(int bpm);
// Свои рисунки Grids: клетка и рисунок целиком.
void link_grids_cell(int slot, int part, int step, int lv);
void link_grids_pat(int slot, const pl_grids_pat_t *pat);
