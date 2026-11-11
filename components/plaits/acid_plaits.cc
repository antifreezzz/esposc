// Голос Plaits для синта: гейт ведёт LPG (уровень), триггер - внутреннюю огибающую.
#include "acid_plaits.h"

#include <new>

#include "plaits/dsp/voice.h"

// Plaits считает высоту от частоты кодека своего железа (kCorrectedSampleRate 47872.34 Гц), мы играем на
// честных 48000: без поправки все его модели на 4.6 цента выше 303 и PETRI. 12 * log2(48000 / 47872.34).
static const float kTuneFix = -0.046105f;

struct acid_plaits {
    plaits::Voice voice;
    char ram[16384]; // общий буфер движков, как в прошивке Plaits
};

extern "C" acid_plaits_t *acid_plaits_create(void)
{
    acid_plaits_t *p = new (std::nothrow) acid_plaits_t;
    if (p) {
        acid_plaits_init(p);
    }
    return p;
}

extern "C" void acid_plaits_init(acid_plaits_t *p)
{
    stmlib::BufferAllocator alloc(p->ram, sizeof p->ram);
    p->voice.Init(&alloc);
}

extern "C" void acid_plaits_render(acid_plaits_t *p, const acid_plaits_params_t *pp, float *out, int n)
{
    plaits::Patch patch = {};
    patch.engine = pp->model;
    patch.note = pp->note + kTuneFix;
    patch.harmonics = pp->harmonics;
    patch.timbre = pp->timbre;
    patch.morph = pp->morph;
    patch.decay = pp->decay;
    patch.lpg_colour = pp->lpg_colour;
    patch.frequency_modulation_amount = pp->fm_env;
    patch.timbre_modulation_amount = pp->timbre_env;
    patch.morph_modulation_amount = pp->morph_env;

    plaits::Modulations m = {};
    m.trigger = pp->trigger ? 1.0f : 0.0f;
    m.level = pp->level;
    m.trigger_patched = !pp->sustained;
    m.level_patched = true;

    plaits::Voice::Frame frames[plaits::kMaxBlockSize];
    p->voice.Render(patch, m, frames, (size_t)n);
    for (int i = 0; i < n; i++) {
        out[i] = frames[i].out * (1.0f / 32768.0f);
    }
}

extern "C" float acid_plaits_test_rcp(float x) { return stmlib::EspRcp(x); }
extern "C" float acid_plaits_test_rsqrt(float x) { return stmlib::EspRsqrt(x); }
