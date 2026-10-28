#pragma once
#include <stddef.h>

#include "esp_err.h"

// Память ручек синтов в NVS: один blob.
esp_err_t store_start(void);
// Прочитать blob: длина или 0, если записи нет.
size_t store_load(void *buf, size_t cap);
// Записать копию blob в фоне (задача низкого приоритета на ядре 0). Звать, когда звук молчит:
// запись во flash останавливает кэш обоих ядер.
void store_save(const void *buf, size_t len);
