#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Вывод звука: 48 кГц, 16 бит, стерео, кадры L,R. ЦАП PCM5102A на I2S (audio_out_i2s.c, по умолчанию)
// или USB-свисток (audio_out_uac.c, сборка -DESPOSC_UAC=1). У I2S устройство всегда на месте.

// Ставит драйвер и возвращается сразу. Устройство открывается само при подключении.
esp_err_t audio_out_start(void);

// Блокирует до timeout_ms. Возвращает принятые кадры; 0 если устройства нет.
size_t audio_out_write(const int16_t *stereo, size_t frames, uint32_t timeout_ms);

int audio_out_connected(void);

// Блокирует, пока устройство не подключится (или timeout_ms). 1 если подключено.
int audio_out_wait_connected(uint32_t timeout_ms);

// Сколько раз буфер драйвера опускался до минимума (почти недогруз).
uint32_t audio_out_underruns(void);

// Ошибки изохронной передачи (счётчик, без очереди событий).
uint32_t audio_out_transfer_errors(void);
