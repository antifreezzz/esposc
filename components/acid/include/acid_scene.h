#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_custom.h"
#include "acid_link.h"
#include "acid_synth.h"

// Сцены: снимок всех ручек штатных страниц (энкодеры 2..16, с типом синтов) и значений слотов своих страниц.
// Вызов на ближайшей черте такта по клоку: сразу или плавно за 1..8 тактов (тип синтов и списки прыгают
// в начале). Без клока - сразу. Ручки ставятся как правки с панели (подхват BSP), не больше
// ACID_SCENE_PER_CHUNK за кусок: обработка событий идёт в куске 666 мкс ядра 1.

#define ACID_SCENES 8
#define ACID_SCENE_PER_CHUNK 8
#define ACID_SCENE_NOCLOCK 750    // кусков без тика (0.5 с): клока нет
#define ACID_SCENE_SNAP_EVERY 150 // кусков между снимками панели во время перехода (100 мс)
#define ACID_SCENE_BAR 96         // тиков на такт

typedef struct {
    uint8_t knob[ACID_SCENES][ACID_PAGES][ACID_KNOBS]; // [.][.][0] не используется
    uint8_t user[ACID_SCENES][ACID_USER_PAGES][ACID_KNOBS];
    uint8_t used; // бит занятого слота
    int save;     // записали сцену: пора во flash
    // Переход.
    int cur;   // последняя вызванная сцена, -1 нет
    int state; // 0 нет, 1 ждём черту такта, 2 идёт
    int bars, ticks, last_phase, idle_chunks, cursor, since_snap, final_sweeps, dirty;
    uint8_t from[ACID_PAGES][ACID_KNOBS];
} acid_scene_t;

#define ACID_SCENE_BLOB (4 + 1 + sizeof(((acid_scene_t *)0)->knob) + sizeof(((acid_scene_t *)0)->user))

void acid_scene_init(acid_scene_t *sc);
void acid_scene_store(acid_scene_t *sc, int n, const acid_synth_t *s, const acid_custom_t *c);
// 0 - пустой слот или n вне 0..7. bars 0..8.
int acid_scene_recall(acid_scene_t *sc, int n, int bars);
// Каждый кусок перед рендером (свои страницы - l->custom). 1 - пора слать панели снимок.
int acid_scene_step(acid_scene_t *sc, acid_link_t *l, acid_synth_t *s);
size_t acid_scene_export(const acid_scene_t *sc, void *buf, size_t cap);
int acid_scene_import(acid_scene_t *sc, const void *buf, size_t len);
