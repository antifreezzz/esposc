#include "acid_osc.h"

#include <math.h>

float acid_midi_to_hz(int note)
{
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

void acid_osc_set_freq(acid_osc_t *o, float hz, float sr)
{
    o->inc = hz / sr;
}

// Поправка PolyBLEP для разрыва в фазе 0, t в [0, 1), dt = приращение фазы.
static float poly_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static void advance(acid_osc_t *o)
{
    o->phase += o->inc;
    if (o->phase >= 1.0f) {
        o->phase -= 1.0f;
    }
}

float acid_osc_saw(acid_osc_t *o)
{
    float y = 2.0f * o->phase - 1.0f - poly_blep(o->phase, o->inc);
    advance(o);
    return y;
}

float acid_osc_square(acid_osc_t *o)
{
    float y = o->phase < 0.5f ? 1.0f : -1.0f;
    y += poly_blep(o->phase, o->inc);
    float t2 = o->phase + 0.5f;
    if (t2 >= 1.0f) {
        t2 -= 1.0f;
    }
    y -= poly_blep(t2, o->inc);
    advance(o);
    return y;
}
