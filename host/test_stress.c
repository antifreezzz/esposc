// Стресс-песня стенда: держит худший случай рендера (оба синта modal, все эффекты, тарелки на +12,
// дробь перкуссии на модальной модели), чтобы замеры на плате были сравнимы между правками.
#include "acid.h"
#include "acid_demo.h"
#include "test.h"

#define SR 48000.0f
#define CH ACID_DEMO_CHUNK
#define MODAL 21

static acid_synth_t s;
static acid_demo_t d;

TEST(test_stress_worst_case)
{
    static acid_midi_event_t ev[96];
    float l[CH], r[CH];
    acid_synth_init(&s, SR);
    acid_demo_init(&d, &acid_stress_song);
    int finite = 1, perc_modal = 0, perc_max = 0, chunks = 0, held = 0;
    int drum[128] = {0};
    // 2 с на разгон, дальше 8 с проверяем, что худший случай держится всё время.
    for (int k = 0; k < 10 * 48000 / CH; k++) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            if (ev[i].type == ACID_MIDI_NOTE_ON && ev[i].ch == 1) {
                drum[ev[i].d1]++;
            }
            acid_synth_midi(&s, &ev[i]);
        }
        acid_synth_render_lr(&s, l, r, CH);
        for (int i = 0; i < CH; i++) {
            finite &= isfinite(l[i]) && isfinite(r[i]);
        }
        if (k < 2 * 48000 / CH) {
            continue;
        }
        chunks++;
        int ok = s.slot[0].type == MODAL && s.slot[1].type == MODAL;
        ok &= acid_crush_active(&s.crush) && acid_delay_active(&s.delay) && acid_pump_active(&s.pump);
        ok &= s.stutter.knob > 0 && s.djf.mode != 0 && s.drive > 2.9f;
        ok &= s.send1 > 0.0f && s.send2 > 0.0f && s.send_drums > 0.0f;
        ok &= s.cym_tune[0] == 76 && s.cym_tune[1] == 76;
        held += ok;
        int np = 0;
        for (int v = 0; v < ACID_PERC_SLOTS; v++) {
            const acid_perc_voice_t *pv = &s.perc.v[v];
            if (pv->active) {
                np++;
                perc_modal += s.perc.inst[pv->inst].model == 3;
            }
        }
        perc_max = np > perc_max ? np : perc_max;
    }
    CHECK(finite);
    CHECK(held == chunks);
    CHECK(perc_max >= ACID_PERC_SOUNDING);
    CHECK(perc_modal > 0);
    CHECK(drum[49] && drum[51] && drum[53]);
    CHECK(drum[37] && drum[41] && drum[50] && drum[56]);
}

TEST(test_stress_song_fits)
{
    int ok = 1;
    for (int i = 0; i < acid_stress_song.nparts; i++) {
        ok &= acid_stress_song.parts[i].nknobs <= ACID_DEMO_KNOBS;
    }
    CHECK(ok);
}

int main(void)
{
    RUN(test_stress_song_fits);
    RUN(test_stress_worst_case);
    TEST_MAIN_END();
}
