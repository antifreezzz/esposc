#include "acid_demo.h"

#include <string.h>

#define CH_DRUMS 1 // канал 2: ударка, туда же CC ручек (как в тестах live)
#define CH_SYN1 4  // канал 5 (SEQ1)
#define CH_SYN2 5  // канал 6 (SEQ2)
#define CC_PAGE 10
#define PAGE_SYN2 4
#define ENC_TYPE 2
#define NO_VALUE 0xFF

static const uint8_t DRUM_NOTES[5] = {36, 38, 39, 42, 46}; // по битам KICK..OHAT

static int32_t part_len(const acid_demo_part_t *p)
{
    return (int32_t)p->bars * 16 * ACID_DEMO_STEP;
}

// Вариации круга: детерминированы его номером.
static void cycle_seed(acid_demo_t *d)
{
    d->rnd = (uint32_t)d->cycle * 2654435761u + 0x6d2b79f5u;
    d->rnd = d->rnd ? d->rnd : 1;
}

static uint32_t rnd_next(acid_demo_t *d)
{
    d->rnd ^= d->rnd << 13;
    d->rnd ^= d->rnd >> 17;
    d->rnd ^= d->rnd << 5;
    return d->rnd;
}

static int clamp127(int v)
{
    return v < 0 ? 0 : (v > 127 ? 127 : v);
}

// Перевернуть акцент или слайд в 1-3 нотах (ноты и шаги прежние).
static void vary_syn1(acid_demo_t *d)
{
    int flips = 1 + (int)(rnd_next(d) % 3);
    for (int f = 0; f < flips; f++) {
        int st = (int)(rnd_next(d) % 16);
        for (int k = 0; k < 16 && !d->syn1[st].len; k++) {
            st = (st + 1) % 16; // ближайшая нота
        }
        acid_demo_note_t *n = &d->syn1[st];
        if (!n->len) {
            return;
        }
        if ((rnd_next(d) & 1) && d->syn1[(st + 1) % 16].len) {
            n->len = n->len == 1 ? 2 : 1; // слайд в следующую ноту или без него
        } else {
            n->vel = n->vel > 100 ? 90 : 120; // акцент - velocity выше 100
        }
    }
}

static void part_enter(acid_demo_t *d)
{
    const acid_demo_part_t *p = &d->song->parts[d->part];
    d->part_len = part_len(p);
    d->k = 0;
    memset(d->last, NO_VALUE, sizeof d->last);
    for (int i = 0; i < p->nknobs && i < ACID_DEMO_KNOBS; i++) {
        const acid_demo_knob_t *kn = &p->knobs[i];
        int a = kn->from, b = kn->to;
        if (d->cycle && kn->vary == ACID_DEMO_PICK) {
            int lo = a < b ? a : b, hi = a < b ? b : a; // круг 0 - from, рамки в любом порядке
            a = b = lo + (int)(rnd_next(d) % (uint32_t)(hi - lo + 1));
        } else if (kn->vary == ACID_DEMO_PICK) {
            b = a;
        } else if (d->cycle && kn->vary) {
            a = clamp127(a + (int)(rnd_next(d) % (2u * kn->vary + 1)) - kn->vary);
            b = clamp127(b + (int)(rnd_next(d) % (2u * kn->vary + 1)) - kn->vary);
        }
        d->from[i] = (uint8_t)a;
        d->to[i] = (uint8_t)b;
    }
    if (p->syn1) {
        memcpy(d->syn1, p->syn1, sizeof d->syn1);
        if (d->cycle && p->vary_syn1) {
            vary_syn1(d);
        }
    }
}

void acid_demo_init(acid_demo_t *d, const acid_demo_song_t *song)
{
    d->song = song;
    d->part = 0;
    d->cycle = 0;
    d->t = 0;
    d->page = -1;
    for (int i = 0; i < ACID_DEMO_HELD; i++) {
        d->held[i].off = -1;
    }
    cycle_seed(d);
    part_enter(d);
}

int acid_demo_cycle(const acid_demo_t *d)
{
    return d->cycle;
}

typedef struct {
    acid_midi_event_t *out;
    int n, cap;
} sink_t;

static void put(sink_t *o, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    if (o->n < o->cap) {
        o->out[o->n++] = (acid_midi_event_t){.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    }
}

// Ручка как с BSP: страница (если другая), затем CC энкодера.
static void knob(acid_demo_t *d, sink_t *o, int page, int enc, int v)
{
    if (d->page != page) {
        put(o, ACID_MIDI_CC, CH_DRUMS, CC_PAGE, (uint8_t)page);
        d->page = page;
    }
    put(o, ACID_MIDI_CC, CH_DRUMS, acid_bsp_enc_cc[enc - 1], (uint8_t)v);
}

// Значение плавной ручки на куске k из len: from в начале, to в конце, округление к ближайшему.
static int ramp(int from, int to, int32_t k, int32_t len)
{
    if (from == to || len <= 1) {
        return from;
    }
    int32_t num = (int32_t)(to - from) * k, den = len - 1;
    return from + (int)(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

static void note_on(acid_demo_t *d, sink_t *o, uint8_t ch, uint8_t note, uint8_t vel, int32_t off)
{
    for (int i = 0; i < ACID_DEMO_HELD; i++) {
        if (d->held[i].off < 0) {
            d->held[i].ch = ch;
            d->held[i].note = note;
            d->held[i].off = off;
            put(o, ACID_MIDI_NOTE_ON, ch, note, vel);
            return;
        }
    }
}

static void synth_step(acid_demo_t *d, sink_t *o, const acid_demo_note_t *pat, uint8_t ch, int step)
{
    if (pat && pat[step].len) {
        note_on(d, o, ch, pat[step].note, pat[step].vel, d->t + (int32_t)pat[step].len * ACID_DEMO_STEP);
    }
}

int acid_demo_events(acid_demo_t *d, acid_midi_event_t *out, int cap)
{
    sink_t o = {out, 0, cap};
    const acid_demo_part_t *p = &d->song->parts[d->part];
    if (d->part == 0 && d->k == 0) {
        put(&o, ACID_MIDI_START, 0, 0, 0);
        d->page = -1; // после круга страница могла уйти: шлём заново
    }

    // Ручки части. Модель синта 2 - в начале части, из пула.
    if (d->k == 0 && p->npool) {
        uint8_t m = d->cycle ? p->syn2_pool[rnd_next(d) % p->npool] : p->syn2_pool[0];
        knob(d, &o, PAGE_SYN2, ENC_TYPE, m);
    }
    for (int i = 0; i < p->nknobs && i < ACID_DEMO_KNOBS; i++) {
        int v = ramp(d->from[i], d->to[i], d->k, d->part_len);
        if (v != d->last[i]) {
            knob(d, &o, p->knobs[i].page, p->knobs[i].enc, v);
            d->last[i] = (uint8_t)v;
        }
    }

    // Отпускания, на последнем куске круга - всё звучащее (на стыке ничего не висит).
    int last = d->part == d->song->nparts - 1 && d->k == d->part_len - 1;
    for (int i = 0; i < ACID_DEMO_HELD; i++) {
        if (d->held[i].off >= 0 && (d->held[i].off == d->t || last)) {
            put(&o, ACID_MIDI_NOTE_OFF, d->held[i].ch, d->held[i].note, 0);
            d->held[i].off = -1;
        }
    }

    if (d->k % ACID_DEMO_STEP == 0 && !last) {
        int step = (int)(d->k / ACID_DEMO_STEP) % 16;
        synth_step(d, &o, p->syn1 ? d->syn1 : NULL, CH_SYN1, step);
        synth_step(d, &o, p->syn2, CH_SYN2, step);
        uint8_t bits = p->drums ? p->drums[step] : 0;
        for (int b = 0; b < 5; b++) {
            if (bits & (1 << b)) {
                note_on(d, &o, CH_DRUMS, DRUM_NOTES[b], bits & ACID_DEMO_ACC ? 127 : 96, d->t + ACID_DEMO_STEP / 2);
            }
        }
    }
    if (d->t % ACID_DEMO_TICK == 0) {
        put(&o, ACID_MIDI_CLOCK, 0, 0, 0);
    }

    d->t++;
    if (++d->k == d->part_len) {
        if (++d->part == d->song->nparts) {
            d->part = 0;
            d->t = 0;
            d->cycle++;
            cycle_seed(d);
        }
        part_enter(d);
    }
    return o.n;
}
