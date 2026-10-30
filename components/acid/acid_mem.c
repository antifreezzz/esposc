#include "acid_mem.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

void *acid_mem_big(size_t bytes, int fast)
{
#ifdef ESP_PLATFORM
    uint32_t first = fast ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
    uint32_t second = fast ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    void *p = heap_caps_calloc(1, bytes, first | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(1, bytes, second | MALLOC_CAP_8BIT);
#else
    (void)fast;
    return calloc(1, bytes);
#endif
}
