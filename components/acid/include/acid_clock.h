#pragma once
#include <stdint.h>

#define ACID_CLOCK_PPQN 24
#define ACID_CLOCK_WIN 48 // интервалов в окне: две четверти, джиттер доставки ~5 мс даёт < 1%

// Темп по MIDI clock: средний интервал тиков за две четверти, с гистерезисом против джиттера доставки.
typedef struct {
    float sr;
    uint32_t last;   // время прошлого тика, сэмплы
    int have_last;
    uint32_t iv[ACID_CLOCK_WIN]; // последние интервалы
    int n, pos;
    int gap;         // прошлый интервал был разрывом
    float spb;       // сэмплов на четверть, отданный наружу
} acid_clock_t;

void acid_clock_init(acid_clock_t *c, float sr);    // 120 BPM
void acid_clock_tick(acid_clock_t *c, uint32_t now); // now - сэмплы с начала
float acid_clock_spb(const acid_clock_t *c);         // сэмплов на четверть
