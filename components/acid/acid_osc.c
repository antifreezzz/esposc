#include "acid_osc.h"

#include <math.h>

#include "acid_math.h"

float acid_midi_to_hz(int note)
{
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

void acid_osc_set_freq(acid_osc_t *o, float hz, float sr)
{
    o->inc = hz / sr; // на управляющей частоте: точно как раньше, иначе фаза уползает
    o->idt = acid_rcp(o->inc);
}
