#include "test.h"
#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t le32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

TEST(test_wav_header)
{
    static int16_t frames[480 * 2];
    const char *path = "test_wav_out.wav";
    CHECK(acid_write_wav(path, frames, 480) == 0);
    unsigned char buf[44 + 1920 + 1];
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) {
        return;
    }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    remove(path);
    CHECK(n == 44 + 1920);
    CHECK(memcmp(buf, "RIFF", 4) == 0);
    CHECK(memcmp(buf + 8, "WAVE", 4) == 0);
    CHECK(buf[22] == 2);           // каналы
    CHECK(le32(buf + 24) == 48000); // частота
    CHECK(buf[34] == 16);          // бит
    CHECK(le32(buf + 40) == 1920);  // размер данных
}

int main(void)
{
    RUN(test_wav_header);
    TEST_MAIN_END();
}
