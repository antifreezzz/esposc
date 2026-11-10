#pragma once

// Каркас панели по инструментам (480 x 480): шапка 40 px, вкладки 36, рабочая область, док 48 (pl_ia.h).
// Одна таблица и для отрисовки, и для касаний; тап мимо кнопок ничего не делает.

enum { PL_BAR_MAIN, PL_BAR_OVERLAY, PL_BARS };
// MAIN - имя группы и сводка, PLAY с темпом, режим, загрузка ядер. OVERLAY - оверлей (сетка значений, редактор,
// выбор режима, выбор USER, большой XY): CLOSE слева, заголовок, действие справа.

enum {
    PL_NAV_NONE,
    PL_NAV_CLOSE,  // закрыть оверлей
    PL_NAV_ACTION, // DONE, BACK
    PL_NAV_PLAY,   // тап - пуск и стоп секвенсеров, сдвиг вбок - темп
    PL_NAV_MODE,   // тап - выбор режима
};

#define PL_NAV_DRAG_PX 12 // сдвиг пальца по PLAY дальше - темп, не тап

int pl_nav_at(int bar, int x);                        // кнопка под x, PL_NAV_NONE мимо
void pl_nav_rect(int bar, int nav, int *x0, int *x1); // x0 == x1 - у вида нет такой кнопки
void pl_nav_title(int bar, int *x0, int *x1);         // место заголовка (у MAIN - имя группы и сводка)
// Тап в шапке: начат на кнопке и отпущен на той же (палец можно чуть увести вниз); PLAY - без сдвига вбок.
int pl_nav_tap(int bar, int x0, int y0, int x1, int y1);
// Палец, начатый на PLAY, ушёл вбок: тянем темп.
int pl_nav_drag(int bar, int x0, int y0, int x);
// Темп на плашке PLAY: dump_bpm - свой темп синта, pos_bpm - по которому идём (0 - нет данных).
// ext 1 - идём по клоку BSP: показываем его темп, тянуть нельзя.
int pl_nav_tempo(int dump_bpm, int pos_bpm, int *ext);

enum { PL_ZONE_TOP, PL_ZONE_TABS, PL_ZONE_WORK, PL_ZONE_DOCK };
int pl_nav_zone(int y, int tabs); // tabs 0 - у группы нет полосы вкладок (PERF)
int pl_nav_work_y(int tabs);      // верх рабочей области
// Тап по кнопке дока: группа или -1 (мимо, съехал, тусклая MOD).
int pl_nav_dock_tap(int x0, int y0, int x1, int y1);
// Тап по вкладке группы: вкладка или -1 (правее вкладок - действие страницы, pl_nav_act_tap).
int pl_nav_tab_tap(int group, int x0, int y0, int x1, int y1);
// Действия страницы правее вкладок: n кнопок поровну; x0 == x1 - места нет.
void pl_nav_act_rect(int group, int n, int i, int *x0, int *x1);
int pl_nav_act_tap(int group, int n, int x0, int y0, int x1, int y1); // номер кнопки или -1

// Выбор режима: без BSP (live 0) METAL, ACID, DRONE, SEQ; с BSP LIVE, DRONE. Коды для PL_MODE в mode, число - ответ.
int pl_nav_modes(int live, int mode[4]);
const char *pl_nav_mode_name(int live, int mode);
// Поле demo модели после выбора mode, до снимка синта: без BSP - песня, с BSP - дрон или прежняя песня.
int pl_nav_mode_demo(int live, int demo, int mode);
void pl_nav_mode_rect(int n, int i, int *x, int *y, int *w, int *h); // плитка i из n на экране
int pl_nav_mode_at(int n, int x, int y);                              // плитка под точкой, -1 мимо
