#pragma once
#include "acid_bank.h"

// Образ тарелок из раздела флэша samples: отображается в память без копирования. NULL - раздела нет
// или образ плохой (тарелки молчат), причина в логе.
const acid_bank_t *samples_part_open(void);
