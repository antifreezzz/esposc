#include "wav.h"

#include <stdio.h>

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void put32(FILE *f, uint32_t v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

int acid_write_wav(const char *path, const int16_t *stereo, size_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    uint32_t data = (uint32_t)(frames * 4);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);          // PCM
    put16(f, 2);          // каналы
    put32(f, 48000);
    put32(f, 48000 * 4);  // байт в секунду
    put16(f, 4);          // байт на кадр
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, data);
    for (size_t i = 0; i < frames * 2; i++) {
        put16(f, (uint16_t)stereo[i]);
    }
    return fclose(f) == 0 ? 0 : -1;
}
