#pragma once

// Экспоненциальный спад (огибающая фильтра): падает до 1/e за decay_s.
typedef struct {
    float value;
    float coef;
} acid_decay_t;

void acid_decay_init(acid_decay_t *e, float decay_s, float sr);
void acid_decay_set(acid_decay_t *e, float decay_s, float sr);
void acid_decay_trigger(acid_decay_t *e, float level);
float acid_decay_next(acid_decay_t *e);

// Огибающая громкости по гейту: однополюсник к 1 при гейте, к 0 без него.
typedef struct {
    float value;
    float att;
    float rel;
    int gate;
} acid_amp_t;

void acid_amp_init(acid_amp_t *e, float attack_s, float release_s, float sr);
void acid_amp_gate(acid_amp_t *e, int on);
float acid_amp_next(acid_amp_t *e);
