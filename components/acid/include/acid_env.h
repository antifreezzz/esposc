#pragma once

// Экспоненциальный спад (огибающая фильтра): падает до 1/e за decay_s.
typedef struct {
    float value;
    float coef;
} acid_decay_t;

void acid_decay_init(acid_decay_t *e, float decay_s, float sr);
void acid_decay_set(acid_decay_t *e, float decay_s, float sr);
void acid_decay_trigger(acid_decay_t *e, float level);
// Шаги огибающих - инлайн: зовутся на каждом сэмпле из другого файла.
static inline float acid_decay_next(acid_decay_t *e)
{
    e->value *= e->coef;
    return e->value;
}
// Шаг на два сэмпла (слой на половинной частоте): полюс в квадрате, коэффициент не трогаем.
static inline float acid_decay_next2(acid_decay_t *e)
{
    e->value *= e->coef * e->coef;
    return e->value;
}

// Огибающая громкости по гейту: однополюсник к 1 при гейте, к 0 без него.
typedef struct {
    float value;
    float att;
    float rel;
    int gate;
} acid_amp_t;

void acid_amp_init(acid_amp_t *e, float attack_s, float release_s, float sr);
static inline void acid_amp_gate(acid_amp_t *e, int on)
{
    e->gate = on;
}
static inline float acid_amp_next(acid_amp_t *e)
{
    if (e->gate) {
        e->value = 1.0f + (e->value - 1.0f) * e->att;
    } else {
        e->value *= e->rel;
    }
    return e->value;
}
static inline float acid_amp_next2(acid_amp_t *e)
{
    if (e->gate) {
        e->value = 1.0f + (e->value - 1.0f) * (e->att * e->att);
    } else {
        e->value *= e->rel * e->rel;
    }
    return e->value;
}
