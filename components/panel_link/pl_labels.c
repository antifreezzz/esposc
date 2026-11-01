#include "pl_labels.h"

#include <stdio.h>

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const PAGE_NAME[12] = {"MIX", "DRUMS", "CYMBALS", "TOMS", "RIM", "COWBELL",
                                          "SYNTH 1", "SYNTH 2", "FX", "TONAL", "LAYER 1", "LAYER 2"};

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
static const char *const FX[16] = {NULL, "CRUSH BITS", "CRUSH RATE", "DLY S1", "DLY S2", "DLY DRUMS", "DLY TIME",
                                   "DLY FB", "DLY TONE", "STUTTER", "MOTION", "WIDTH", "LYR1 VOL", "LYR2 VOL"};
static const char *const TONAL[16] = {NULL, "STEP LEN", "STEPS", "ORDER", "TARGET", "HELD", "SCALE", "ROOT",
                                      "STEP 1", "STEP 2", "STEP 3", "STEP 4", "STEP 5", "STEP 6", "STEP 7", "STEP 8"};

// Типы синта: 0 - наш 303, дальше модели Plaits по порядку движков.
static const char *const TYPES[26] = {"303", "VA VCF", "PHASE DIST", "DX7 1", "DX7 2", "DX7 3", "TERRAIN",
                                      "STRINGS", "CHIP", "VA 2WAVE", "WAVESHAPE", "FM 2OP", "FORMANT",
                                      "ADDITIVE", "WAVETABLE", "CHORDS", "SPEECH", "SWARM", "NOISE",
                                      "PARTICLE", "STRING", "MODAL", "BASS DRUM", "SNARE", "HI-HAT", "OFF"}; // OFF - только у слоя
static const char *const PERC_MODEL[5] = {"ANALOG", "SQUARES", "FM", "MODAL", "NOISE"};
static const char *const WAVE[2] = {"SAW", "SQR"};
static const char *const DLY_TIME[5] = {"1/16", "1/8", "3/16", "1/4", "3/8"};
static const char *const STUTTER[5] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
static const char *const MOTION[3] = {"CENTER", "PING-PONG", "RANDOM"};
static const char *const ORDER[3] = {"FWD", "BOUNCE", "RANDOM"};
static const char *const TARGET[3] = {"BOTH", "S1", "S2"};
static const char *const HELD[3] = {"FINISH", "SLIDE", "JUMP"};
static const char *const SCALE[8] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "MIXO", "HARM MIN", "PENTA"};
static const char *const ROOT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

const char *pl_page_name(int page)
{
    return page >= 0 && page < N(PAGE_NAME) ? PAGE_NAME[page] : NULL;
}

#define LAYER1 10
#define LAYER_OFF 25

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

const char *pl_knob_label(int page, int enc, const uint8_t page_raw[16])
{
    if (enc < 1 || enc > 15 || (layer_off(page, page_raw) && enc > 1)) {
        return NULL;
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
    case 7: return (model(page_raw, 25) == 0 ? S303 : PLAITS)[enc];
    case 8: return FX[enc];
    case 9: return TONAL[enc];
    default: return NULL;
    }
}

// Список ручки: слова (или NULL - числа) и границы. 0 - ручка не список.
static int list_of(int page, int enc, const uint8_t *raw, const char *const **words, int *lo, int *hi)
{
    *words = NULL;
    *lo = 0;
    if (is_layer(page) && enc == 1) {
        *words = TYPES, *hi = LAYER_OFF;
        return 1;
    }
    if (layer_off(page, raw)) {
        return 0;
    }
    page = as_synth(page);
    int plaits = (page == 6 || page == 7) && model(raw, 25) != 0;
    if ((page == 6 || page == 7) && enc == 1) {
        *words = TYPES, *hi = 24;
    } else if ((page == 6 || page == 7) && !plaits && enc == 7) {
        *words = WAVE, *hi = 1;
    } else if ((page == 6 || page == 7) && enc == (plaits ? 8 : 9)) {
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

const char *pl_value_text(int page, int enc, int v, const uint8_t page_raw[16], char buf[12])
{
    const char *const *w;
    int lo, hi;
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
    snprintf(buf, 12, "%d", v);
    return buf;
}

int pl_labels_depend(int page)
{
    return (page >= 3 && page <= 7) || is_layer(page);
}

static const uint32_t PAGE_COLOR[12] = {0x6fc3ff, 0xff6f7d, 0xffb84d, 0xff7b47, 0xff9457,
                                        0xffd24d, 0xff9900, 0x7be382, 0xc79bff, 0x5fd3e6, 0xff9900, 0x7be382}; // слои - цветом своего синта

uint32_t pl_page_color(int page)
{
    return page >= 0 && page < N(PAGE_COLOR) ? PAGE_COLOR[page] : 0;
}

uint32_t pl_knob_color(int page, int enc, const uint8_t page_raw[16])
{
    if (!pl_knob_label(page, enc, page_raw)) {
        return 0;
    }
    switch (as_synth(page)) {
    case 0: // MIX: мастер/драйв, синты, ударка, пампинг/DJ-фильтр, панорама
        if (enc <= 2) return 0xffb547;
        if (enc <= 4) return 0x7be382;
        if (enc <= 8) return 0xff6f7d;
        if (enc <= 12) return 0x6fc3ff;
        return 0x5fd3e6;
    case 1: // DRUMS: бочка, хэты, клэп, малый, панорама
        if (enc <= 4) return 0xff6f7d;
        if (enc <= 7) return 0xffb84d;
        if (enc <= 9) return 0xc79bff;
        if (enc <= 12) return 0x6fc3ff;
        return 0x5fd3e6;
    case 2: // CYMBALS: крэш, райд, купол и велосити
        if (enc <= 4) return 0xff8844;
        if (enc <= 8) return 0xffc247;
        return 0x6fc3ff;
    case 3:
    case 4:
    case 5: // PERC: модель, высота/спад, тембр, грязь, выход/стерео
        if (enc == 1) return 0xffd24d;
        if (enc <= 5) return 0xff7b47;
        if (enc <= 8) return 0xffb547;
        if (enc <= 10) return 0xff5e6c;
        return 0x5fd3e6;
    case 6:
    case 7: { // SYNTH 1 & 2
        int plaits = model(page_raw, 25) != 0;
        if (enc == 1) return 0xffd24d;
        if (enc <= 6) return as_synth(page) == 6 ? 0xff9900 : 0x7be382;
        if (enc <= (plaits ? 8 : 9)) return 0x6fc3ff;
        if (enc <= 12) return 0xc79bff;
        return 0xff5e6c;
    }
    case 8: // FX: крашер, посылы, дилэй, статтер
        if (enc <= 2) return 0xff6f7d;
        if (enc <= 5) return 0xffb547;
        if (enc == 9) return 0x5fd3e6;
        return 0xc79bff;
    case 9: // TONAL: шагатель, лад/тоника, шаги 1..8
        if (enc <= 5) return 0x5fd3e6;
        if (enc <= 7) return 0xffb547;
        return 0x7be382;
    default:
        return 0;
    }
}

int pl_knob_bipolar(int page, int enc, const uint8_t page_raw[16])
{
    if (!pl_knob_label(page, enc, page_raw)) {
        return 0;
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
        if (model(page_raw, 25) == 0) {
            return enc == 9 || enc == 14;
        }
        return (enc >= 8 && enc <= 11) || enc == 14;
    case 9: return enc >= 8 && enc <= 15;
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
    case 9:
        if (page_raw[1] == 0) {
            return "OFF";
        }
        snprintf(buf, 24, "%s %s", SCALE[page_raw[6] < N(SCALE) ? page_raw[6] : 0],
                 ROOT[page_raw[7] < N(ROOT) ? page_raw[7] : 0]);
        return buf;
    default:
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
