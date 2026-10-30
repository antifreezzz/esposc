#include "samples_part.h"

#include "esp_log.h"
#include "esp_partition.h"

#define SUBTYPE 0x40

static const char *TAG = "samples";

const acid_bank_t *samples_part_open(void)
{
    static acid_bank_t bank;
    static esp_partition_mmap_handle_t handle;
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTYPE, "samples");
    if (!p) {
        ESP_LOGW(TAG, "no samples partition: cymbals off");
        return NULL;
    }
    const void *img;
    esp_err_t err = esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &img, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mmap %u bytes failed: %s, cymbals off", (unsigned)p->size, esp_err_to_name(err));
        return NULL;
    }
    if (!acid_bank_open(&bank, img, p->size)) {
        esp_partition_munmap(handle);
        ESP_LOGW(TAG, "no valid image in partition (flash it with esptool): cymbals off");
        return NULL;
    }
    ESP_LOGI(TAG, "%u notes, %u bytes", (unsigned)bank.hdr->nnotes, (unsigned)bank.hdr->size);
    return &bank;
}
