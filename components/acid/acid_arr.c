#include "acid_arr.h"

#include "acid_noise.h"

#define A ACID_ACCENT
#define S ACID_SLIDE
#define R ACID_REST

// B: вариация вокруг ля, больше пауз и слайдов.
static const acid_step_t pat_b[16] = {
    {45, A}, {0, R},  {45, 0}, {57, S},
    {55, 0}, {45, 0}, {0, R},  {48, A},
    {45, S}, {43, 0}, {45, 0}, {52, A | S},
    {50, 0}, {0, R},  {45, 0}, {47, 0},
};

// C: октавой выше и плотнее, для пика.
static const acid_step_t pat_c[16] = {
    {57, A}, {57, 0}, {60, S}, {64, 0},
    {57, 0}, {55, A}, {57, 0}, {69, S},
    {57, 0}, {60, 0}, {62, A}, {60, S},
    {57, 0}, {55, 0}, {57, A}, {59, S},
};

// F: филл, разбег вверх и обратно.
static const acid_step_t pat_f[16] = {
    {45, A}, {48, 0}, {52, 0}, {55, 0},
    {57, A}, {60, 0}, {64, 0}, {67, 0},
    {69, A | S}, {67, S}, {64, S}, {60, 0},
    {57, A}, {0, R},  {45, A | S}, {57, 0},
};

const acid_step_t *const acid_patterns[ACID_PAT_COUNT] = {
    acid_pattern_default, pat_b, pat_c, pat_f,
};

#define PA ACID_PAT_A
#define PB ACID_PAT_B
#define PC ACID_PAT_C
#define PF ACID_PAT_F
#define K ACID_DR_KICK
#define H ACID_DR_HAT
#define O ACID_DR_OHAT
#define C ACID_DR_CLAP

const acid_section_t acid_track[ACID_SECTIONS] = {
    // pat fill tr wave drums       cut_from cut_to res_from res_to env
    {PA, PA, 0, 0, K | H,           120,  250,  0.60f, 0.70f, 0.4f}, // 1 интро
    {PA, PF, 0, 0, K | H,           250,  450,  0.70f, 0.75f, 0.6f}, // 2
    {PA, PA, 0, 0, K | H | C,       450,  700,  0.75f, 0.80f, 0.8f}, // 3 разгон
    {PB, PF, 0, 0, K | H | C,       700,  900,  0.80f, 0.80f, 1.0f}, // 4
    {PA, PA, 0, 0, K | H | C,       600,  900,  0.85f, 0.85f, 1.0f}, // 5 грув, ля
    {PA, PA, 5, 0, K | H | C,       700,  1000, 0.85f, 0.85f, 1.0f}, // 6 ре
    {PB, PB, 0, 0, K | H | O | C,   800,  1100, 0.85f, 0.85f, 1.0f}, // 7 ля
    {PB, PF, 7, 0, K | H | O | C,   900,  1300, 0.85f, 0.88f, 1.0f}, // 8 ми
    {PC, PC, 0, 1, K | H | O | C,   1200, 2000, 0.90f, 0.90f, 1.2f}, // 9 пик
    {PC, PF, 0, 1, K | H | O | C,   2000, 2500, 0.90f, 0.92f, 1.2f}, // 10
    {PA, PA, 0, 0, H,               1500, 400,  0.92f, 0.97f, 0.8f}, // 11 брейк
    {PA, PF, 0, 0, H,               400,  150,  0.97f, 0.97f, 0.5f}, // 12
    {PA, PA, 0, 0, K | H | O | C,   1000, 1400, 0.85f, 0.85f, 1.1f}, // 13 дроп
    {PB, PB, 5, 0, K | H | O | C,   1100, 1500, 0.85f, 0.85f, 1.1f}, // 14
    {PB, PB, 0, 0, K | H | C,       1200, 800,  0.85f, 0.80f, 1.0f}, // 15 аутро
    {PA, PF, 0, 0, K | H,           600,  150,  0.80f, 0.65f, 0.6f}, // 16 к интро
};

const acid_section_t *acid_arr_section(int bar)
{
    int b = bar % ACID_TRACK_BARS;
    return &acid_track[b / ACID_SECTION_BARS];
}

int acid_arr_pattern(int bar)
{
    const acid_section_t *sec = acid_arr_section(bar);
    return bar % ACID_SECTION_BARS == ACID_SECTION_BARS - 1 ? sec->fill_pattern : sec->pattern;
}

float acid_arr_progress(int bar, int step)
{
    int pos = (bar % ACID_SECTION_BARS) * 16 + step;
    return (float)pos / (float)(ACID_SECTION_BARS * 16);
}

static int count_accents(const acid_step_t *p, int len)
{
    int n = 0;
    for (int i = 0; i < len; i++) {
        n += !!(p[i].flags & ACID_ACCENT);
    }
    return n;
}

// Случайный индекс ноты (не паузы) с нужным состоянием флага; -1 если такой нет.
static int pick(const acid_step_t *p, int len, uint32_t *rng, uint8_t flag, int want_set)
{
    int start = (int)(acid_rand(rng) % (uint32_t)len);
    for (int k = 0; k < len; k++) {
        int i = (start + k) % len;
        if (p[i].flags & ACID_REST) {
            continue;
        }
        if (want_set < 0 || !!(p[i].flags & flag) == want_set) {
            return i;
        }
    }
    return -1;
}

void acid_mutate(acid_step_t *dst, const acid_step_t *src, int len, uint32_t seed)
{
    uint32_t rng = seed * 2654435761u + 0x6d2b79f5u;
    if (rng == 0) {
        rng = 1;
    }
    for (int i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    int flips = 2 + (int)(acid_rand(&rng) % 2);
    for (int f = 0; f < flips; f++) {
        uint8_t flag = (acid_rand(&rng) & 1) ? ACID_ACCENT : ACID_SLIDE;
        int i = pick(dst, len, &rng, flag, -1);
        if (i >= 0) {
            dst[i].flags ^= flag;
        }
    }
    // Держим акценты в [2, 6].
    while (count_accents(dst, len) < 2) {
        int i = pick(dst, len, &rng, ACID_ACCENT, 0);
        if (i < 0) {
            break;
        }
        dst[i].flags |= ACID_ACCENT;
    }
    while (count_accents(dst, len) > 6) {
        int i = pick(dst, len, &rng, ACID_ACCENT, 1);
        dst[i].flags &= (uint8_t)~ACID_ACCENT;
    }
    // Если флипы взаимно погасились, гарантируем хотя бы одно отличие.
    int same = 1;
    for (int i = 0; i < len; i++) {
        same &= dst[i].flags == src[i].flags;
    }
    if (same) {
        int i = pick(dst, len, &rng, ACID_SLIDE, -1);
        if (i >= 0) {
            dst[i].flags ^= ACID_SLIDE;
        }
    }
}
