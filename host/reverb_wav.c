// reverb_wav [--song acid|metal] <out.wav> <seconds> <decay 0..127> <damp 0..127> <send 0..1> [bank.bin]:
// прослушка ревёрба до обвязки в синте. Демо-трек как у acid_render, в ревёрб идёт весь микс (моно-сумма * send):
// посылов по инструментам ещё нет, так что бочка тоже там.
#include "acid.h"
#include "acid_demo.h"
#include "acid_reverb.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const acid_demo_song_t *song = &acid_demo_song;
    if (argc > 2 && strcmp(argv[1], "--song") == 0) {
        song = strcmp(argv[2], "metal") == 0 ? &acid_metal_song : &acid_demo_song;
        argv += 2;
        argc -= 2;
    }
    if (argc != 6 && argc != 7) {
        fprintf(stderr, "usage: %s [--song acid|metal] <out.wav> <seconds> <decay> <damp> <send> [bank.bin]\n", argv[0]);
        return 2;
    }
    static acid_bank_t bank;
    void *img = NULL;
    if (argc == 7) {
        FILE *f = fopen(argv[6], "rb");
        long len = -1;
        if (f && fseek(f, 0, SEEK_END) == 0) {
            len = ftell(f);
            rewind(f);
        }
        img = len > 0 ? malloc((size_t)len) : NULL;
        if (!img || fread(img, 1, (size_t)len, f) != (size_t)len || !acid_bank_open(&bank, img, (size_t)len)) {
            fprintf(stderr, "bad bank %s\n", argv[6]);
            return 1;
        }
        fclose(f);
    }
    size_t frames = (size_t)(atof(argv[2]) * 48000.0) / ACID_DEMO_CHUNK * ACID_DEMO_CHUNK;
    float send_gain = (float)atof(argv[5]);
    float *l = malloc(frames * sizeof *l), *r = malloc(frames * sizeof *r);
    int16_t *pcm = malloc(frames * 2 * sizeof *pcm);
    static acid_synth_t s;
    static acid_demo_t d;
    static acid_midi_event_t ev[96];
    static acid_reverb_t rv;
    static float mem[ACID_REVERB_MEM];
    acid_synth_init(&s, 48000.0f);
    if (img) {
        acid_synth_set_bank(&s, &bank);
    }
    acid_demo_init(&d, song);
    acid_reverb_init(&rv, mem);
    acid_reverb_set(&rv, atoi(argv[3]), atoi(argv[4]));
    for (size_t off = 0; off < frames; off += ACID_DEMO_CHUNK) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&s, &ev[i]);
        }
        acid_synth_render_lr(&s, l + off, r + off, ACID_DEMO_CHUNK);
        float send[ACID_DEMO_CHUNK], wl[ACID_DEMO_CHUNK], wr[ACID_DEMO_CHUNK];
        for (int i = 0; i < ACID_DEMO_CHUNK; i++) {
            send[i] = send_gain * 0.5f * (l[off + i] + r[off + i]);
        }
        if (acid_reverb_run(&rv, send, wl, wr, ACID_DEMO_CHUNK)) {
            for (int i = 0; i < ACID_DEMO_CHUNK; i++) {
                l[off + i] += wl[i];
                r[off + i] += wr[i];
            }
        }
    }
    acid_pcm_lr_to_s16(l, r, pcm, frames);
    return acid_write_wav(argv[1], pcm, frames) ? 1 : 0;
}
