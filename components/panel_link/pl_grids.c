// Генератор барабанов Grids: чтение карты и решения по шагам.
// Порт из https://github.com/pichenettes/eurorack, grids/pattern_generator.cc
// (ReadDrumMap, EvaluateDrums) и avrlib U8Mix.
//
// Copyright 2011 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include "pl_grids.h"

// U8Mix из avrlib: (a * (255 - w) + b * w) >> 8. Не /255: так в оригинале, потери до 2 единиц у углов.
static uint8_t u8_mix(uint8_t a, uint8_t b, uint8_t w)
{
    return (uint8_t)((a * (255 - w) + b * w) >> 8);
}

uint8_t pl_grids_u8(int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 127) {
        v = 127;
    }
    return (uint8_t)((v << 1) | (v >> 6));
}

uint8_t pl_grids_level(uint8_t x, uint8_t y, int part, int step)
{
    int i = x >> 6;
    int j = y >> 6;
    int offset = part * PL_GRIDS_STEPS + step;
    uint8_t a = pl_grids_drum_map[i][j][offset];
    uint8_t b = pl_grids_drum_map[i + 1][j][offset];
    uint8_t c = pl_grids_drum_map[i][j + 1][offset];
    uint8_t d = pl_grids_drum_map[i + 1][j + 1][offset];
    uint8_t wx = (uint8_t)(x << 2);
    uint8_t wy = (uint8_t)(y << 2);
    return u8_mix(u8_mix(a, b, wx), u8_mix(c, d, wx), wy);
}

uint8_t pl_grids_step(uint8_t x, uint8_t y, const uint8_t density[3], const uint8_t perturb[3], int step)
{
    uint8_t state = 0;
    for (int p = 0; p < PL_GRIDS_PARTS; p++) {
        uint8_t level = pl_grids_level(x, y, p, step);
        if (level < 255 - perturb[p]) {
            level = (uint8_t)(level + perturb[p]);
        } else {
            level = 255;
        }
        uint8_t threshold = (uint8_t)~density[p];
        if (level > threshold) {
            state |= (uint8_t)(1 << p);
            if (level > PL_GRIDS_ACCENT) {
                state |= (uint8_t)(8 << p);
            }
        }
    }
    return state;
}

void pl_grids_preview(const uint8_t knob[16], uint8_t out[PL_GRIDS_STEPS])
{
    const uint8_t density[3] = {pl_grids_u8(knob[4]), pl_grids_u8(knob[5]), pl_grids_u8(knob[6])};
    const uint8_t none[3] = {0, 0, 0};
    uint8_t x = pl_grids_u8(knob[2]);
    uint8_t y = pl_grids_u8(knob[3]);
    for (int s = 0; s < PL_GRIDS_STEPS; s++) {
        out[s] = pl_grids_step(x, y, density, none, s);
    }
}

static uint8_t bank_level(const pl_grids_bank_t *b, int part, int step)
{
    int per_bar = b->grid == PL_GRIDS_12T ? 12 : 16;
    int in = step % 16;
    if (in >= per_bar) {
        return 0;
    }
    switch (b->part[part][(step / 16) * per_bar + in]) {
    case 'X': return PL_GRIDS_LV_ACC;
    case 'x': return PL_GRIDS_LV_HIT;
    case 'g': return PL_GRIDS_LV_GHOST;
    case ',': return PL_GRIDS_LV_EXTRA;
    default: return 0;
    }
}

static const pl_grids_pat_t *user_pat(int src, const pl_grids_pat_t *user)
{
    return user && src >= PL_GRIDS_SRC_USER && src < PL_GRIDS_SRCS ? &user[src - PL_GRIDS_SRC_USER] : 0;
}

int pl_grids_src_grid(int src, const pl_grids_pat_t *user)
{
    if (src >= 1 && src < PL_GRIDS_SRC_USER) {
        return pl_grids_bank[src - 1].grid;
    }
    const pl_grids_pat_t *u = user_pat(src, user);
    return u && u->grid == PL_GRIDS_12T ? PL_GRIDS_12T : PL_GRIDS_16;
}

uint8_t pl_grids_src_level(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, int part, int step)
{
    if (src == PL_GRIDS_SRC_MAP) {
        return pl_grids_level(x, y, part, step);
    }
    if (src >= 1 && src < PL_GRIDS_SRC_USER) {
        return bank_level(&pl_grids_bank[src - 1], part, step);
    }
    const pl_grids_pat_t *u = user_pat(src, user);
    if (!u || (u->grid == PL_GRIDS_12T && step % 16 >= 12)) {
        return 0;
    }
    return u->lv[part * PL_GRIDS_STEPS + step];
}

uint16_t pl_grids_src_step(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, const uint8_t density[3],
                           const uint8_t perturb[3], int step)
{
    uint16_t state = 0;
    for (int p = 0; p < PL_GRIDS_PARTS; p++) {
        uint8_t level = pl_grids_src_level(src, user, x, y, p, step);
        if (level < 255 - perturb[p]) {
            level = (uint8_t)(level + perturb[p]);
        } else {
            level = 255;
        }
        uint8_t threshold = (uint8_t)~density[p];
        if (level > threshold) {
            state |= (uint16_t)(1 << p);
            if (level > PL_GRIDS_ACCENT) {
                state |= (uint16_t)(8 << p);
            } else if (src != PL_GRIDS_SRC_MAP && level < PL_GRIDS_GHOST) {
                state |= (uint16_t)(64 << p);
            }
        }
    }
    return state;
}

void pl_grids_copy(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, const uint8_t density[3],
                   pl_grids_pat_t *out)
{
    pl_grids_pat_t r;
    r.grid = (uint8_t)pl_grids_src_grid(src, user);
    for (int p = 0; p < PL_GRIDS_PARTS; p++) {
        for (int s = 0; s < PL_GRIDS_STEPS; s++) {
            uint8_t lv = pl_grids_src_level(src, user, x, y, p, s);
            if (src == PL_GRIDS_SRC_MAP) {
                lv = lv <= (uint8_t)~density[p] ? 0 : lv > PL_GRIDS_ACCENT ? PL_GRIDS_LV_ACC : PL_GRIDS_LV_HIT;
            } else if (user_pat(src, user)) {
                lv = user_pat(src, user)->lv[p * PL_GRIDS_STEPS + s]; // и немые слоты триолей: копия точная
            }
            r.lv[p * PL_GRIDS_STEPS + s] = lv;
        }
    }
    *out = r; // out может быть одним из user
}
