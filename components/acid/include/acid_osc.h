#pragma once

// Номер MIDI-ноты в частоту, A4 = 69 = 440 Гц.
float acid_midi_to_hz(int note);

typedef struct {
    float phase; // [0, 1)
    float inc;   // приращение фазы за сэмпл
    float idt;   // 1 / inc: PolyBLEP без деления на сэмпле
} acid_osc_t;

void acid_osc_set_freq(acid_osc_t *o, float hz, float sr);

// Оба с PolyBLEP, диапазон примерно [-1, 1], каждый вызов сдвигает фазу. Инлайн: вызов из другого файла на
// каждый сэмпл на плате дорог.
// Поправка PolyBLEP для разрыва в фазе 0, t в [0, 1), dt = приращение фазы.
static inline float acid_osc_blep(float t, float dt, float idt)
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline void acid_osc_advance(acid_osc_t *o)
{
    o->phase += o->inc;
    if (o->phase >= 1.0f) {
        o->phase -= 1.0f;
    }
}

static inline float acid_osc_saw(acid_osc_t *o)
{
    float y = 2.0f * o->phase - 1.0f - acid_osc_blep(o->phase, o->inc, o->idt);
    acid_osc_advance(o);
    return y;
}

static inline float acid_osc_square(acid_osc_t *o)
{
    float y = o->phase < 0.5f ? 1.0f : -1.0f;
    y += acid_osc_blep(o->phase, o->inc, o->idt);
    float t2 = o->phase + 0.5f;
    if (t2 >= 1.0f) {
        t2 -= 1.0f;
    }
    y -= acid_osc_blep(t2, o->inc, o->idt);
    acid_osc_advance(o);
    return y;
}
