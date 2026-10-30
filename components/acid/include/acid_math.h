#pragma once
#include <stdint.h>

// Деление float на S3 - программный вызов (у FPU нет инструкции, GCC её не строит ни с какими флагами).
// 1/x: начальное приближение битовым трюком (~5%), три шага Ньютона (ошибка 5% -> 2.5e-3 -> 6e-6 -> точность
// float). Только умножения и сложения FPU. x не 0, не бесконечность.
static inline float acid_rcp(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x < 0.0f ? -x : x};
    u.i = 0x7EF311C3u - u.i;
    float a = x < 0.0f ? -x : x, y = u.f;
    y = y * (2.0f - a * y);
    y = y * (2.0f - a * y);
    y = y * (2.0f - a * y);
    return x < 0.0f ? -y : y;
}

// sin(2 pi p) для p в [0, 1) без libm: отражение в четверть периода, ряд до t^11 (ошибка ~1e-7).
static inline float acid_sin2pi(float p)
{
    float x = p - 0.5f; // sin(2 pi p) = -sin(2 pi x), x в [-0.5, 0.5)
    x = x > 0.25f ? 0.5f - x : (x < -0.25f ? -0.5f - x : x);
    float t = 6.28318531f * x, t2 = t * t;
    float s = t * (1.0f + t2 * (-1.6666667e-1f + t2 * (8.3333333e-3f + t2 * (-1.9841270e-4f +
                                                             t2 * (2.7557319e-6f + t2 * -2.5052108e-8f)))));
    return -s;
}

// Быстрый tanh (Паде 3/2), точен до ~2% и ровно +/-1 за |x| >= 3.
static inline float acid_tanh(float x)
{
    if (x > 3.0f) {
        return 1.0f;
    }
    if (x < -3.0f) {
        return -1.0f;
    }
    float x2 = x * x;
    return x * (27.0f + x2) * acid_rcp(27.0f + 9.0f * x2); // без программного деления
}
