#include "acid_math.h"
#include "acid_noise.h"
#include "test.h"

#include <math.h>

TEST(test_fast_tanh_close)
{
    float worst = 0.0f;
    for (float x = -6.0f; x <= 6.0f; x += 0.001f) {
        float e = fabsf(acid_tanh(x) - tanhf(x));
        worst = e > worst ? e : worst;
    }
    CHECK(worst < 0.03f);
}

TEST(test_fast_tanh_bounded)
{
    for (float x = -100.0f; x <= 100.0f; x += 0.01f) {
        CHECK(fabsf(acid_tanh(x)) <= 1.0f);
    }
}

// 1/x без программного деления: относительная ошибка на уровне точности float.
TEST(test_fast_rcp_exact_enough)
{
    float worst = 0.0f;
    for (float x = 1e-4f; x < 1e5f; x *= 1.0007f) {
        float e = fabsf(acid_rcp(x) * x - 1.0f);
        float en = fabsf(acid_rcp(-x) * -x - 1.0f);
        worst = fmaxf(worst, fmaxf(e, en));
    }
    CHECK(worst < 4e-7f);
}

// sin(2 pi p) для бочки и малого: чистая синусоида на слух, ошибка ниже 16 бит выхода.
TEST(test_fast_sin_exact_enough)
{
    float worst = 0.0f;
    for (float p = 0.0f; p < 1.0f; p += 1e-5f) {
        worst = fmaxf(worst, fabsf(acid_sin2pi(p) - (float)sin(2.0 * M_PI * p)));
    }
    CHECK(worst < 2e-6f);
}

// Кэш коэффициентов SVF: бит в бит как пересчёт на каждом сэмпле, и при смене g, k, и с нуля.
TEST(test_svf_cached_coefs_bit_exact)
{
    acid_svf_t f = {0};
    float ic1 = 0.0f, ic2 = 0.0f;
    uint32_t rng = 1;
    int same = 1;
    for (int i = 0; i < 20000; i++) {
        float g = i < 5000 ? 0.0f : (i < 12000 ? 0.3f : 1.7f), k = i < 9000 ? 0.0f : 0.5f;
        float in = acid_noise(&rng);
        float bp, hp;
        acid_svf_bp_hp(&f, in, g, k, &bp, &hp);
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float v3 = in - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        same &= bp == v1 && hp == in - k * v1 - v2;
    }
    CHECK(same);
}

int main(void)
{
    RUN(test_fast_rcp_exact_enough);
    RUN(test_fast_sin_exact_enough);
    RUN(test_svf_cached_coefs_bit_exact);
    RUN(test_fast_tanh_close);
    RUN(test_fast_tanh_bounded);
    TEST_MAIN_END();
}
