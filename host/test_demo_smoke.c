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
static acid_perc_t perc_defaults;

typedef struct {
    int wave[2], models[ACID_TYPES], drum[128], hat_knob, pump, djf[3], drive, pan;
    int howl_303, drive_plaits, whistle; // перегруз синта с воем на 303, на Plaits, свист резонанса 303
    int motion[3], times[5], sends, crush, stutter[5], tonal_step, scale_chrom, scale_mode, cym_knob, perc_models[ACID_PERC_MODELS], perc_knob;
    int layer[2], layer_vol; // слои синтов звучали, их громкость крутили
    int layer_half;          // слой звучал на половинной частоте (FX 15)
    int grids, grids_fill, grids_ride; // автобарабаны играли, у них была сбивка, рука времени на райде
    int grids_bank, grids_swing;       // играл рисунок из банка классики, играли со свингом
    int petri, petri_knob, petri_freeze; // PETRI звучал, ручки «Чашки» крутили, фриз включали
    double petri_rms, other_rms;          // самое громкое окно с PETRI и без него
    long silent_windows, wide_windows;
    int finite;
} seen_t;

// Обе песни демо прошивки: охват фич одинаковый (акид заморожен, но охват не должен тихо потеряться).
static const acid_demo_song_t *const SONGS[] = {&acid_metal_song, &acid_demo_song};
#define NSONGS (int)(sizeof SONGS / sizeof SONGS[0])

static void start(const acid_demo_song_t *song)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
    acid_demo_init(&d, song);
    acid_perc_init(&perc_defaults, SR);
}

// Играет до конца круга c, собирает что увидено.
static void play_cycle(seen_t *z, int c)
{
    static acid_midi_event_t ev[96];
    float l[CH], r[CH];
    double sl = 0.0, sd = 0.0, ss = 0.0;
    int w = 0;
    float drive0 = s.drive;
    uint32_t hits0 = s.grids_hits;
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
        int pet = 0;
        for (int k = 0; k < 2; k++) {
            pet |= s.slot[k].type == ACID_TYPE_PETRI && acid_petri_active(&s.petri[k]);
            z->petri_freeze |= s.petri[k].m > 0.5f;
        }
        z->petri |= pet;
        z->petri_knob |= s.raw[ACID_PAGE_PETRI][5] != 3 || s.raw[ACID_PAGE_PETRI][8] != 1; // пожиратели, вихри
        if ((w += CH) >= WIN) {
            double wr = sqrt(sl / (2.0 * w));
            double *top = pet ? &z->petri_rms : &z->other_rms;
            *top = wr > *top ? wr : *top;
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
        if (v->type == 0 && v->amp.value > 0.01f) {
            z->howl_303 |= acid_drive_on(&v->drv) && v->drv.howl > 64;
            z->whistle |= v->res > 1.0f;
        }
        for (int k = 0; k < 2; k++) {
            z->drive_plaits |= s.slot[k].type != 0 && acid_drive_on(&s.slot[k].drv);
        }
        for (int k = 0; k < 2; k++) {
            const acid_slot_t *lv = &s.layer[k];
            z->layer[k] |= lv->type != ACID_LAYER_OFF && lv->nheld > 0;
            z->layer_vol |= s.layer_gain[k] != 100.0f / 127.0f;
            z->layer_half |= s.layer_half && lv->half && lv->nheld > 0;
        }
        z->hat_knob |= s.hat_tone != ACID_HAT_HZ;
        z->grids |= s.grids_hits != hits0;
        z->grids_fill |= s.drummer.knob[DRM_ON] && s.drummer.fill_from < s.drummer.fill_to;
        z->grids_ride |= s.grids_hits != hits0 && s.drummer.knob[DRM_TIME] == DRM_TIME_RIDE;
        z->grids_bank |= s.grids_hits != hits0 && s.drummer.knob[DRM_SRC] >= 1 && s.drummer.knob[DRM_SRC] <= PL_GRIDS_BANK;
        z->grids_swing |= s.grids_hits != hits0 && s.drummer.knob[DRM_SWING] > 0;
        hits0 = s.grids_hits;
        for (int k = 0; k < ACID_PERC_SLOTS; k++) {
            const acid_perc_voice_t *pv = &s.perc.v[k];
            if (pv->active && pv->fade_step == 0.0f) {
                z->perc_models[s.perc.inst[pv->inst].model] = 1;
            }
        }
        z->perc_knob |= memcmp(s.perc.inst, perc_defaults.inst, sizeof perc_defaults.inst) != 0;
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
    for (int g = 0; g < NSONGS; g++) {
        start(SONGS[g]);
        seen_t z;
        for (int c = 0; c < 3; c++) {
            play_cycle(&z, c);
            CHECK(z.finite);
            CHECK(z.silent_windows == 0);
            CHECK(z.wide_windows > 0);
        }
    }
}

static void coverage(const acid_demo_song_t *song)
{
    start(song);
    seen_t z;
    for (int c = 0; c < 3; c++) {
        play_cycle(&z, c);
        CHECK(z.wave[0] && z.wave[1]);
        CHECK(count(z.models, ACID_TYPES, 1) >= 5);
        CHECK(z.drum[36] && z.drum[38] && z.drum[39] && z.drum[42] && z.drum[46]);
        CHECK(z.drum[49] && z.drum[51] && z.drum[53]);
        CHECK(z.cym_knob);
        CHECK(z.drum[37] && z.drum[41] && z.drum[43] && z.drum[45] && z.drum[47] && z.drum[48] && z.drum[50] && z.drum[56]);
        CHECK(count(z.perc_models, ACID_PERC_MODELS, 0) == ACID_PERC_MODELS);
        CHECK(z.perc_knob);
        CHECK(z.hat_knob);
        CHECK(z.pump);
        CHECK(z.djf[1] && z.djf[2]);
        CHECK(z.drive);
        CHECK(z.howl_303 && z.drive_plaits && z.whistle);
        CHECK(z.pan);
        CHECK(z.motion[0] && z.motion[1] && z.motion[2]);
        CHECK(count(z.times, 5, 0) >= 2);
        CHECK(z.sends == 7);
        CHECK(z.crush);
        CHECK(count(z.stutter, 5, 1) >= 2);
        CHECK(z.tonal_step && z.scale_chrom && z.scale_mode);
        CHECK(z.layer[0] && z.layer[1] && z.layer_vol && z.layer_half);
        CHECK(z.grids && z.grids_fill);
        CHECK(z.grids_bank && z.grids_swing);
        CHECK(z.petri);
        CHECK(z.petri_knob);
        CHECK(z.petri_freeze);
        CHECK(z.petri_rms <= z.other_rms); // чашка не громче остального трека
        printf("  cycle %d: petri rms %.3f, loudest other %.3f\n", c, z.petri_rms, z.other_rms);
        CHECK(song != &acid_metal_song || z.grids_ride); // райд - в метале, в акиде хэт вперемешку
        printf("  cycle %d: %d models\n", c, count(z.models, ACID_TYPES, 1));
    }
}

TEST(test_demo_coverage)
{
    for (int g = 0; g < NSONGS; g++) {
        coverage(SONGS[g]);
    }
}

TEST(test_demo_reset_clean)
{
    start(&acid_demo_song);
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
    for (int g = 0; g < NSONGS; g++) {
        for (int i = 0; i < SONGS[g]->nparts; i++) {
            ok &= SONGS[g]->parts[i].nknobs <= ACID_DEMO_KNOBS;
        }
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
