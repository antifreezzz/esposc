#pragma once
#include <stdint.h>

enum {
    ACID_REST = 1,
    ACID_ACCENT = 2,
    ACID_SLIDE = 4, // глайд из этого шага в следующий, гейт держится
};

typedef struct {
    int8_t note; // MIDI
    uint8_t flags;
} acid_step_t;

typedef struct {
    const acid_step_t *steps;
    int len;
    double samples_per_step;
    double pos;   // сэмплов от начала текущего шага
    int index;    // -1 до первого tick
} acid_seq_t;

void acid_seq_init(acid_seq_t *s, const acid_step_t *steps, int len, float bpm, float sr);

// Вызывать раз в сэмпл. Возвращает индекс шага на первом сэмпле шага, иначе -1.
int acid_seq_tick(acid_seq_t *s);

int acid_seq_gate(const acid_seq_t *s);
const acid_step_t *acid_seq_next_step(const acid_seq_t *s);

extern const acid_step_t acid_pattern_default[16];
