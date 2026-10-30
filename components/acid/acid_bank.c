#include "acid_bank.h"

#include <string.h>

_Static_assert(sizeof(acid_bank_hdr_t) == ACID_BANK_HDR_SIZE, "bank header layout");
_Static_assert(sizeof(acid_bank_note_t) == 4 && sizeof(acid_bank_layer_t) == 4 && sizeof(acid_bank_rr_t) == 8, "bank tables");

static const int16_t STEPS[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const int8_t INDEX_ADJ[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

int16_t acid_adpcm_step(acid_adpcm_t *st, uint8_t nibble)
{
    int step = STEPS[st->index];
    int diff = step >> 3;
    if (nibble & 4) {
        diff += step;
    }
    if (nibble & 2) {
        diff += step >> 1;
    }
    if (nibble & 1) {
        diff += step >> 2;
    }
    int pred = st->pred + ((nibble & 8) ? -diff : diff);
    pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
    int index = st->index + INDEX_ADJ[nibble & 7];
    st->index = (uint8_t)(index < 0 ? 0 : index > 88 ? 88 : index);
    st->pred = (int16_t)pred;
    return st->pred;
}

uint32_t acid_crc32(const uint8_t *p, size_t n)
{
    // Полубайтовая таблица: 64 байта, на 6 МБ образа при старте хватает.
    static const uint32_t T[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                   0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        c = (c >> 4) ^ T[c & 15];
        c = (c >> 4) ^ T[c & 15];
    }
    return c ^ 0xFFFFFFFFu;
}

size_t acid_bank_rr_bytes(uint32_t frames)
{
    size_t blocks = ((size_t)frames + ACID_BANK_BLOCK - 1) / ACID_BANK_BLOCK;
    return blocks * ACID_BANK_BLOCK_HDR + frames;
}

int acid_bank_open(acid_bank_t *b, const void *img, size_t len)
{
    memset(b, 0, sizeof *b);
    const uint8_t *p = img;
    if (len < ACID_BANK_HDR_SIZE) {
        return 0;
    }
    const acid_bank_hdr_t *h = img;
    if (h->magic != ACID_BANK_MAGIC || h->version != ACID_BANK_VERSION || h->rate != ACID_BANK_RATE) {
        return 0;
    }
    size_t size = h->size;
    size_t tables = ACID_BANK_HDR_SIZE + 4u * h->nnotes + 4u * h->nlayers + 8u * h->nrr;
    if (size > len || size < tables) {
        return 0;
    }
    if (acid_crc32(p + ACID_BANK_HDR_SIZE, size - ACID_BANK_HDR_SIZE) != h->crc32) {
        return 0;
    }
    const acid_bank_note_t *notes = (const void *)(p + ACID_BANK_HDR_SIZE);
    const acid_bank_layer_t *layers = (const void *)(p + ACID_BANK_HDR_SIZE + 4u * h->nnotes);
    const acid_bank_rr_t *rrs = (const void *)(p + ACID_BANK_HDR_SIZE + 4u * h->nnotes + 4u * h->nlayers);
    for (int i = 0; i < h->nnotes; i++) {
        if (notes[i].nlayers == 0 || (size_t)notes[i].first_layer + notes[i].nlayers > h->nlayers) {
            return 0;
        }
    }
    for (int i = 0; i < h->nlayers; i++) {
        if (layers[i].nrr == 0 || (size_t)layers[i].first_rr + layers[i].nrr > h->nrr) {
            return 0;
        }
    }
    for (int i = 0; i < h->nrr; i++) {
        // Кадров не больше байт образа: дальше размер считается без переполнения и на 32 битах.
        if (rrs[i].frames == 0 || rrs[i].frames > size || rrs[i].offset < tables ||
            (uint64_t)rrs[i].offset + acid_bank_rr_bytes(rrs[i].frames) > size) {
            return 0;
        }
    }
    b->img = p;
    b->hdr = h;
    b->notes = notes;
    b->layers = layers;
    b->rrs = rrs;
    return 1;
}

const acid_bank_note_t *acid_bank_note(const acid_bank_t *b, int note)
{
    if (b->hdr == NULL) {
        return NULL;
    }
    for (int i = 0; i < b->hdr->nnotes; i++) {
        if (b->notes[i].note == note) {
            return &b->notes[i];
        }
    }
    return NULL;
}
