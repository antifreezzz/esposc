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
// Сцена: op PL_SCENE_RECALL или PL_SCENE_STORE, слот n, переход bars тактов.
void link_scene(int op, int n, int bars);
// Тап по статусу в демо: синту - следующая песня демо.
void link_demo(void);
// Таблица чашки Петри синту: 4 кадра (из задачи чашки, ядро 1).
void link_petri(const uint8_t out[4][PL_PETRI_PART_MAX], const size_t len[4]);
