#pragma once

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
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
