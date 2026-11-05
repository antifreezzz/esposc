#include <stdlib.h>
#include <string.h>

#include "acid_petri.h"
#include "test.h"

extern const acid_petri_frame_t acid_petri_baked[];
extern const int acid_petri_nbaked;

// Зашитые таблицы - живая чашка, снятая хостом: звучат, без постоянной составляющей, меняются от таблицы к таблице.
TEST(baked_tables_alive)
{
    CHECK(acid_petri_nbaked == 32);
    int same = 0;
    for (int k = 0; k < acid_petri_nbaked; k++) {
        const acid_petri_frame_t *f = &acid_petri_baked[k];
        int pk = 0;
        long sum = 0;
        for (int i = 0; i < ACID_PETRI_TABLE; i++) {
            pk = abs(f->tab[i]) > pk ? abs(f->tab[i]) : pk;
            sum += f->tab[i];
        }
        CHECK(pk > 2000);
        CHECK(labs(sum) / ACID_PETRI_TABLE < 50);
        if (k) {
            same += memcmp(f->tab, acid_petri_baked[k - 1].tab, sizeof f->tab) == 0;
        }
    }
    CHECK(same == 0);
}

int main(void)
{
    RUN(baked_tables_alive);
    TEST_MAIN_END();
}
