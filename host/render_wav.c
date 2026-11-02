// acid_render [--song acid|metal] <out.wav> <seconds> [bank.bin]: демо-трек с начала в WAV, как играет плата
// без BSP. Без --song - акид (на нём проверка «бит в бит как в main»).
// bank.bin - образ тарелок (tools/samples/build_samples.py); без него тарелки молчат.
#include "acid.h"
#include "acid_demo.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const acid_demo_song_t *song = &acid_demo_song;
    if (argc > 2 && strcmp(argv[1], "--song") == 0) {
        song = strcmp(argv[2], "metal") == 0 ? &acid_metal_song : strcmp(argv[2], "acid") == 0 ? &acid_demo_song : NULL;
        argv += 2;
        argc -= 2;
    }
    if (!song || (argc != 3 && argc != 4)) {
        fprintf(stderr, "usage: %s [--song acid|metal] <out.wav> <seconds> [bank.bin]\n", argv[0]);
        return 2;
    }
    static acid_bank_t bank;
    void *img = NULL;
    if (argc == 4) {
        FILE *f = fopen(argv[3], "rb");
        long len = -1;
        if (f && fseek(f, 0, SEEK_END) == 0) {
            len = ftell(f);
            rewind(f);
        }
        img = len > 0 ? malloc((size_t)len) : NULL;
        if (!img || fread(img, 1, (size_t)len, f) != (size_t)len || !acid_bank_open(&bank, img, (size_t)len)) {
            fprintf(stderr, "bad bank %s\n", argv[3]);
            return 1;
        }
        fclose(f);
    }
    size_t frames = (size_t)(atof(argv[2]) * 48000.0);
    float *l = malloc(frames * sizeof *l);
    float *r = malloc(frames * sizeof *r);
    int16_t *pcm = malloc(frames * 2 * sizeof *pcm);
    if (!l || !r || !pcm) {
        return 1;
    }
    static acid_synth_t s;
    static acid_demo_t d;
    static acid_midi_event_t ev[96];
    acid_synth_init(&s, 48000.0f);
    if (img) {
        acid_synth_set_bank(&s, &bank);
    }
    acid_demo_init(&d, song);
    // События перед каждым куском, как в прошивке.
    for (size_t off = 0; off < frames; off += ACID_DEMO_CHUNK) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&s, &ev[i]);
        }
        size_t m = frames - off < ACID_DEMO_CHUNK ? frames - off : ACID_DEMO_CHUNK;
        acid_synth_render_lr(&s, l + off, r + off, m);
    }
    acid_pcm_lr_to_s16(l, r, pcm, frames);
    int rc = acid_write_wav(argv[1], pcm, frames);
    free(l);
    free(r);
    free(pcm);
    free(img);
    return rc ? 1 : 0;
}
