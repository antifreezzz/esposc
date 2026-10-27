#pragma once
#include <stddef.h>
#include <stdint.h>

// Пишет 48 кГц, 16 бит, стерео RIFF. 0 при успехе.
int acid_write_wav(const char *path, const int16_t *stereo, size_t frames);
