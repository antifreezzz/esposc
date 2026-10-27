#pragma once

// Номер MIDI-ноты в частоту, A4 = 69 = 440 Гц.
float acid_midi_to_hz(int note);

typedef struct {
    float phase; // [0, 1)
    float inc;   // приращение фазы за сэмпл
} acid_osc_t;

void acid_osc_set_freq(acid_osc_t *o, float hz, float sr);

// Оба с PolyBLEP, диапазон примерно [-1, 1], каждый вызов сдвигает фазу.
float acid_osc_saw(acid_osc_t *o);
float acid_osc_square(acid_osc_t *o);
