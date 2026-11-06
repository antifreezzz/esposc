#include "acid_seq.h"

#include <string.h>

#define TICKS 6     // тиков на шаг (шестнадцатая при 24 PPQN)
#define GATE_TICK 3 // без слайда нота гаснет на середине шага
#define VEL 90
#define VEL_ACC 110 // выше ACCENT_VEL синта
#define BPM_DEFAULT 120

void acid_seq_init(acid_seq_t *q)
{
    memset(q, 0, sizeof *q);
    for (int s = 0; s < ACID_SEQ_SYNTHS; s++) {
        acid_seq_track_t *t = &q->t[s];
        memset(t->pat, ACID_SEQ_REST, sizeof t->pat);
        memset(t->cell, ACID_SEQ_EMPTY, sizeof t->cell);
        memset(t->rep, 1, sizeof t->rep);
        t->sounding = -1;
    }
    q->bpm = BPM_DEFAULT;
}

static int in(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

void acid_seq_set_step(acid_seq_t *q, int synth, int pat, int step, uint8_t v)
{
    if (in(synth, 0, ACID_SEQ_SYNTHS - 1) && in(pat, 0, ACID_SEQ_PATS - 1) && in(step, 0, ACID_SEQ_STEPS - 1)) {
        q->t[synth].pat[pat][step] = v & (ACID_SEQ_DEG | ACID_SEQ_ACC | ACID_SEQ_SLIDE);
    }
}

void acid_seq_set_oct(acid_seq_t *q, int synth, int pat, int oct)
{
    if (in(synth, 0, ACID_SEQ_SYNTHS - 1) && in(pat, 0, ACID_SEQ_PATS - 1) &&
        in(oct, -ACID_SEQ_OCT_MAX, ACID_SEQ_OCT_MAX)) {
        q->t[synth].oct[pat] = (int8_t)oct;
    }
}

void acid_seq_set_row(acid_seq_t *q, int synth, int row, const uint8_t cells[ACID_SEQ_CELLS], int rep)
{
    if (!in(synth, 0, ACID_SEQ_SYNTHS - 1) || !in(row, 0, ACID_SEQ_ROWS - 1) || !in(rep, 1, ACID_SEQ_REP_MAX)) {
        return;
    }
    acid_seq_track_t *t = &q->t[synth];
    for (int i = 0; i < ACID_SEQ_CELLS; i++) {
        t->cell[row][i] = cells[i] < ACID_SEQ_PATS ? cells[i] : ACID_SEQ_EMPTY;
    }
    t->rep[row] = (uint8_t)rep;
}

void acid_seq_set_edit(acid_seq_t *q, int synth, int pat)
{
    if (in(synth, 0, ACID_SEQ_SYNTHS - 1) && in(pat, 0, ACID_SEQ_PATS - 1)) {
        q->t[synth].edit = (uint8_t)pat;
    }
}

void acid_seq_set_bpm(acid_seq_t *q, int bpm)
{
    if (in(bpm, ACID_SEQ_BPM_MIN, ACID_SEQ_BPM_MAX)) {
        q->bpm = (uint8_t)bpm;
    }
}

// Первая непустая ячейка строки не раньше from, иначе -1.
static int cell_from(const acid_seq_track_t *t, int row, int from)
{
    for (int c = from; c < ACID_SEQ_CELLS; c++) {
        if (t->cell[row][c] != ACID_SEQ_EMPTY) {
            return c;
        }
    }
    return -1;
}

// Первая непустая строка начиная с from (по кругу), -1 - песня пустая.
static int row_from(const acid_seq_track_t *t, int from)
{
    for (int i = 0; i < ACID_SEQ_ROWS; i++) {
        int r = (from + i) % ACID_SEQ_ROWS;
        if (cell_from(t, r, 0) >= 0) {
            return r;
        }
    }
    return -1;
}

// Такт на позиции; позиция уже указывает на непустую ячейку или песня пуста.
static void latch(acid_seq_track_t *t)
{
    int r = row_from(t, t->row);
    if (r < 0) {
        t->cur_pat = t->edit;
        return;
    }
    if (r != t->row) {
        t->row = (uint8_t)r, t->n = 0, t->c = 0;
    }
    int c = cell_from(t, t->row, t->c);
    if (c < 0) {
        c = cell_from(t, t->row, 0);
    }
    t->c = (uint8_t)c;
    t->cur_pat = t->cell[t->row][t->c];
}

static void song_start(acid_seq_track_t *t)
{
    t->row = t->n = t->c = 0;
    t->step = t->tick = 0;
    latch(t);
}

// Следующий такт песни: ячейка, проход строки, строка по кругу. Правки посреди хода сюда приходят
// с любыми ячейками и повтором: индексы остаются в границах.
static void song_next(acid_seq_track_t *t)
{
    int c = cell_from(t, t->row, t->c + 1);
    if (c >= 0) {
        t->c = (uint8_t)c;
    } else if (++t->n < t->rep[t->row]) {
        t->c = 0;
    } else {
        t->n = 0, t->c = 0;
        t->row = (uint8_t)((t->row + 1) % ACID_SEQ_ROWS);
    }
    latch(t);
}

static int emit(acid_seq_ev_t *out, int k, int synth, int on, int note, int vel)
{
    out[k].synth = (uint8_t)synth;
    out[k].on = (uint8_t)on;
    out[k].note = (uint8_t)note;
    out[k].vel = (uint8_t)vel;
    return k + 1;
}

int acid_seq_run(acid_seq_t *q, int synth, int on, int beat_running, acid_seq_ev_t out[2])
{
    if (!in(synth, 0, ACID_SEQ_SYNTHS - 1)) {
        return 0;
    }
    acid_seq_track_t *t = &q->t[synth];
    int k = 0;
    if (t->sounding >= 0) {
        k = emit(out, k, synth, 0, t->sounding, 0);
        t->sounding = -1;
    }
    t->slid = 0;
    t->run = on ? 1 : 0;
    if (on) {
        song_start(t);
        t->armed = beat_running ? 1 : 0;
    }
    return k;
}

void acid_seq_rewind(acid_seq_t *q)
{
    for (int s = 0; s < ACID_SEQ_SYNTHS; s++) {
        song_start(&q->t[s]);
        q->t[s].slid = 0;
        q->t[s].armed = 0;
    }
}

static int track_clock(acid_seq_track_t *t, int synth, int beat_step, const acid_tonal_t *tonal, acid_seq_ev_t *out)
{
    if (!t->run) {
        return 0;
    }
    if (t->armed) {
        if (beat_step != 0) {
            return 0;
        }
        t->armed = 0;
    }
    int k = 0;
    if (t->tick == 0 && t->step == 0) {
        latch(t); // песню или такт экрана правили посреди хода
    }
    uint8_t v = t->pat[t->cur_pat][t->step];
    if (t->tick == 0) {
        int deg = v & ACID_SEQ_DEG;
        int old = t->sounding;
        if (old >= 0 && !t->slid) {
            k = emit(out, k, synth, 0, old, 0); // гейт не успел (rewind): гасим до новой, без глайда
            old = t->sounding = -1;
        }
        if (deg == ACID_SEQ_REST) {
            if (old >= 0) {
                k = emit(out, k, synth, 0, old, 0);
                t->sounding = -1;
            }
        } else {
            int base = (synth == 0 ? 36 : 48) + tonal->tonic + 12 * t->oct[t->cur_pat];
            int note = acid_tonal_degree_note(tonal, base, deg);
            note = note < 0 ? 0 : (note > 127 ? 127 : note);
            if (note != old) {
                k = emit(out, k, synth, 1, note, (v & ACID_SEQ_ACC) ? VEL_ACC : VEL); // новая раньше старой: глайд
                if (old >= 0) {
                    k = emit(out, k, synth, 0, old, 0);
                }
                t->sounding = (int8_t)note;
            }
        }
        t->slid = (v & ACID_SEQ_SLIDE) && t->sounding >= 0;
    } else if (t->tick == GATE_TICK && t->sounding >= 0 && !t->slid) {
        k = emit(out, k, synth, 0, t->sounding, 0);
        t->sounding = -1;
    }
    if (++t->tick == TICKS) {
        t->tick = 0;
        if (++t->step == ACID_SEQ_STEPS) {
            t->step = 0;
            song_next(t);
        }
    }
    return k;
}

int acid_seq_clock(acid_seq_t *q, int beat_step, const acid_tonal_t *tonal, acid_seq_ev_t out[4])
{
    int k = 0;
    for (int s = 0; s < ACID_SEQ_SYNTHS; s++) {
        k += track_clock(&q->t[s], s, beat_step, tonal, out + k);
    }
    return k;
}
