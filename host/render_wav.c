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
    float *mono = malloc(frames * sizeof *mono);
    int16_t *pcm = malloc(frames * 2 * sizeof *pcm);
    if (!mono || !pcm) {
        return 1;
    }
    static acid_synth_t s;
    acid_synth_init(&s, 48000.0f);
    acid_synth_render(&s, mono, frames);
    acid_pcm_to_s16_stereo(mono, pcm, frames);
    int rc = acid_write_wav(argv[1], pcm, frames);
    free(mono);
    free(pcm);
    return rc ? 1 : 0;
}
