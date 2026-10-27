// Минимальный тестовый харнесс для сборки ядра на ПК.
#pragma once
#include <math.h>
#include <stdio.h>

static int test_failures_;
static int test_count_;

#define TEST(name) static void name(void)

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            test_failures_++;                                          \
        }                                                              \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                          \
    do {                                                               \
        double a_ = (a), b_ = (b);                                     \
        if (!(fabs(a_ - b_) <= (eps))) {                               \
            printf("  FAIL %s:%d: %s = %g, expected %g +/- %g\n",      \
                   __FILE__, __LINE__, #a, a_, b_, (double)(eps));     \
            test_failures_++;                                          \
        }                                                              \
    } while (0)

#define RUN(name)                                                      \
    do {                                                               \
        int before_ = test_failures_;                                  \
        test_count_++;                                                 \
        name();                                                        \
        printf("%s %s\n", test_failures_ == before_ ? "ok  " : "FAIL", #name); \
    } while (0)

#define TEST_MAIN_END()                                                \
    do {                                                               \
        printf("%d tests, %d failed checks\n", test_count_, test_failures_); \
        return test_failures_ ? 1 : 0;                                 \
    } while (0)
