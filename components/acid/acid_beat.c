#include "acid_beat.h"

void acid_beat_init(acid_beat_t *b)
{
    acid_beat_start(b);
}

void acid_beat_start(acid_beat_t *b)
{
    *b = (acid_beat_t){0};
}

void acid_beat_continue(acid_beat_t *b)
{
    b->stopped = 0;
}

void acid_beat_stop(acid_beat_t *b)
{
    b->stopped = 1;
}

int acid_beat_tick(acid_beat_t *b)
{
    if (b->stopped) {
        return 0;
    }
    if (b->tick == ACID_BEAT_TICKS) {
        b->tick = 0;
        if (++b->step == ACID_BEAT_STEPS) {
            b->step = 0;
            b->bar++;
        }
    }
    return b->tick++ == 0;
}

int acid_beat_grid_due(const acid_beat_t *b, int grid, int swing)
{
    if (b->stopped || b->tick == 0) {
        return -1;
    }
    int t = b->step * ACID_BEAT_TICKS + b->tick - 1; // тик в такте: acid_beat_tick уже шагнул дальше
    if (grid) {
        return t % 8 == 0 ? t / 8 : -1;
    }
    return b->tick - 1 == ((b->step & 1) ? swing : 0) ? b->step : -1;
}
