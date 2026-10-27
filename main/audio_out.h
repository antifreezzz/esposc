#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Вывод звука: 48 кГц, 16 бит, стерео, кадры L,R. Сейчас через USB-свисток (UAC),
// потом рядом встанет I2S с тем же интерфейсом.

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
