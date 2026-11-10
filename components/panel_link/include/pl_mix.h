#pragma once

// Вкладка MIX инструмента: встроенная страница псевдонимов (PL_PAGE_MIX_DRUMS, PL_PAGE_MIX_SYN1 и следом).
// Слот ведёт на ручку штатной страницы; своих значений у страницы нет, номера BSP тоже.

int pl_mix_is(int page);
// Цель слота enc (индекс энкодера 1..15). type и ltype - типы синта и слоя этого инструмента (у ударки не
// важны): TRANS у 303 и Plaits на разных энкодерах, у чашки и выключенного слоя его нет. 1 - цель есть.
int pl_mix_target(int page, int enc, int type, int ltype, int *tpage, int *tenc);
