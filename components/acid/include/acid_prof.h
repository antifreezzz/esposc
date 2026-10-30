#pragma once
#include <stdint.h>

// Разбивка рендера по участкам в тактах, только в сборке стенда (ACID_PROFILE). Иначе макросы пустые.
enum { ACID_PROF_CYM, ACID_PROF_PERC, ACID_PROF_SYNTH1, ACID_PROF_DRUMS, ACID_PROF_FX, ACID_PROF_N };

#ifdef ACID_PROFILE
#include "esp_cpu.h"
extern uint32_t acid_prof[ACID_PROF_N];
#define ACID_PROF_T(t) uint32_t t = esp_cpu_get_cycle_count()
#define ACID_PROF_ADD(k, t)                                  \
    do {                                                     \
        uint32_t now_ = esp_cpu_get_cycle_count();           \
        acid_prof[k] += now_ - (t);                          \
        (t) = now_;                                          \
    } while (0)
#else
#define ACID_PROF_T(t) (void)0
#define ACID_PROF_ADD(k, t) (void)0
#endif
