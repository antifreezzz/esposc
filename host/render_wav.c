// acid_render <out.wav> <seconds>: рендер паттерна по умолчанию в WAV.
#include "acid.h"
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
    acid_synth_init(&s, 48000.0f);
    acid_synth_render_lr(&s, l, r, frames);
    acid_pcm_lr_to_s16(l, r, pcm, frames);
    int rc = acid_write_wav(argv[1], pcm, frames);
    free(l);
    free(r);
    free(pcm);
    return rc ? 1 : 0;
}
