#include "acid_mem.h"
#include "test.h"

#include <stdint.h>

TEST(test_mem_big_zeroed)
{
    size_t n = 1 << 20;
    const uint8_t *p = acid_mem_big(n);
    CHECK(p != NULL);
    size_t nz = 0;
    for (size_t i = 0; p && i < n; i++) {
        nz += p[i] != 0;
    }
    CHECK(nz == 0);
}

int main(void)
{
    RUN(test_mem_big_zeroed);
    TEST_MAIN_END();
}
