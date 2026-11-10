#pragma once
#include <stddef.h>

#include "esp_err.h"

// Память в NVS: blob на ключ. Ключи: STORE_KNOBS (ручки синтов), STORE_CUSTOM (свои страницы),
// STORE_SCENES (сцены), STORE_SEQ (секвенсер), STORE_MOD (модуляторы), STORE_MODE (режим без BSP).
#define STORE_KNOBS "knobs"
#define STORE_CUSTOM "custom"
#define STORE_SCENES "scenes"
#define STORE_SEQ "seq"
#define STORE_MOD "mod"
#define STORE_MODE "mode"

esp_err_t store_start(void);
// Прочитать blob ключа: длина или 0, если записи нет.
size_t store_load(const char *key, void *buf, size_t cap);
// Записать копию blob ключа в фоне (задача низкого приоритета на ядре 0). Звать, когда звук молчит:
// запись во flash останавливает кэш обоих ядер.
void store_save(const char *key, const void *buf, size_t len);
