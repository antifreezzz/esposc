#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_bank.h"

// Синтетический набор для тестов: у повтора r слоя l ноты n - синус постоянной громкости,
// громкость по слою, частота по ноте и повтору. L и R одинаковые.
typedef struct {
    int note;
    int nlayers;
    int vel_hi[5];
    int nrr[5];
    int frames;
} fixture_note_t;

float fixture_level(int layer);
float fixture_hz(int note, int rr);
// Собирает образ в buf (выровнен на 4). Возврат - размер, 0 если не влез.
size_t fixture_bank(uint8_t *buf, size_t cap, const fixture_note_t *notes, int n);
// IMA ADPCM кодер: следующий полубайт для x, состояние обновляется как у декодера.
uint8_t fixture_adpcm_encode(acid_adpcm_t *st, int16_t x);
