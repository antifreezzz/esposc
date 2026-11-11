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

// Без libm в рендере: на плате вызовы tanf, exp2f, floorf из флэша стоили больше всего голоса (замер этапа 10).
// 2^x: целая часть в показатель, дробная - полином (ошибка ~0.01%).
static inline float acid_exp2(float x)
{
    x = x < -60.0f ? -60.0f : x > 60.0f ? 60.0f : x;
    int i = (int)x;
    i -= x < (float)i;
    float f = x - (float)i;
    float m = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union {
        float f;
        int32_t i;
    } u = {m};
    u.i += i * (1 << 23); // не сдвиг: у отрицательного i он не определён стандартом
    return u.f;
}

// tan(x) для 0 <= x < 1.42 (частота до 0.45 sr): Паде 3/2, ошибка до ~3% у верхнего края.
static inline float acid_tan(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

// tan(x) с точностью float для 0 <= x < 1.42: Паде 5/6 (ошибка ~4e-7) и acid_rcp вместо деления. Для срезов на
// управляющей частоте, где 3% у acid_tan слышны как сдвиг среза.
static inline float acid_tan_fine(float x)
{
    float x2 = x * x;
    float num = x * (10395.0f + x2 * (-1260.0f + 21.0f * x2));
    float den = 10395.0f + x2 * (-4725.0f + x2 * (210.0f - x2));
    return num * acid_rcp(den);
}
