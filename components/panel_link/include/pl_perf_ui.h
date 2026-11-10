#pragma once

// Экран PERF (дом панели, y 40..432 без полосы вкладок): раскладка в координатах 480 x 480,
// попадание пальцем и какая ручка синта стоит за элементом.

enum {
    PL_PUI_NONE,
    PL_PUI_SCENE,   // 8 сцен (0..7): тап - вызвать, удержание - записать
    PL_PUI_MORPH,   // время морфа сцен (0..8)
    PL_PUI_XY,      // XY-пэд 200x200 страницы USER 1 (слоты E2 и E3)
    PL_PUI_FILL,    // сбивка Grids, пока держишь
    PL_PUI_GRIDS,   // переключатель GRIDS ON
    PL_PUI_STUTTER, // 5 сегментов статтера: OFF, 1/4, 1/8, 1/16, 1/32
    PL_PUI_DJF,     // горизонтальный ползунок DJ FILTER от середины
    PL_PUI_PUMP,    // горизонтальный ползунок PUMP
    PL_PUI_FADER,   // 7 вертикальных ползунков громкости
    PL_PUI_IDS
};

enum { PL_PUI_SCENES = 8, PL_PUI_STUTTERS = 5, PL_PUI_FADERS = 7 };

// Прямоугольник элемента id (idx - номер сцены, сегмента или ползунка). 0 - нет такого.
int pl_pui_rect(int id, int idx, int *x, int *y, int *w, int *h);
// Что под точкой (x, y); в arg - индекс внутри группы (или 0 у одиночных).
int pl_pui_at(int x, int y, int *arg);
// Страница и энкодер синта за элементом; 0 - у сцен, морфа и XY (у них своя логика).
int pl_pui_target(int id, int idx, int *page, int *enc);
// Значение горизонтального ползунка (DJF, PUMP) по x пальца: левый край 0, правый 127.
int pl_pui_hbar_value(int x);
// Значение вертикального ползунка громкости по y пальца: верх 127, низ 0.
int pl_pui_fader_value(int y);
