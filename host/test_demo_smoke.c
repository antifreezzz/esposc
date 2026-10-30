// Смоук демо-трека: синт и плеер вместе, как в прошивке (события перед каждым куском).
#include "acid.h"
#include "acid_demo.h"
#include "test.h"

#include <string.h>

#define SR 48000.0f
#define CH ACID_DEMO_CHUNK
#define WIN 48000 // окно 1 с

static acid_synth_t s;
static acid_demo_t d;

typedef struct {
    int wave[2], models[ACID_TYPES], drum[128], hat_knob, pump, djf[3], drive, pan;
    int motion[3], times[5], sends, crush, stutter[5], tonal_step, scale_chrom, scale_mode, cym_knob;
    long silent_windows, wide_windows;
    int finite;
} seen_t;

static void start(void)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
    acid_demo_init(&d, &acid_demo_song);
}

// Играет до конца круга c, собирает что увидено.
static void play_cycle(seen_t *z, int c)
{
    static acid_midi_event_t ev[96];
    float l[CH], r[CH];
    double sl = 0.0, sd = 0.0, ss = 0.0;
    int w = 0;
    float drive0 = s.drive;
    memset(z, 0, sizeof *z);
    z->finite = 1;
    (void)drive0;
    while (acid_demo_cycle(&d) == c) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            if (ev[i].type == ACID_MIDI_NOTE_ON && ev[i].ch == 1) {
                z->drum[ev[i].d1]++;
            }
            acid_synth_midi(&s, &ev[i]);
        }
        acid_synth_render_lr(&s, l, r, CH);
        for (int i = 0; i < CH; i++) {
            z->finite &= isfinite(l[i]) && isfinite(r[i]);
            sl += (double)l[i] * l[i] + (double)r[i] * r[i];
            sd += (double)(l[i] - r[i]) * (l[i] - r[i]);
            ss += (double)(l[i] + r[i]) * (l[i] + r[i]);
        }
        if ((w += CH) >= WIN) {
            z->silent_windows += sqrt(sl / (2.0 * w)) < 1e-4;
            z->wide_windows += sqrt(sd) > 0.1 * sqrt(ss);
            sl = sd = ss = 0.0;
            w = 0;
        }
        const acid_slot_t *v = &s.slot[0];
        if (v->type == 0 && v->amp.value > 0.01f) {
            z->wave[v->wave & 1] = 1;
        }
        z->models[s.slot[1].type] = 1;
        z->hat_knob |= s.hat_tone != ACID_HAT_HZ;
        for (int k = 0; k < 2; k++) {
            z->cym_knob |= s.cym_tail[k] != 127 || s.cym_tune[k] != 64 || s.cym_pan[k] != 64;
        }
        z->pump |= acid_pump_active(&s.pump);
        z->djf[s.djf.mode] = 1;
        z->drive |= s.drive != 0.8f;
        z->pan |= s.pan_l[ACID_PAN_SYNTH1] != 1.0f || s.pan_r[ACID_PAN_SYNTH1] != 1.0f ||
                  s.pan_l[ACID_PAN_SYNTH2] != 1.0f || s.pan_r[ACID_PAN_SYNTH2] != 1.0f;
        if (acid_delay_active(&s.delay)) {
            z->motion[s.delay.motion] = 1;
            z->times[s.delay.time_knob] = 1;
        }
        z->sends |= (s.send1 > 0.0f) | (s.send2 > 0.0f) << 1 | (s.send_drums > 0.0f) << 2;
        z->crush |= acid_crush_active(&s.crush);
        z->stutter[s.stutter.knob] = 1;
        if (acid_tonal_active(&s.tonal)) {
            z->tonal_step |= acid_tonal_step(&s.tonal) > 0;
            z->scale_chrom |= s.tonal.scale == 0;
            z->scale_mode |= s.tonal.scale != 0;
        }
    }
}

static int count(const int *a, int n, int from)
{
    int c = 0;
    for (int i = from; i < n; i++) {
        c += a[i] != 0;
    }
    return c;
}

TEST(test_demo_smoke_loop)
{
    start();
    seen_t z;
    for (int c = 0; c < 3; c++) {
        play_cycle(&z, c);
        CHECK(z.finite);
        CHECK(z.silent_windows == 0);
        CHECK(z.wide_windows > 0);
    }
}

TEST(test_demo_coverage)
{
    start();
    seen_t z;
    for (int c = 0; c < 3; c++) {
        play_cycle(&z, c);
        CHECK(z.wave[0] && z.wave[1]);
        CHECK(count(z.models, ACID_TYPES, 1) >= 5);
        CHECK(z.drum[36] && z.drum[38] && z.drum[39] && z.drum[42] && z.drum[46]);
        CHECK(z.drum[49] && z.drum[51] && z.drum[53]);
        CHECK(z.cym_knob);
        CHECK(z.hat_knob);
        CHECK(z.pump);
        CHECK(z.djf[1] && z.djf[2]);
        CHECK(z.drive);
        CHECK(z.pan);
        CHECK(z.motion[0] && z.motion[1] && z.motion[2]);
        CHECK(count(z.times, 5, 0) >= 2);
        CHECK(z.sends == 7);
        CHECK(z.crush);
        CHECK(count(z.stutter, 5, 1) >= 2);
        CHECK(z.tonal_step && z.scale_chrom && z.scale_mode);
        printf("  cycle %d: %d models\n", c, count(z.models, ACID_TYPES, 1));
    }
}

TEST(test_demo_reset_clean)
{
    start();
    static acid_midi_event_t ev[96];
    float l[CH], r[CH];
    // Посреди круга 1 подключили BSP: сброс синта, демо молчит.
    while (acid_demo_cycle(&d) < 1 || d.part < 3) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&s, &ev[i]);
        }
        acid_synth_render_lr(&s, l, r, CH);
    }
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
    CHECK(s.slot[0].nheld == 0 && s.slot[1].nheld == 0);
    float p = 0.0f;
    for (int k = 0; k < 2 * 48000 / CH; k++) {
        acid_synth_render_lr(&s, l, r, CH);
        if (k >= 2 * 48000 / CH - 15) {
            for (int i = 0; i < CH; i++) {
                p = fmaxf(p, fmaxf(fabsf(l[i]), fabsf(r[i])));
            }
        }
    }
    CHECK(p < 1e-3f);
}

// Каждая ручка части доходит: их не больше, чем помнит плеер.
TEST(test_demo_song_fits)
{
    int ok = 1;
    for (int i = 0; i < acid_demo_song.nparts; i++) {
        ok &= acid_demo_song.parts[i].nknobs <= ACID_DEMO_KNOBS;
    }
    CHECK(ok);
}

int main(void)
{
    RUN(test_demo_song_fits);
    RUN(test_demo_smoke_loop);
    RUN(test_demo_coverage);
    RUN(test_demo_reset_clean);
    TEST_MAIN_END();
}
