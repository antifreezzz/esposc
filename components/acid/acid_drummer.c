// Барабанщик: решения Grids на шаг -> ноты по конечностям и сбивки.
#include "acid_drummer.h"

#define KNOB_MAX 127

static const uint8_t EVERY_BARS[4] = {0, 4, 8, 16}; // сбивка каждые: список ручки -> тактов
static const uint8_t FILL_BEATS[3] = {1, 2, 4};     // длина сбивки: список ручки -> долей
static const uint8_t TOMS[6] = {50, 48, 47, 45, 43, 41}; // сверху вниз по ходу сбивки

enum { N_KICK = 36, N_SNARE = 38, N_PEDAL = 44, N_CRASH = 49 };
enum { VEL_LEFT = 12, VEL_RUN = 6 }; // левая нога слабее; в очереди удар вне доли легче
enum { VEL_ACCENT = 120, VEL_NORMAL = 90, VEL_GHOST = 45, VEL_PEDAL = 50, VEL_JITTER = 8 };

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
    d->user = 0;
    acid_drummer_reset(d);
}

void acid_drummer_reset(acid_drummer_t *d)
{
    d->fill_from = d->fill_to = 0;
    d->fill_pending = 0;
    d->crash_next = 0;
    d->kick_next = 0;
    d->kick_run = 0;
    d->roll[0].left = d->roll[1].left = 0;
}

void acid_drummer_use_user(acid_drummer_t *d, const pl_grids_pat_t *user)
{
    d->user = user;
}

int acid_drummer_grid(const acid_drummer_t *d)
{
    return pl_grids_src_grid(d->knob[DRM_SRC], d->user);
}

int acid_drummer_set(acid_drummer_t *d, int enc, int v)
{
    if (enc < DRM_ON || enc > DRM_ROLL || v < 0 || v > KNOB_MAX) {
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
    case DRM_SRC:
        if (v >= PL_GRIDS_SRCS) {
            return 0;
        }
        break;
    case DRM_SWING:
        if (v > DRM_SWING_MAX) {
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

// Сила удара: акцент 120, гост 45, иначе 90, плюс разброс -8..+8 для живости.
static int vel_of(acid_drummer_t *d, int accent, int ghost)
{
    int base = accent ? VEL_ACCENT : ghost ? VEL_GHOST : VEL_NORMAL;
    return base + (int)(next_rnd(d) % (2 * VEL_JITTER + 1)) - VEL_JITTER;
}

// Дробь руки hand (0 малый, 1 рука времени) на шаге длиной len тиков: с шансом ROLL удар повторяется, деля шаг
// ровно - чаще на 2, в четверти случаев на fast (3, у триольной сетки 4). Доли короче 2 тиков не играются.
static void roll_arm(acid_drummer_t *d, int hand, int note, int vel, int len, int fast)
{
    int amount = d->knob[DRM_ROLL];
    if (!amount) {
        return; // и генератор не трогаем: без дроби рисунок сил ударов прежний
    }
    uint32_t r = next_rnd(d);
    if ((int)(r & 127) >= amount) {
        return;
    }
    int hits = ((r >> 7) & 3) == 0 ? fast : 2;
    while (hits > 1 && len / hits < 2) {
        hits--;
    }
    if (hits < 2) {
        return;
    }
    d->roll[hand].note = (uint8_t)note;
    d->roll[hand].vel = (uint8_t)(vel * 3 / 4); // повторы тише первого удара
    d->roll[hand].left = (uint8_t)(hits - 1);
    d->roll[hand].every = d->roll[hand].wait = (uint8_t)(len / hits);
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
    // Сетка источника: 16 шагов по 4 на долю или 12 триольных по 3.
    int src = d->knob[DRM_SRC];
    int beat = acid_drummer_grid(d) == PL_GRIDS_12T ? 3 : 4;
    int bar_steps = 4 * beat;
    int crash = 0;
    // Длина шага в тиках клока: триольный - 8, шестнадцатая - 6, свинг удлиняет чётную и укорачивает нечётную.
    int len = beat == 3 ? 8 : 6 + ((step & 1) ? -1 : 1) * d->knob[DRM_SWING];
    int fast = beat == 3 ? 4 : 3;
    d->roll[0].left = d->roll[1].left = 0; // недоигранная дробь прошлого шага обрывается
    if (step == 0) {
        // Новый такт: сбивка прошлого такта кончилась, авто-сбивка на конец такта фразы.
        d->fill_from = d->fill_to = 0;
        int every = EVERY_BARS[d->knob[DRM_FILL_EVERY]];
        if (every && (bar + 1) % (uint32_t)every == 0) {
            d->fill_from = bar_steps - beat * FILL_BEATS[d->knob[DRM_FILL_LEN]];
            d->fill_to = bar_steps;
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
    if (d->fill_pending && step % beat == 0) {
        d->fill_from = step;
        d->fill_to = bar_steps;
        d->fill_pending = 0;
    }
    // Источник сменили посреди сбивки на сетку короче: конец сбивки - конец такта этой сетки.
    int fill_to = d->fill_to < bar_steps ? d->fill_to : bar_steps;
    int in_fill = !crash && d->fill_from < fill_to && step >= d->fill_from && step < fill_to;

    uint8_t x = pl_grids_u8(d->knob[DRM_X]);
    uint8_t y = pl_grids_u8(d->knob[DRM_Y]);
    uint8_t dens[3] = {pl_grids_u8(d->knob[DRM_KICK]), pl_grids_u8(d->knob[DRM_SNARE]),
                       pl_grids_u8(d->knob[DRM_HAT])};
    int g = (int)(bar & 1) * 16 + step;
    // Биты маски: 0-2 удар дорожки, 3-5 акцент, 6-8 гост.
    unsigned m = pl_grids_src_step(src, d->user, x, y, dens, d->perturb, g);
    int n = 0;

    // Ноги: бочка по дорожке 0; после сбивки на шаге 0 - всегда, вместе с крэшем. На соседних шагах ноги
    // чередуются: левая слабее, в очереди удары вне доли легче - иначе бластбит звучит пулемётом.
    if ((m & 1) || crash) {
        int vel = vel_of(d, crash || ((m >> 3) & 1), (m >> 6) & 1);
        uint32_t at = bar * (uint32_t)bar_steps + (uint32_t)step + 1; // +1: ноль занят под «очереди нет»
        d->kick_run = d->kick_next == at ? d->kick_run + 1 : 0;
        d->kick_next = at + 1;
        if (d->kick_run & 1) {
            vel -= VEL_LEFT;
        }
        if (d->kick_run >= 2 && step % beat != 0) {
            vel -= VEL_RUN;
        }
        add(out, &n, N_KICK, vel);
    }
    if (crash) {
        add(out, &n, N_CRASH, VEL_ACCENT); // крэш бьёт рука времени
    }
    if (in_fill) {
        // Обе руки на малом и томах: дорожки малого и хэта при плотности +128.
        uint8_t fd[3] = {dens[0], sat_add128(dens[1]), sat_add128(dens[2])};
        unsigned f = pl_grids_src_step(src, d->user, x, y, fd, d->perturb, g);
        if ((f >> 1) & 1) {
            int vel = vel_of(d, (f >> 4) & 1, (f >> 7) & 1);
            add(out, &n, N_SNARE, vel);
            if (!((f >> 4) & 1)) {
                roll_arm(d, 0, N_SNARE, vel, len, fast);
            }
        }
        if ((f >> 2) & 1) {
            int idx = 6 * (step - d->fill_from) / (fill_to - d->fill_from);
            add(out, &n, TOMS[idx], vel_of(d, (f >> 5) & 1, (f >> 8) & 1));
        }
        if (step == fill_to - 1) {
            d->crash_next = 1;
        }
    } else {
        // Вторая рука: малый по дорожке 1.
        if ((m >> 1) & 1) {
            int vel = vel_of(d, (m >> 4) & 1, (m >> 7) & 1);
            add(out, &n, N_SNARE, vel);
            if (!((m >> 4) & 1)) {
                roll_arm(d, 0, N_SNARE, vel, len, fast); // акцент не дробится
            }
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
            int vel = vel_of(d, accent, (m >> 8) & 1);
            add(out, &n, note, vel);
            if (!accent && note != 46) {
                roll_arm(d, 1, note, vel, len, fast); // закрытый хэт и райд; открытый хэт дробью звучит кашей
            }
        }
    }
    // Левая нога: педаль хэта на 2 и 4 долях тихо, только в режиме райд.
    if (d->knob[DRM_TIME] == DRM_TIME_RIDE && (step == beat || step == 3 * beat)) {
        add(out, &n, N_PEDAL, VEL_PEDAL);
    }
    return n;
}

int acid_drummer_tick(acid_drummer_t *d, acid_hit_t out[ACID_DRUMMER_ROLLS])
{
    int n = 0;
    for (int i = 0; i < ACID_DRUMMER_ROLLS; i++) {
        if (d->roll[i].left && --d->roll[i].wait == 0) {
            d->roll[i].left--;
            d->roll[i].wait = d->roll[i].every;
            add(out, &n, d->roll[i].note, d->roll[i].vel);
        }
    }
    return n;
}
