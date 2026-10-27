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

int main(void)
{
    RUN(test_decay_time_constant);
    RUN(test_decay_retrigger);
    RUN(test_amp_attack_release);
    RUN(test_amp_gate_held_no_retrigger);
    TEST_MAIN_END();
}
