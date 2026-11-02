// Барабанщик: решения Grids на шаг -> ноты по конечностям и сбивки.
#include "acid_drummer.h"

#include "pl_grids.h"

#define KNOB_MAX 127

static const uint8_t EVERY_BARS[4] = {0, 4, 8, 16}; // сбивка каждые: список ручки -> тактов
static const uint8_t FILL_BEATS[3] = {1, 2, 4};     // длина сбивки: список ручки -> долей
static const uint8_t TOMS[6] = {50, 48, 47, 45, 43, 41}; // сверху вниз по ходу сбивки

enum { N_KICK = 36, N_SNARE = 38, N_PEDAL = 44, N_CRASH = 49 };
enum { VEL_ACCENT = 120, VEL_NORMAL = 90, VEL_PEDAL = 50, VEL_JITTER = 8 };

void acid_drummer_init(acid_drummer_t *d)
{
    for (int i = 0; i < 16; i++) {
        d->knob[i] = 0;
    }
    d->knob[DRM_X] = 64;
    d->knob[DRM_Y] = 64;
    d->knob[DRM_KICK] = 80;
    d->knob[DRM_SNARE] = 70;
    d->knob[DRM_HAT] = 90;
    d->knob[DRM_FILL_EVERY] = 2; // 8 тактов
    d->perturb[0] = d->perturb[1] = d->perturb[2] = 0;
    d->rnd = 0x2545F491u;
    acid_drummer_reset(d);
}

void acid_drummer_reset(acid_drummer_t *d)
{
    d->fill_from = d->fill_to = 0;
    d->fill_pending = 0;
    d->crash_next = 0;
}

int acid_drummer_set(acid_drummer_t *d, int enc, int v)
{
    if (enc < DRM_ON || enc > DRM_FILL_NOW || v < 0 || v > KNOB_MAX) {
        return 0;
    }
    switch (enc) {
    case DRM_HAT_MODE:
        if (v > DRM_HAT_MIX) {
            return 0;
        }
        break;
    case DRM_TIME:
        if (v > DRM_TIME_RIDE) {
            return 0;
        }
        break;
    case DRM_FILL_EVERY:
        if (v >= (int)sizeof EVERY_BARS) {
            return 0;
        }
        break;
    case DRM_FILL_LEN:
        if (v >= (int)sizeof FILL_BEATS) {
            return 0;
        }
        break;
    default:
        break;
    }
    int was = d->knob[enc];
    d->knob[enc] = (uint8_t)v;
    if (enc == DRM_ON && v == 0) {
        acid_drummer_reset(d);
    }
    // «Сейчас» срабатывает на переходе 0 -> 1; если сбивка уже идёт или ждёт своего такта - игнор.
    if (enc == DRM_FILL_NOW && v != 0 && was == 0 && d->knob[DRM_ON] && d->fill_from >= d->fill_to) {
        d->fill_pending = 1;
    }
    return 1;
}

static uint32_t next_rnd(acid_drummer_t *d)
{
    d->rnd = d->rnd * 1664525u + 1013904223u;
    return d->rnd >> 16; // старшие биты LCG: младшие почти не случайны
}

static void add(acid_hit_t *out, int *n, int note, int vel)
{
    out[*n].note = (uint8_t)note;
    out[*n].vel = (uint8_t)vel;
    (*n)++;
}

// Сила удара: акцент 120, иначе 90, плюс разброс -8..+8 для живости.
static int vel_of(acid_drummer_t *d, int accent)
{
    return (accent ? VEL_ACCENT : VEL_NORMAL) + (int)(next_rnd(d) % (2 * VEL_JITTER + 1)) - VEL_JITTER;
}

static uint8_t sat_add128(uint8_t v)
{
    return v > 127 ? 255 : (uint8_t)(v + 128);
}

int acid_drummer_step(acid_drummer_t *d, int step, uint32_t bar, acid_hit_t out[ACID_DRUMMER_MAX])
{
    if (!d->knob[DRM_ON]) {
        acid_drummer_reset(d);
        return 0;
    }
    int crash = 0;
    if (step == 0) {
        // Новый такт: сбивка прошлого такта кончилась, авто-сбивка на конец такта фразы.
        d->fill_from = d->fill_to = 0;
        int every = EVERY_BARS[d->knob[DRM_FILL_EVERY]];
        if (every && (bar + 1) % (uint32_t)every == 0) {
            d->fill_from = 16 - 4 * FILL_BEATS[d->knob[DRM_FILL_LEN]];
            d->fill_to = 16;
        }
        // Хаос: возмущение на дорожку в начале каждого 32-шагового паттерна.
        if ((bar & 1) == 0) {
            int amp = pl_grids_u8(d->knob[DRM_CHAOS]) >> 2;
            for (int i = 0; i < PL_GRIDS_PARTS; i++) {
                d->perturb[i] = (uint8_t)((((int)(next_rnd(d) & 255)) * amp) >> 8);
            }
        }
        crash = d->crash_next;
        d->crash_next = 0;
    }
    // «Сейчас»: с ближайшей доли до конца такта; нажатое на последней доле уходит на шаг 0 следующего такта.
    if (d->fill_pending && step % 4 == 0) {
        d->fill_from = step;
        d->fill_to = 16;
        d->fill_pending = 0;
    }
    int in_fill = !crash && d->fill_from < d->fill_to && step >= d->fill_from && step < d->fill_to;

    uint8_t x = pl_grids_u8(d->knob[DRM_X]);
    uint8_t y = pl_grids_u8(d->knob[DRM_Y]);
    uint8_t dens[3] = {pl_grids_u8(d->knob[DRM_KICK]), pl_grids_u8(d->knob[DRM_SNARE]),
                       pl_grids_u8(d->knob[DRM_HAT])};
    int g = (int)(bar & 1) * 16 + step;
    uint8_t m = pl_grids_step(x, y, dens, d->perturb, g);
    int n = 0;

    // Правая нога: бочка по дорожке 0; после сбивки на шаге 0 - всегда, вместе с крэшем.
    if ((m & 1) || crash) {
        add(out, &n, N_KICK, vel_of(d, crash || ((m >> 3) & 1)));
    }
    if (crash) {
        add(out, &n, N_CRASH, VEL_ACCENT); // крэш бьёт рука времени
    }
    if (in_fill) {
        // Обе руки на малом и томах: дорожки малого и хэта при плотности +128.
        uint8_t fd[3] = {dens[0], sat_add128(dens[1]), sat_add128(dens[2])};
        uint8_t f = pl_grids_step(x, y, fd, d->perturb, g);
        if ((f >> 1) & 1) {
            add(out, &n, N_SNARE, vel_of(d, (f >> 4) & 1));
        }
        if ((f >> 2) & 1) {
            int idx = 6 * (step - d->fill_from) / (d->fill_to - d->fill_from);
            add(out, &n, TOMS[idx], vel_of(d, (f >> 5) & 1));
        }
        if (step == d->fill_to - 1) {
            d->crash_next = 1;
        }
    } else {
        // Вторая рука: малый по дорожке 1.
        if ((m >> 1) & 1) {
            add(out, &n, N_SNARE, vel_of(d, (m >> 4) & 1));
        }
        // Рука времени по дорожке 2, на шаге крэша молчит.
        if (((m >> 2) & 1) && !crash) {
            int accent = (m >> 5) & 1;
            int note;
            if (d->knob[DRM_TIME] == DRM_TIME_RIDE) {
                note = accent ? 53 : 51;
            } else if (d->knob[DRM_HAT_MODE] == DRM_HAT_OPEN) {
                note = 46;
            } else if (d->knob[DRM_HAT_MODE] == DRM_HAT_MIX) {
                note = accent ? 46 : 42;
            } else {
                note = 42;
            }
            add(out, &n, note, vel_of(d, accent));
        }
    }
    // Левая нога: педаль хэта на 2 и 4 долях тихо, только в режиме райд.
    if (d->knob[DRM_TIME] == DRM_TIME_RIDE && (step == 4 || step == 12)) {
        add(out, &n, N_PEDAL, VEL_PEDAL);
    }
    return n;
}
