#include "bank_fixture.h"

#include <math.h>
#include <string.h>

float fixture_level(int layer)
{
    return 0.2f + 0.25f * (float)layer;
}

float fixture_hz(int note, int rr)
{
    return 500.0f * (float)(rr + 1) + 100.0f * (float)(note - 48);
}

static const int16_t STEPS[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

uint8_t fixture_adpcm_encode(acid_adpcm_t *st, int16_t x)
{
    int step = STEPS[st->index];
    int diff = (int)x - (int)st->pred;
    uint8_t nib = 0;
    if (diff < 0) {
        nib = 8;
        diff = -diff;
    }
    if (diff >= step) {
        nib |= 4;
        diff -= step;
    }
    if (diff >= step / 2) {
        nib |= 2;
        diff -= step / 2;
    }
    if (diff >= step / 4) {
        nib |= 1;
    }
    acid_adpcm_step(st, nib);
    return nib;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

size_t fixture_bank(uint8_t *buf, size_t cap, const fixture_note_t *notes, int n)
{
    int nlayers = 0, nrr = 0;
    for (int i = 0; i < n; i++) {
        nlayers += notes[i].nlayers;
        for (int l = 0; l < notes[i].nlayers; l++) {
            nrr += notes[i].nrr[l];
        }
    }
    size_t tables = ACID_BANK_HDR_SIZE + 4u * (size_t)n + 4u * (size_t)nlayers + 8u * (size_t)nrr;
    size_t pos = (tables + 3u) & ~(size_t)3u;
    size_t at_note = ACID_BANK_HDR_SIZE, at_layer = at_note + 4u * (size_t)n;
    size_t at_rr = at_layer + 4u * (size_t)nlayers;
    if (pos > cap) {
        return 0;
    }
    memset(buf, 0, pos);
    int li = 0, ri = 0;
    for (int i = 0; i < n; i++) {
        const fixture_note_t *fn = &notes[i];
        uint8_t *np = buf + at_note + 4u * (size_t)i;
        np[0] = (uint8_t)fn->note;
        np[1] = (uint8_t)fn->nlayers;
        put16(np + 2, (uint16_t)li);
        for (int l = 0; l < fn->nlayers; l++, li++) {
            uint8_t *lp = buf + at_layer + 4u * (size_t)li;
            lp[0] = (uint8_t)fn->vel_hi[l];
            lp[1] = (uint8_t)fn->nrr[l];
            put16(lp + 2, (uint16_t)ri);
            for (int r = 0; r < fn->nrr[l]; r++, ri++) {
                size_t bytes = acid_bank_rr_bytes((uint32_t)fn->frames);
                if (pos + bytes > cap) {
                    return 0;
                }
                uint8_t *rp = buf + at_rr + 8u * (size_t)ri;
                put32(rp, (uint32_t)pos);
                put32(rp + 4, (uint32_t)fn->frames);
                acid_adpcm_t st[2] = {{0, 0}, {0, 0}};
                float w = 2.0f * 3.14159265f * fixture_hz(fn->note, r) / (float)ACID_BANK_RATE;
                float a = 32767.0f * fixture_level(l);
                uint8_t *p = buf + pos;
                for (int f = 0; f < fn->frames; f++) {
                    if (f % ACID_BANK_BLOCK == 0) {
                        for (int c = 0; c < 2; c++) {
                            put16(p, (uint16_t)st[c].pred);
                            p[2] = st[c].index;
                            p[3] = 0;
                            p += 4;
                        }
                    }
                    int16_t x = (int16_t)lrintf(a * sinf(w * (float)f));
                    uint8_t lo = fixture_adpcm_encode(&st[0], x);
                    uint8_t hi = fixture_adpcm_encode(&st[1], x);
                    *p++ = (uint8_t)(lo | (hi << 4));
                }
                pos += (bytes + 3u) & ~(size_t)3u;
            }
        }
    }
    put32(buf, ACID_BANK_MAGIC);
    put32(buf + 4, ACID_BANK_VERSION);
    put32(buf + 8, (uint32_t)pos);
    put32(buf + 16, ACID_BANK_RATE);
    put16(buf + 20, (uint16_t)n);
    put16(buf + 22, (uint16_t)nlayers);
    put16(buf + 24, (uint16_t)nrr);
    put32(buf + 12, acid_crc32(buf + ACID_BANK_HDR_SIZE, pos - ACID_BANK_HDR_SIZE));
    return pos;
}
