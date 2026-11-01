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
