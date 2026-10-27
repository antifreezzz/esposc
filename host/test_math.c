#include "acid_math.h"
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

int main(void)
{
    RUN(test_fast_tanh_close);
    RUN(test_fast_tanh_bounded);
    TEST_MAIN_END();
}
