#pragma once
#include <stddef.h>
#include <stdint.h>

// Образ набора сэмплов (тарелки). Собирает tools/samples/build_samples.py, читает прошивка из раздела
// флэша или хост из файла. Всё little-endian, смещения от начала образа.
// Порядок: заголовок, notes[nnotes], layers[nlayers], rrs[nrr], данные (выровнены на 4).
#define ACID_BANK_MAGIC 0x4B4E4241u // "ABNK"
#define ACID_BANK_VERSION 1
#define ACID_BANK_RATE 48000
#define ACID_BANK_BLOCK 256      // кадров в блоке ADPCM
#define ACID_BANK_BLOCK_HDR 8    // L: int16 pred, uint8 index, uint8 0; R то же
#define ACID_BANK_HDR_SIZE 28

typedef struct {
    uint32_t magic, version, size, crc32, rate; // crc32 (IEEE, как zlib) по байтам [28, size)
    uint16_t nnotes, nlayers, nrr, pad;
} acid_bank_hdr_t;

typedef struct {
    uint8_t note, nlayers; // нота GM, слоёв
    uint16_t first_layer;  // слои по возрастанию vel_hi
} acid_bank_note_t;

typedef struct {
    uint8_t vel_hi, nrr; // верх velocity слоя 1..127, повторов
    uint16_t first_rr;
} acid_bank_layer_t;

// Звук повтора: блоки по 256 кадров (последний короче). Блок: заголовок с состоянием декодера ДО блока,
// дальше байт на кадр: младший полубайт L, старший R. Стерео, IMA ADPCM 4 бита.
typedef struct {
    uint32_t offset, frames;
} acid_bank_rr_t;

typedef struct {
    const uint8_t *img;
    const acid_bank_hdr_t *hdr;
    const acid_bank_note_t *notes;
    const acid_bank_layer_t *layers;
    const acid_bank_rr_t *rrs;
} acid_bank_t;

// 1 - образ годен (магия, версия, размер, CRC, частота, таблицы и блоки в границах). img выровнен на 4.
int acid_bank_open(acid_bank_t *b, const void *img, size_t len);
// NULL - такой ноты в наборе нет.
const acid_bank_note_t *acid_bank_note(const acid_bank_t *b, int note);
// Байт данных повтора: ceil(frames / 256) блоков.
size_t acid_bank_rr_bytes(uint32_t frames);
uint32_t acid_crc32(const uint8_t *p, size_t n);

typedef struct {
    int16_t pred;
    uint8_t index;
} acid_adpcm_t;

// Стандартный декодер IMA ADPCM: полубайт -> следующий отсчёт.
int16_t acid_adpcm_step(acid_adpcm_t *st, uint8_t nibble);

extern const int16_t acid_adpcm_steps[89];
extern const int8_t acid_adpcm_adj[8];
// Таблица горячего цикла: для (индекс шага, полубайт) приращение со знаком и следующий индекс.
extern int32_t acid_adpcm_diff[89 * 16];
extern uint8_t acid_adpcm_nexti[89 * 16];
// Заполнить таблицу (один раз, до рендера; повторный вызов ничего не делает).
void acid_adpcm_init_lut(void);

// То же, что acid_adpcm_step, по таблице: две загрузки и ограничение вместо арифметики шага.
static inline int acid_adpcm_next(int *pred, int *index, int nib)
{
    int k = (*index << 4) | nib;
    int p = *pred + acid_adpcm_diff[k];
    p = p > 32767 ? 32767 : p;
    p = p < -32768 ? -32768 : p;
    *index = acid_adpcm_nexti[k];
    *pred = p;
    return p;
}
