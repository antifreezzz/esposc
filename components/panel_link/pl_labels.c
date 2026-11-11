#include "pl_grids.h"
#include "pl_labels.h"

#include <math.h>
#include <stdio.h>

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const PAGE_NAME[14] = {"MIX",     "DRUMS", "CYMBALS", "TOMS",    "RIM",     "COWBELL", "SYNTH 1",
                                          "SYNTH 2", "FX",    "TONAL",   "LAYER 1", "LAYER 2", "GRIDS",   "PETRI"};

// По индексу энкодера с 0; NULL - пусто.
static const char *const MIX[16] = {NULL, "MASTER", "DRIVE", "SYNTH 1", "SYNTH 2", "KICK", "HATS", "CLAP",
                                    "SNARE", "PUMP", "PUMP REL", "DJ FILTER", "DJ RESO", "PAN S1", "PAN S2", NULL};
static const char *const DRUMS[16] = {NULL, "KICK TUNE", "KICK SWEEP", "SWEEP TIME", "KICK LEN", "HAT TONE",
                                      "CH LEN", "OH LEN", "CLAP TONE", "CLAP TAIL", "SN TUNE", "SN SNAP",
                                      "SN LEN", "PAN HAT", "PAN CLAP", "PAN SN"};
static const char *const CYMBALS[16] = {NULL, "CRASH VOL", "CRASH TAIL", "CRASH TUNE", "CRASH PAN", "RIDE VOL",
                                        "RIDE TAIL", "RIDE TUNE", "RIDE PAN", "BELL VOL", "VEL SENS"};
static const char *const PERC[16] = {NULL, "MODEL", "TUNE", "DECAY", "P DEPTH", "P TIME", NULL, NULL,
                                     "CLICK", "DRIVE", "CRUSH", "VOLUME", "PAN", "VEL SENS", "SPREAD", "WIDTH"};
// Тон и характер по модели перкуссии.
static const char *const PERC_TONE[5][2] = {{"SHAPE", "OVERTONE"}, {"BP FREQ", "RATIO"}, {"INDEX", "RATIO"},
                                            {"BRIGHT", "MODES"},   {"RESO", "COLOR"}};
static const char *const S303[16] = {NULL, "TYPE", "CUTOFF", "RESO", "ENV", "DECAY", "ACCENT", "WAVE",
                                     "GLIDE", "TRANS", NULL, NULL, NULL, "DRIVE", "TONE", "HOWL"};
static const char *const PLAITS[16] = {NULL, "TYPE", "HARMONICS", "TIMBRE", "MORPH", "DECAY", "LPG", "GLIDE",
                                       "TRANS", "T ENV", "M ENV", "P ENV", "ACCENT", "DRIVE", "TONE", "HOWL"};
static const char *const PETRI_SYN[16] = {NULL,     "TYPE",     "CUTOFF", "RESO",   "ENV",   "DECAY", "ATTACK", "RELEASE",
                                          "RELIEF", "CONTRAST", "ORBIT",  "RADIUS", "CENTER", "DRIVE", "TONE",   "HOWL"};
static const char *const PETRI_PAGE[16] = {NULL,       "PRESET",   "F FINE",  "K FINE",  "STEPS",   "GRAZERS",
                                           "SEEDERS",  "MUTAGENS", "VORTICES", "APPETITE", "MUTATION", "GRZ>CUT",
                                           "MUT>RESO", "VRT>ENV",  "FREEZE",  "RESEED"};
static const char *const PRESET[6] = {"MITOSIS", "CORAL", "MAZE", "WORMS", "SOLITONS", "SKATES"};
static const char *const RESEED[2] = {"-", "RESEED"};
static const char *const RELIEF[4] = {"HEIGHT", "EDGE", "FOLD", "VOID"};
static const char *const ORBIT[5] = {"CIRCLE", "EIGHT", "LISS 3:2", "ROSE", "EPI"};
static const char *const FX[16] = {NULL, "CRUSH BITS", "CRUSH RATE", "DLY S1", "DLY S2", "DLY DRUMS", "DLY TIME",
                                   "DLY FB", "DLY TONE", "STUTTER", "MOTION", "WIDTH", "LYR1 VOL", "LYR2 VOL", "LYR HALF",
                                   "REVERB"};
static const char *const REVERB[16] = {NULL, "RVB S1", "RVB S2", "RVB DRUMS", "DECAY", "DAMP", "WIDTH"};
static const char *const GRIDS[16] = {NULL, "ON", "X", "Y", "KICK", "SNARE", "HAT", "CHAOS", "HAT MODE",
                                      "TIME", "EVERY", "FILL LEN", "FILL", "PATTERN", "SWING", NULL};
// Источник рисунка: имена банка - те же, что в pl_grids_bank (сверяет test_labels).
static const char *const GRIDS_SRC[PL_GRIDS_SRCS] = {
    "MAP",        "FOUR FLOOR", "HOUSE", "ELECTRO", "BRK HOUSE", "DUB TECHNO", "TWO STEP", "AMEN",
    "FUNK BREAK", "JUNGLE",     "HALF TIME",     "ROCK 8",      "ROCK 16",     "DBL KICK", "GALLOP",   "BLAST",
    "BOMB BLAST", "D-BEAT",     "SHUFFLE",  "U1",          "U2",          "U3",          "U4"};
static const char *const SWING[4] = {"50%", "58%", "67%", "75%"};
static const char *const TONAL[16] = {NULL, "STEP LEN", "STEPS", "ORDER", "TARGET", "HELD", "SCALE", "ROOT",
                                      "STEP 1", "STEP 2", "STEP 3", "STEP 4", "STEP 5", "STEP 6", "STEP 7", "STEP 8"};

// Типы синта: 0 - наш 303, 1..24 - модели Plaits по порядку движков, 25 - чашка Петри (не у слоя), 26 - OFF (только у слоя).
static const char *const TYPES[27] = {"303",      "VA VCF",   "PHASE DIST", "DX7 1",    "DX7 2",     "DX7 3",  "TERRAIN",
                                      "STRINGS",  "CHIP",     "VA 2WAVE",   "WAVESHAPE", "FM 2OP",   "FORMANT", "ADDITIVE",
                                      "WAVETABLE", "CHORDS",  "SPEECH",     "SWARM",    "NOISE",     "PARTICLE", "STRING",
                                      "MODAL",    "BASS DRUM", "SNARE",     "HI-HAT",   "PETRI",     "OFF"};
static const char *const PERC_MODEL[5] = {"ANALOG", "SQUARES", "FM", "MODAL", "NOISE"};
static const char *const WAVE[2] = {"SAW", "SQR"};
static const char *const DLY_TIME[5] = {"1/16", "1/8", "3/16", "1/4", "3/8"};
static const char *const STUTTER[5] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
static const char *const MOTION[3] = {"CENTER", "PING-PONG", "RANDOM"};
static const char *const ORDER[3] = {"FWD", "BOUNCE", "RANDOM"};
static const char *const TARGET[3] = {"BOTH", "S1", "S2"};
static const char *const HELD[3] = {"FINISH", "SLIDE", "JUMP"};
static const char *const SCALE[8] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "MIXO", "HARM MIN", "PENTA"};
// Страница модулятора (энкодеры 2-13), порядок значений - как в acid_mod.h.
static const char *const MOD[16] = {NULL,     "SHAPE",   "RATE",    "SYNC",    "TRIG",    "PROB",    "ATTACK", "DECAY",
                                    "AMOUNT", "DEPTH 1", "DEPTH 2", "DEPTH 3", "DEPTH 4", NULL,      NULL,     NULL};
static const char *const MOD_SHAPE[8] = {"SIN", "TRI", "SAW", "RAMP", "SQR", "S&H", "DRIFT", "ENV"};
static const char *const MOD_SYNC[9] = {"FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const char *const MOD_TRIG[14] = {"FREE", "S1 NOTE", "S2 NOTE", "S1 ACC", "S2 ACC", "KICK", "SNARE",
                                         "HAT",  "CLAP",    "PERC",    "BEAT",   "BAR",    "4 BARS", "STEP"};
static const char *const MOD_NAME[PL_MOD_PAGES] = {"MOD 1", "MOD 2", "MOD 3", "MOD 4"};
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const HAT_MODE[3] = {"CLOSED", "OPEN", "MIX"};
static const char *const TIME_HAND[2] = {"HAT", "RIDE"};
static const char *const EVERY[4] = {"OFF", "4", "8", "16"};
static const char *const FILL_LEN[3] = {"1", "2", "4"};
static const char *const FILL_NOW[2] = {"-", "FILL"};
static const char *const ROOT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int is_mod_page(int page)
{
    return page >= PL_PAGE_MOD1 && page < PL_PAGE_MOD1 + PL_MOD_PAGES;
}

const char *pl_page_name(int page)
{
    if (page == PL_PAGE_REVERB) {
        return "REVERB";
    }
    if (is_mod_page(page)) {
        return MOD_NAME[page - PL_PAGE_MOD1];
    }
    return page >= 0 && page < N(PAGE_NAME) ? PAGE_NAME[page] : NULL;
}

#define LAYER1 PL_PAGE_LAYER1
#define PETRI PL_TYPE_PETRI
#define LAYER_OFF PL_LAYER_OFF
#define GRIDS_PAGE PL_PAGE_GRIDS
#define PETRI_PG PL_PAGE_PETRI

static int is_layer(int page)
{
    return page == LAYER1 || page == LAYER1 + 1;
}

// Слой подписан как страница его синта; выключенный слой пуст, кроме TYPE.
static int as_synth(int page)
{
    return is_layer(page) ? page - LAYER1 + 6 : page;
}

static int layer_off(int page, const uint8_t *raw)
{
    return is_layer(page) && raw[1] == LAYER_OFF;
}

static int model(const uint8_t *raw, int n)
{
    return raw[1] < n ? raw[1] : 0;
}

// Вид синта по типу: 0 - 303, 1 - Plaits, 2 - PETRI.
enum { K303, KPLAITS, KPETRI };
static int synth_kind(const uint8_t *raw)
{
    int t = model(raw, LAYER_OFF);
    return t == 0 ? K303 : t == PETRI ? KPETRI : KPLAITS;
}

const char *pl_knob_label(int page, int enc, const uint8_t page_raw[16])
{
    if (enc < 1 || enc > 15 || (layer_off(page, page_raw) && enc > 1)) {
        return NULL;
    }
    if (is_mod_page(page)) {
        return MOD[enc];
    }
    switch (as_synth(page)) {
    case 0: return MIX[enc];
    case 1: return DRUMS[enc];
    case 2: return CYMBALS[enc];
    case 3:
    case 4:
    case 5:
        if (enc == 6 || enc == 7) {
            return PERC_TONE[model(page_raw, 5)][enc - 6];
        }
        return page != 3 && enc >= 14 ? NULL : PERC[enc]; // интервал и ширина только у томов
    case 6:
    case 7: {
        static const char *const *const BY_KIND[3] = {S303, PLAITS, PETRI_SYN};
        return BY_KIND[synth_kind(page_raw)][enc];
    }
    case 8: return FX[enc];
    case PL_PAGE_REVERB: return REVERB[enc];
    case 9: return TONAL[enc];
    case GRIDS_PAGE: return GRIDS[enc];
    case PETRI_PG: return PETRI_PAGE[enc];
    default: return NULL;
    }
}

// Список ручки: слова (или NULL - числа) и границы. 0 - ручка не список.
static int list_of(int page, int enc, const uint8_t *raw, const char *const **words, int *lo, int *hi)
{
    *words = NULL;
    *lo = 0;
    if (is_mod_page(page)) {
        if (enc == 1) {
            *words = MOD_SHAPE, *hi = 7;
        } else if (enc == 3) {
            *words = MOD_SYNC, *hi = 8;
        } else if (enc == 4) {
            *words = MOD_TRIG, *hi = 13;
        } else {
            return 0;
        }
        return 1;
    }
    if (is_layer(page) && enc == 1) {
        *words = TYPES, *hi = LAYER_OFF;
        return 1;
    }
    if (layer_off(page, raw)) {
        return 0;
    }
    page = as_synth(page);
    int synth = page == 6 || page == 7, kind = synth ? synth_kind(raw) : K303, plaits = kind == KPLAITS;
    if (synth && enc == 1) {
        *words = TYPES, *hi = PETRI;
    } else if (synth && kind == KPETRI && enc == 8) {
        *words = RELIEF, *hi = 3;
    } else if (synth && kind == KPETRI && enc == 10) {
        *words = ORBIT, *hi = 4;
    } else if (synth && kind == KPETRI) {
        return 0;
    } else if (synth && !plaits && enc == 7) {
        *words = WAVE, *hi = 1;
    } else if (synth && enc == (plaits ? 8 : 9)) {
        *lo = 52, *hi = 76; // транспозиция ±12
    } else if (page >= 3 && page <= 5 && enc == 1) {
        *words = PERC_MODEL, *hi = 4;
    } else if (page == 8 && enc == 6) {
        *words = DLY_TIME, *hi = 4;
    } else if (page == 8 && enc == 9) {
        *words = STUTTER, *hi = 4;
    } else if (page == 8 && enc == 10) {
        *words = MOTION, *hi = 2;
    } else if (page == 9 && enc >= 1 && enc <= 7) {
        static const char *const *const W[8] = {NULL, NULL, NULL, ORDER, TARGET, HELD, SCALE, ROOT};
        static const int LO[8] = {0, 0, 1, 0, 0, 0, 0, 0}, HI[8] = {0, 16, 8, 2, 2, 2, 7, 11};
        *words = W[enc], *lo = LO[enc], *hi = HI[enc];
    } else if (page == 9 && enc >= 8) {
        *lo = 52, *hi = 76; // шаг тональности, 64 без сдвига
    } else if (page == PETRI_PG && (enc == 1 || (enc >= 4 && enc <= 8) || enc == 15)) {
        static const char *const *const W[16] = {[1] = PRESET, [15] = RESEED};
        static const int HI[16] = {[1] = 5, [4] = 6, [5] = 16, [6] = 4, [7] = 4, [8] = 3, [15] = 1};
        *words = W[enc], *hi = HI[enc];
    } else if (page == GRIDS_PAGE && (enc == 1 || (enc >= 8 && enc <= 14))) {
        static const char *const *const W[15] = {[1] = OFF_ON, [8] = HAT_MODE, [9] = TIME_HAND, [10] = EVERY,
                                                 [11] = FILL_LEN, [12] = FILL_NOW, [13] = GRIDS_SRC, [14] = SWING};
        static const int HI[15] = {[1] = 1, [8] = 2, [9] = 1, [10] = 3, [11] = 2, [12] = 1,
                                   [13] = PL_GRIDS_SRCS - 1, [14] = 3};
        *words = W[enc], *hi = HI[enc];
    } else {
        return 0;
    }
    return 1;
}

void pl_knob_range(int page, int enc, const uint8_t page_raw[16], int *lo, int *hi)
{
    const char *const *w;
    if (!list_of(page, enc, page_raw, &w, lo, hi)) {
        *lo = 0;
        *hi = 127;
    }
}

int pl_knob_list(int page, int enc, const uint8_t page_raw[16])
{
    const char *const *w;
    int lo, hi;
    return list_of(page, enc, page_raw, &w, &lo, &hi);
}

int pl_knob_skip(int page, int enc, const uint8_t page_raw[16])
{
    (void)page_raw;
    return is_layer(page) && enc == 1 ? PETRI : -1;
}

// Значения числовых ручек модулятора: частота, проценты, время, глубина со знаком (как считает acid_mod.c).
static const char *mod_number(int enc, int v, char buf[12])
{
    if (enc == 2) {
        float hz = 0.05f * exp2f((float)v * (8.644f / 127.0f));
        snprintf(buf, 12, hz < 1.0f ? "%.2f Hz" : hz < 10.0f ? "%.1f Hz" : "%.0f Hz", (double)hz);
    } else if (enc == 6 || enc == 7) {
        float s = 0.001f * exp2f((float)v * (13.288f / 127.0f));
        if (s < 0.9995f) {
            snprintf(buf, 12, "%.0f ms", (double)(s * 1000.0f));
        } else {
            snprintf(buf, 12, s < 9.95f ? "%.1f s" : "%.0f s", (double)s);
        }
    } else if (enc == 5 || enc == 8) {
        snprintf(buf, 12, "%d%%", (v * 100 + 63) / 127);
    } else if (enc >= 9 && enc <= 12) {
        snprintf(buf, 12, v > 64 ? "+%d" : "%d", v - 64);
    } else {
        snprintf(buf, 12, "%d", v);
    }
    return buf;
}

const char *pl_value_text(int page, int enc, int v, const uint8_t page_raw[16], char buf[12])
{
    const char *const *w;
    int lo, hi;
    if (is_mod_page(page)) {
        if (list_of(page, enc, page_raw, &w, &lo, &hi) && v >= lo && v <= hi) {
            return w[v - lo];
        }
        return mod_number(enc, v, buf);
    }
    if (list_of(page, enc, page_raw, &w, &lo, &hi) && v >= lo && v <= hi) {
        if (w) {
            return w[v - lo];
        }
        if (lo == 52) {
            int d = v - 64;
            if (page == 9 && page_raw[6] != 0) {
                d = d > 7 ? 7 : d < -7 ? -7 : d; // в ладу синт зажимает шаг до 7 ступеней
            }
            snprintf(buf, 12, d > 0 ? "+%d" : "%d", d);
            return buf;
        }
        if (page == 9 && enc == 1 && v == 0) {
            return "OFF"; // слой выключен
        }
    }
    if ((as_synth(page) == 6 || as_synth(page) == 7) && synth_kind(page_raw) == KPETRI && enc == 12 && v == 127) {
        return "FOLLOW"; // центр орбиты едет за пожирателем
    }
    if ((page == 8 || page == PETRI_PG) && enc == 14) {
        return v >= 64 ? "ON" : "OFF"; // LYR HALF, FREEZE: переключатели
    }
    snprintf(buf, 12, "%d", v);
    return buf;
}

int pl_labels_depend(int page)
{
    return (page >= 3 && page <= 7) || is_layer(page);
}

// Стиль «Б. Тёплый аналог»: цвета-лампы (docs/design/panel-mockups.html).
static const uint32_t PAGE_COLOR[14] = {0xf2c46d, 0xff6b4a, 0xffb648, 0xff8a4c, 0xffa25e, 0xffd66b, 0xff9d2e,
                                        0xa8d672, 0xe39bb0, 0x7fd1c0, 0xff9d2e, 0xa8d672, 0xff6b4a, 0x7fd1a0}; // слои - цветом своего синта

uint32_t pl_page_color(int page)
{
    if (page == PL_PAGE_REVERB) {
        return PAGE_COLOR[8];
    }
    if (is_mod_page(page)) {
        return 0xc4a6e0; // C_MOD панели
    }
    return page >= 0 && page < N(PAGE_COLOR) ? PAGE_COLOR[page] : 0;
}

uint32_t pl_knob_color(int page, int enc, const uint8_t page_raw[16])
{
    if (!pl_knob_label(page, enc, page_raw)) {
        return 0;
    }
    if (is_mod_page(page)) { // форма, время и запуск, огибающая, глубины
        return enc <= 4 ? 0xc4a6e0 : enc <= 7 ? 0x8fc4d8 : enc == 8 ? 0xffb648 : 0xe39bb0;
    }
    switch (as_synth(page)) {
    case PL_PAGE_REVERB: return pl_page_color(page);
    case 0: // MIX: мастер/драйв, синты, ударка, пампинг/DJ-фильтр, панорама
        if (enc <= 2) return 0xffb648;
        if (enc <= 4) return 0xa8d672;
        if (enc <= 8) return 0xff6b4a;
        if (enc <= 12) return 0x8fc4d8;
        return 0x7fd1c0;
    case 1: // DRUMS: бочка, хэты, клэп, малый, панорама
        if (enc <= 4) return 0xff6b4a;
        if (enc <= 7) return 0xffb648;
        if (enc <= 9) return 0xe39bb0;
        if (enc <= 12) return 0x8fc4d8;
        return 0x7fd1c0;
    case 2: // CYMBALS: крэш, райд, купол и велосити
        if (enc <= 4) return 0xff8a4c;
        if (enc <= 8) return 0xffc65a;
        return 0x8fc4d8;
    case 3:
    case 4:
    case 5: // PERC: модель, высота/спад, тембр, грязь, выход/стерео
        if (enc == 1) return 0xffd66b;
        if (enc <= 5) return 0xff8a4c;
        if (enc <= 8) return 0xffb648;
        if (enc <= 10) return 0xff6b4a;
        return 0x7fd1c0;
    case 6:
    case 7: { // SYNTH 1 & 2
        if (synth_kind(page_raw) == KPETRI) { // тип, фильтр, огибающие, рельеф и орбита, перегруз
            if (enc == 1) return 0xffd66b;
            if (enc <= 5) return as_synth(page) == 6 ? 0xff9d2e : 0xa8d672;
            if (enc <= 7) return 0x8fc4d8;
            if (enc <= 12) return 0xe39bb0;
            return 0xff6b4a;
        }
        int plaits = synth_kind(page_raw) == KPLAITS;
        if (enc == 1) return 0xffd66b;
        if (enc <= 6) return as_synth(page) == 6 ? 0xff9d2e : 0xa8d672;
        if (enc <= (plaits ? 8 : 9)) return 0x8fc4d8;
        if (enc <= 12) return 0xe39bb0;
        return 0xff6b4a;
    }
    case 8: // FX: крашер, посылы, дилэй, статтер
        if (enc <= 2) return 0xff6b4a;
        if (enc <= 5) return 0xffb648;
        if (enc == 9) return 0x7fd1c0;
        return 0xe39bb0;
    case 9: // TONAL: шагатель, лад/тоника, шаги 1..8
        if (enc <= 5) return 0x7fd1c0;
        if (enc <= 7) return 0xffb648;
        return 0xa8d672;
    case PETRI_PG: // PETRI: колония, жители, еда и мутации, маршруты в фильтр, фриз и пересев
        if (enc <= 4) return 0x7fd1a0;
        if (enc <= 8) return 0xffb648;
        if (enc <= 10) return 0xff6b4a;
        if (enc <= 13) return 0xe39bb0;
        return 0x8fc4d8;
    case GRIDS_PAGE: // GRIDS: вкл, карта, плотности и хаос, руки, сбивки
        if (enc == 1) return 0xffd66b;
        if (enc <= 3) return 0x8fc4d8;
        if (enc <= 7) return 0xff6b4a;
        if (enc <= 9) return 0xffb648;
        return 0xe39bb0;
    default:
        return 0;
    }
}

int pl_knob_bipolar(int page, int enc, const uint8_t page_raw[16])
{
    if (!pl_knob_label(page, enc, page_raw)) {
        return 0;
    }
    if (is_mod_page(page)) {
        return enc >= 9 && enc <= 12; // глубины со знаком, 64 - ноль
    }
    switch (as_synth(page)) {
    case 0: return enc == 11 || enc == 13 || enc == 14;
    case 1: return enc >= 13 && enc <= 15;
    case 2: return enc == 3 || enc == 4 || enc == 7 || enc == 8;
    case 3:
    case 4:
    case 5: return enc == 12;
    case 6:
    case 7:
        if (synth_kind(page_raw) == KPETRI) {
            return enc == 14;
        }
        if (synth_kind(page_raw) == K303) {
            return enc == 9 || enc == 14;
        }
        return (enc >= 8 && enc <= 11) || enc == 14;
    case 9: return enc >= 8 && enc <= 15;
    case PETRI_PG: return enc == 2 || enc == 3 || (enc >= 11 && enc <= 13);
    default: return 0;
    }
}

const char *pl_page_summary(int page, const uint8_t page_raw[16], char buf[24])
{
    switch (page) {
    case 0:
        if (page_raw[11] != 64) {
            snprintf(buf, 24, "DJF %s %+d", page_raw[11] < 64 ? "LP" : "HP", (int)page_raw[11] - 64);
            return buf;
        }
        return "LEVELS & DJF";
    case 1: return "808 / 909";
    case PETRI_PG: return pl_value_text(page, 1, page_raw[1], page_raw, buf);
    case 2: return "CRASH & RIDE";
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case LAYER1:
    case LAYER1 + 1: return pl_value_text(page, 1, page_raw[1], page_raw, buf);
    case 8: {
        const char *dt = DLY_TIME[page_raw[6] < N(DLY_TIME) ? page_raw[6] : 0];
        if (page_raw[9] > 0 && page_raw[9] < N(STUTTER)) {
            snprintf(buf, 24, "DLY %s ST %s", dt, STUTTER[page_raw[9]]);
        } else {
            snprintf(buf, 24, "DLY %s", dt);
        }
        return buf;
    }
    case GRIDS_PAGE:
        if (!page_raw[1]) {
            return "OFF";
        }
        if (page_raw[13] != PL_GRIDS_SRC_MAP && page_raw[13] < PL_GRIDS_SRCS) {
            snprintf(buf, 24, "ON %s", GRIDS_SRC[page_raw[13]]);
            return buf;
        }
        snprintf(buf, 24, "ON X%d Y%d", page_raw[2], page_raw[3]);
        return buf;
    case 9:
        if (page_raw[1] == 0) {
            return "OFF";
        }
        snprintf(buf, 24, "%s %s", SCALE[page_raw[6] < N(SCALE) ? page_raw[6] : 0],
                 ROOT[page_raw[7] < N(ROOT) ? page_raw[7] : 0]);
        return buf;
    default:
        if (is_mod_page(page)) { // форма и запуск: SIN FREE, S&H KICK
            snprintf(buf, 24, "%s %s", MOD_SHAPE[page_raw[1] < N(MOD_SHAPE) ? page_raw[1] : 0],
                     MOD_TRIG[page_raw[4] < N(MOD_TRIG) ? page_raw[4] : 0]);
            return buf;
        }
        return "";
    }
}

const char *pl_user_name(int up)
{
    static const char *const NAME[PL_USER_PAGES] = {"USER 1", "USER 2", "USER 3", "USER 4", "USER 5", "USER 6"};
    return up >= 0 && up < PL_USER_PAGES ? NAME[up] : NULL;
}

const char *pl_slot_label(const pl_target_t t[PL_TARGETS], const uint8_t raw[PL_PAGES][PL_KNOBS], char buf[20])
{
    int n = 0;
    while (n < PL_TARGETS && t[n].pe != PL_NONE) {
        n++;
    }
    int page = t[0].pe >> 4, enc = t[0].pe & 15;
    const char *first = n && page < PL_PAGES ? pl_knob_label(page, enc, raw[page]) : NULL;
    if (!first) {
        return NULL;
    }
    if (n == 1) {
        return first;
    }
    snprintf(buf, 20, "%s +%d", first, n - 1);
    return buf;
}
