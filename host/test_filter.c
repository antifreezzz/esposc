#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define PI_F 3.14159265f

// Отношение RMS выхода к RMS входа на установившемся синусе.
static float sine_gain(float sine_hz, float amp, float cutoff, float res)
{
    acid_ladder_t f;
    acid_ladder_reset(&f);
    int settle = (int)(0.1f * SR), measure = (int)(0.2f * SR);
    double in_sq = 0.0, out_sq = 0.0;
    for (int i = 0; i < settle + measure; i++) {
        float x = amp * sinf(2.0f * PI_F * sine_hz * (float)i / SR);
        float y = acid_ladder_process(&f, x, cutoff, res, SR);
        if (i >= settle) {
            in_sq += (double)x * x;
            out_sq += (double)y * y;
        }
    }
    return (float)sqrt(out_sq / in_sq);
}

TEST(test_ladder_passes_low)
{
    float g = sine_gain(100.0f, 0.5f, 2000.0f, 0.0f);
    CHECK(g >= 0.8f);
    CHECK(g <= 1.2f);
}

TEST(test_ladder_cuts_high)
{
    CHECK(sine_gain(8000.0f, 0.5f, 500.0f, 0.0f) < 0.01f);
}

TEST(test_ladder_resonance_peak)
{
    float flat = sine_gain(1000.0f, 0.1f, 1000.0f, 0.0f);
    float peak = sine_gain(1000.0f, 0.1f, 1000.0f, 0.8f);
    CHECK(peak > 2.0f * flat);
}

// Верх ручки резонанса: фильтр заводится сам и свистит после удара, но не разносит.
static float tail_after_ping(float res)
{
    acid_ladder_t f;
    acid_ladder_reset(&f);
    float g = acid_ladder_coef(1000.0f, SR), pk = 0.0f;
    for (int i = 0; i < (int)SR; i++) {
        float y = acid_ladder_run(&f, i == 0 ? 0.5f : 0.0f, g, res);
        if (i > (int)(0.8f * SR)) {
            pk = fabsf(y) > pk ? fabsf(y) : pk;
        }
    }
    return pk;
}

TEST(test_ladder_self_oscillates_above_1)
{
    CHECK(tail_after_ping(0.97f) < 1e-6f);
    float pk = tail_after_ping(ACID_LADDER_RES_MAX);
    CHECK(pk > 0.1f);
    CHECK(pk < 2.0f);
}

TEST(test_ladder_stable_max_res)
{
    acid_ladder_t f;
    acid_ladder_reset(&f);
    acid_osc_t o = {0};
    acid_osc_set_freq(&o, 55.0f, SR);
    int n = (int)(10.0f * SR);
    int finite = 1;
    float peak = 0.0f;
    for (int i = 0; i < n; i++) {
        float cutoff = 20.0f * powf(1000.0f, (float)i / (float)n);
        float y = acid_ladder_process(&f, acid_osc_saw(&o), cutoff, 1.0f, SR);
        if (!isfinite(y)) {
            finite = 0;
            break;
        }
        peak = fabsf(y) > peak ? fabsf(y) : peak;
    }
    CHECK(finite);
    CHECK(peak < 4.0f);
}

int main(void)
{
    RUN(test_ladder_passes_low);
    RUN(test_ladder_cuts_high);
    RUN(test_ladder_resonance_peak);
    RUN(test_ladder_self_oscillates_above_1);
    RUN(test_ladder_stable_max_res);
    TEST_MAIN_END();
}
