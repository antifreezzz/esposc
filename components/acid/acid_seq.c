#include "acid_seq.h"

void acid_seq_init(acid_seq_t *s, const acid_step_t *steps, int len, float bpm, float sr)
{
    s->steps = steps;
    s->len = len;
    s->samples_per_step = (double)sr * 60.0 / (double)bpm / 4.0;
    s->pos = 0.0;
    s->index = -1;
}

int acid_seq_tick(acid_seq_t *s)
{
    if (s->index < 0) {
        s->index = 0;
        s->pos = 0.0;
        return 0;
    }
    s->pos += 1.0;
    if (s->pos >= s->samples_per_step) {
        s->pos -= s->samples_per_step;
        s->index = (s->index + 1) % s->len;
        return s->index;
    }
    return -1;
}

const acid_step_t *acid_seq_next_step(const acid_seq_t *s)
{
    int i = s->index < 0 ? 0 : (s->index + 1) % s->len;
    return &s->steps[i];
}

int acid_seq_gate(const acid_seq_t *s)
{
    if (s->index < 0) {
        return 0;
    }
    const acid_step_t *cur = &s->steps[s->index];
    if (cur->flags & ACID_REST) {
        return 0;
    }
    if ((cur->flags & ACID_SLIDE) && !(acid_seq_next_step(s)->flags & ACID_REST)) {
        return 1;
    }
    return s->pos < 0.5 * s->samples_per_step;
}
