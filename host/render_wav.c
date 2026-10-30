// acid_render <out.wav> <seconds>: демо-трек с начала в WAV, как играет плата без BSP.
#include "acid.h"
#include "acid_demo.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <out.wav> <seconds>\n", argv[0]);
        return 2;
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
    acid_demo_init(&d, &acid_demo_song);
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
    return rc ? 1 : 0;
}
