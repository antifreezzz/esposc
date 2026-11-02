#include "acid.h"
#include "test.h"

#define SR 48000.0f

TEST(test_decay_time_constant)
{
    acid_decay_t e;
    acid_decay_init(&e, 0.2f, SR);
    acid_decay_trigger(&e, 1.0f);
    float v = 0.0f;
    for (int i = 0; i < 9600; i++) {
        v = acid_decay_next(&e);
    }
    CHECK_NEAR(v, 0.3679f, 0.01f);
}

TEST(test_decay_retrigger)
{
    acid_decay_t e;
    acid_decay_init(&e, 0.2f, SR);
    acid_decay_trigger(&e, 1.0f);
    for (int i = 0; i < 5000; i++) {
        acid_decay_next(&e);
    }
    acid_decay_trigger(&e, 1.0f);
    CHECK(acid_decay_next(&e) >= 0.99f);
}

TEST(test_amp_attack_release)
{
    acid_amp_t e;
    acid_amp_init(&e, 0.003f, 0.01f, SR);
    acid_amp_gate(&e, 1);
    float v = 0.0f;
    for (int i = 0; i < 960; i++) {
        v = acid_amp_next(&e);
    }
    CHECK(v > 0.99f);
    acid_amp_gate(&e, 0);
    for (int i = 0; i < 2400; i++) {
        v = acid_amp_next(&e);
    }
    CHECK(v < 0.01f);
}

TEST(test_amp_gate_held_no_retrigger)
{
    acid_amp_t e;
    acid_amp_init(&e, 0.003f, 0.01f, SR);
    acid_amp_gate(&e, 1);
    float prev = 0.0f;
    for (int i = 0; i < 960; i++) {
        prev = acid_amp_next(&e);
    }
    acid_amp_gate(&e, 1);
    float v = acid_amp_next(&e);
    CHECK(v >= prev);
}

// Половинная частота слоя: шаг на два сэмпла равен двум обычным шагам (до округления float, -100 дБ).
TEST(test_decay_next2_equals_two_steps)
{
    acid_decay_t a, b;
    acid_decay_init(&a, 0.05f, SR);
    acid_decay_init(&b, 0.05f, SR);
    acid_decay_trigger(&a, 1.0f);
    acid_decay_trigger(&b, 1.0f);
    for (int i = 0; i < 1000; i++) {
        acid_decay_next(&a);
        float va = acid_decay_next(&a);
        float vb = acid_decay_next2(&b);
        CHECK(fabsf(va - vb) < 1e-5f);
    }
}

TEST(test_amp_next2_equals_two_steps)
{
    acid_amp_t a, b;
    acid_amp_init(&a, 0.003f, 0.01f, SR);
    acid_amp_init(&b, 0.003f, 0.01f, SR);
    for (int g = 1; g >= 0; g--) {
        acid_amp_gate(&a, g);
        acid_amp_gate(&b, g);
        for (int i = 0; i < 1000; i++) {
            acid_amp_next(&a);
            float va = acid_amp_next(&a);
            float vb = acid_amp_next2(&b);
            CHECK(fabsf(va - vb) < 1e-5f);
        }
    }
}

int main(void)
{
    RUN(test_decay_time_constant);
    RUN(test_decay_retrigger);
    RUN(test_amp_attack_release);
    RUN(test_amp_gate_held_no_retrigger);
    RUN(test_decay_next2_equals_two_steps);
    RUN(test_amp_next2_equals_two_steps);
    TEST_MAIN_END();
}
