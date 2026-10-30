// Демо-трек: ~3 минуты акида по кругу. Каждая часть выводит вперёд своё: 303, модели Plaits на синте 2,
// ударку и её ручки, пампинг, DJ-фильтр, стерео, дилэй (центр, пинг-понг, случайно), хруст, статтер,
// слой тональности. Ручки по страницам BSP: 0 микс, 1 ударка, 2 синт 1, 3 синт 2, 4 эффекты, 5 тональность.
#include "acid_demo.h"

#define N(n, v, l) {n, v, l}
#define X {0, 0, 0} // пауза
#define AC 120      // акцент 303: velocity выше 100
#define NO 90

// 303, ля-минор. len 2 - слайд в следующую ноту.
static const acid_demo_note_t bass_a[16] = {
    N(45, AC, 1), N(45, NO, 1), N(57, NO, 2), N(45, NO, 1), X, N(48, AC, 1), N(45, NO, 1), N(55, NO, 2),
    N(57, NO, 1), X, N(45, AC, 1), N(52, NO, 2), N(50, NO, 1), N(45, NO, 1), N(60, AC, 2), N(57, NO, 1),
};
static const acid_demo_note_t bass_b[16] = {
    N(57, AC, 1), N(45, NO, 1), N(57, NO, 1), N(60, NO, 2), N(62, AC, 1), N(45, NO, 1), N(64, NO, 2), N(62, NO, 1),
    N(57, AC, 1), X, N(55, NO, 2), N(57, NO, 1), N(45, AC, 1), N(57, NO, 1), N(69, NO, 2), N(67, NO, 1),
};

// Синт 2.
static const acid_demo_note_t arp[16] = { // восьмые, FM
    N(69, 90, 1), X, N(72, 80, 1), X, N(76, 90, 1), X, N(72, 80, 1), X,
    N(69, 90, 1), X, N(72, 80, 1), X, N(79, 95, 1), X, N(76, 80, 1), X,
};
static const acid_demo_note_t pad[16] = { // длинные ноты: тональность ведёт их слайдом через смену шага
    N(57, 90, 8), X, X, X, X, X, X, X, N(60, 90, 8), X, X, X, X, X, X, X,
};
static const acid_demo_note_t pluck[16] = {
    N(69, 100, 1), X, X, N(72, 90, 1), X, X, N(76, 100, 1), X,
    N(74, 90, 1), X, X, N(72, 90, 1), X, X, N(67, 100, 1), X,
};
static const acid_demo_note_t stab[16] = { // офбиты
    X, X, N(57, 100, 1), X, X, X, N(60, 100, 1), X, X, X, N(57, 100, 1), X, X, X, N(64, 110, 2), N(62, 90, 1),
};
static const acid_demo_note_t riser[16] = {
    N(57, 110, 16), X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,
};

#define K ACID_DEMO_KICK
#define S ACID_DEMO_SNARE
#define C ACID_DEMO_CLAP
#define H ACID_DEMO_CHAT
#define O ACID_DEMO_OHAT
#define A ACID_DEMO_ACC
#define CR ACID_DEMO_CRASH
#define R ACID_DEMO_RIDE
#define B ACID_DEMO_BELL

static const uint16_t dr_intro[16] = {K | H, 0, H, 0, K | H, 0, H, 0, K | H, 0, H, 0, K | H, 0, H, H};
static const uint16_t dr_groove[16] = {K | A, H, O, H, K | C, H, O, H | S, K, H, O, H, K | C | A, H, O, S};
static const uint16_t dr_break[16] = {H, H, H, H, H | S, H, H, H, H, H, H | S, H, H | S, S, S | H, S | A};
static const uint16_t dr_drop[16] = {K | A | C | R, H, R, H, K | C | R, H, R, H | S, K | R, H, R, H, K | C | S | R, H, B | A, S};

#define P ACID_DEMO_PICK

// 1 Интро: всё в исходное (круг начинается с одного и того же состояния), 303 из-под фильтра.
static const acid_demo_knob_t k_intro[] = {
    {0, 2, 127, 127, 0}, {0, 3, 64, 64, 0},   {0, 4, 70, 70, 0},   {0, 5, 90, 90, 0},   {0, 10, 0, 0, 0},    {0, 11, 40, 40, 0},  {0, 13, 30, 30, 0},
    {0, 14, 64, 64, 0},  {0, 15, 64, 64, 0},  {0, 12, 20, 63, 0},  // DJ-фильтр ФНЧ открывается
    {1, 6, 64, 64, 0},   {1, 10, 50, 50, 0},  {1, 12, 64, 64, 0},  {1, 14, 80, 80, 0}, {1, 15, 48, 48, 0},
    {1, 16, 64, 64, 0},
    {3, 2, 0, 0, 0},     {3, 4, 90, 90, 0},   {3, 5, 70, 70, 0},   {3, 6, 60, 60, 0},   {3, 7, 90, 90, 0},
    {3, 8, 0, 0, 0},     {3, 9, 40, 40, 0},   {3, 10, 64, 64, 0},  {3, 3, 20, 60, 8},   // cutoff
    {5, 2, 0, 0, 0},     {5, 3, 0, 0, 0},     {5, 4, 0, 0, 0},     {5, 5, 0, 0, 0},     {5, 6, 0, 0, 0},
    {5, 7, 2, 2, 0},     {5, 10, 0, 0, 0},    {5, 11, 0, 0, 0},    {6, 2, 0, 0, 0},
    {2, 3, 127, 127, 0}, {2, 4, 64, 64, 0},
};
// 2 Грув: FM-арпеджио справа, пампинг набирается, дилэй в центре.
static const acid_demo_knob_t k_groove[] = {
    {0, 12, 64, 64, 0},  {0, 10, 0, 80, 0},   {0, 15, 84, 110, P}, {3, 3, 60, 95, 10},  {3, 5, 70, 100, 0},
    {4, 3, 40, 90, 0},   {4, 4, 50, 50, 0},   {4, 5, 30, 80, 0},   {4, 6, 50, 50, 0},
    {5, 5, 70, 70, 0},   {5, 8, 75, 75, 0},   {5, 9, 80, 80, 0},   {5, 12, 127, 127, 0},
};
// 3 Квадрат в полутонах на 2 такта, пэд слайдом через смену шага.
static const acid_demo_knob_t k_tonal[] = {
    {6, 9, 64, 64, 0},   {6, 10, 69, 71, P},  {6, 11, 67, 60, P},  {6, 12, 62, 59, P},  {6, 3, 4, 4, 0},
    {6, 4, 0, 0, 0},     {6, 5, 0, 0, 0},     {6, 6, 1, 1, 0},     {6, 7, 0, 0, 0},     {6, 8, 9, 9, 0},
    {6, 2, 2, 2, 0},     {4, 6, 90, 90, 0},   {3, 3, 95, 80, 10},
};
// 4 Пинг-понг, квадрат по ступеням ля-минора, время дилэя меняется посередине.
static const acid_demo_knob_t k_pingpong[] = {
    {6, 9, 64, 64, 0},   {6, 10, 66, 68, P},  {6, 11, 62, 60, P},  {6, 12, 65, 67, P},  {6, 7, 2, 2, 0},
    {6, 6, 2, 2, 0},     {6, 2, 1, 1, 0},     {5, 11, 1, 1, 0},    {5, 12, 127, 127, 0}, {5, 4, 40, 40, 0},
    {5, 6, 35, 35, 0},   {5, 7, 2, 3, 0},     {4, 6, 40, 40, 0},   {4, 4, 30, 100, 0},  {0, 5, 110, 110, 0},
};
// 5 Брейк: без бочки, 303 меандром, хруст, ФВЧ, синт 1 гуляет по панораме, повторы случайно.
static const acid_demo_knob_t k_break[] = {
    {6, 2, 0, 0, 0},     {3, 8, 1, 1, 0},     {3, 4, 110, 110, 0}, {3, 3, 30, 110, 10}, {5, 2, 0, 70, 0},
    {5, 3, 30, 30, 0},   {0, 12, 64, 100, 0}, {0, 13, 30, 70, 0},  {0, 14, 20, 108, 0}, {5, 11, 2, 2, 0},
    {5, 12, 90, 127, P}, {5, 7, 2, 2, 0},     {0, 4, 55, 55, 0},
};
// 6 Статтер: 1/8, потом 1/16.
static const acid_demo_knob_t k_stutter[] = {
    {5, 2, 0, 0, 0}, {5, 3, 0, 0, 0}, {0, 12, 64, 64, 0}, {0, 14, 64, 64, 0}, {3, 8, 0, 0, 0}, {5, 10, 2, 3, 0},
    {0, 4, 70, 70, 0},
};
// 7 Дроп: всё вместе, квадрат в полутонах по 4 такта.
static const acid_demo_knob_t k_drop[] = {
    {5, 10, 0, 0, 0},    {0, 10, 90, 90, 0},  {3, 4, 95, 95, 0},   {3, 3, 70, 110, 10}, {5, 11, 1, 2, P},
    {6, 9, 64, 64, 0},   {6, 10, 60, 62, P},  {6, 11, 65, 67, P},  {6, 12, 62, 59, P},  {6, 7, 0, 0, 0},
    {6, 6, 1, 1, 0},     {6, 2, 4, 4, 0},     {0, 15, 30, 50, P},  {4, 3, 60, 60, 0},   {4, 6, 50, 50, 0},
    {0, 5, 120, 120, 0},
};
// 8 Пик: шумовой райзер, drive и ручки ударки ползут.
static const acid_demo_knob_t k_peak[] = {
    {6, 2, 0, 0, 0},     {0, 3, 64, 90, 0},   {0, 5, 80, 80, 0},  {1, 6, 60, 110, 0},  {1, 10, 40, 90, 0},  {1, 12, 60, 100, 0},
    {4, 4, 20, 110, 0},  {4, 6, 100, 100, 0}, {5, 5, 50, 50, 0},  {2, 3, 127, 60, 0},  {2, 4, 64, 76, 0},
};
// 9 Аутро: всё закрывается.
static const acid_demo_knob_t k_outro[] = {
    {0, 3, 90, 64, 0},   {0, 5, 100, 100, 0}, {3, 3, 90, 25, 10},  {0, 12, 64, 15, 0},  {5, 4, 40, 0, 0},    {5, 5, 50, 0, 0},
    {5, 6, 35, 0, 0},    {0, 10, 80, 0, 0},   {4, 6, 40, 40, 0},   {2, 3, 127, 127, 0}, {2, 4, 64, 64, 0},
};
// 10 Хвост к интро.
static const acid_demo_knob_t k_tail[] = {{0, 12, 15, 25, 0}};

static const uint8_t pool_fm[] = {11, 4, 10};    // FM 2-op, DX7, waveshaping
static const uint8_t pool_pad[] = {15, 13, 14};  // аккорды, аддитивный, wavetable
static const uint8_t pool_pluck[] = {21, 20, 6}; // modal, string, wave terrain
static const uint8_t pool_stab[] = {7, 9, 1};    // string machine, VA две волны, VA + фильтр
static const uint8_t pool_noise[] = {18, 17, 23}; // шум с фильтром, swarm, малый барабан
static const uint8_t pool_out[] = {12, 16, 2};   // granular formant, speech, phase distortion

#define KN(k) k, (uint8_t)(sizeof k / sizeof k[0])
#define POOL(p) p, (uint8_t)(sizeof p)

static const acid_demo_part_t parts[] = {
    {8, bass_a, NULL, dr_intro, KN(k_intro), NULL, 0, 0, 0},
    {16, bass_a, arp, dr_groove, KN(k_groove), POOL(pool_fm), 1, CR},
    {8, bass_a, pad, dr_groove, KN(k_tonal), POOL(pool_pad), 1, 0},
    {8, bass_a, pluck, dr_groove, KN(k_pingpong), POOL(pool_pluck), 1, 0},
    {8, bass_b, NULL, dr_break, KN(k_break), NULL, 0, 1, 0},
    {2, bass_b, NULL, dr_groove, KN(k_stutter), NULL, 0, 0, 0},
    {16, bass_b, stab, dr_drop, KN(k_drop), POOL(pool_stab), 1, CR},
    {16, bass_b, riser, dr_drop, KN(k_peak), POOL(pool_noise), 1, CR},
    {8, bass_a, pluck, dr_groove, KN(k_outro), POOL(pool_out), 0, CR},
    {2, bass_a, NULL, dr_intro, KN(k_tail), NULL, 0, 0, 0},
};

const acid_demo_song_t acid_demo_song = {parts, (int)(sizeof parts / sizeof parts[0])};
