#include <math.h>
#include <stdio.h>
#include <string.h>

#include "acid.h"
#include "acid_demo.h"
#include "acid_link.h"
#include "acid_prof.h"
#include "acid_scene.h"
#include "audio_out.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "midi_in.h"
#include "nvs.h"
#include "panel_uart.h"
#include "pl_load.h"
#include "samples_part.h"
#include "store.h"
#include "usb_host_task.h"

#define SR 48000
#define BLOCK 96 // 2 мс: короткий блок - меньше задержка от ноты до звука
#define BLOCK_US (BLOCK * 1000000 / SR)
#define CHUNK 32  // MIDI применяется между кусками; рендер идёт рывками, так что реальный джиттер ~5 мс
#define STATS_PERIOD_US (10 * 1000 * 1000)
#define QUIET_BLOCKS 500   // 1 с тишины на выходе: можно писать во flash
#define QUIET_PEAK 1e-4f
#define FADE_BLOCKS 25 // 50 мс ухода громкости при смене демо <-> BSP
#define DEMO_EVENTS 96

static const char *TAG = "main";

// Счётчик загрузок и причины последних сбросов в NVS (своё пространство "diag", не store.h): переживает и
// пропадание питания. Видно, сколько раз плата перезапускалась, пока COM был выдернут и лога не было.
#define BOOT_HIST 8
static char s_boot_str[80];

static void boot_count(void)
{
    uint32_t boots = 0;
    uint8_t hist[BOOT_HIST] = {0};
    size_t len = sizeof hist;
    nvs_handle_t h;
    if (nvs_open("diag", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &boots);
        nvs_get_blob(h, "resets", hist, &len);
        memmove(hist + 1, hist, BOOT_HIST - 1);
        hist[0] = (uint8_t)esp_reset_reason();
        boots++;
        nvs_set_u32(h, "boots", boots);
        nvs_set_blob(h, "resets", hist, sizeof hist);
        nvs_commit(h);
        nvs_close(h);
    }
    int n = snprintf(s_boot_str, sizeof s_boot_str, "boot %lu, last resets", (unsigned long)boots);
    for (int i = 0; i < BOOT_HIST && i < (int)boots && n < (int)sizeof s_boot_str - 4; i++) {
        n += snprintf(s_boot_str + n, sizeof s_boot_str - n, " %d", hist[i]);
    }
}

static acid_synth_t s_synth;
static acid_demo_t s_demo;
static volatile int s_demo_on;   // играет демо: события от него, MIDI от BSP не слушаем
static volatile int s_demo_song; // номер песни в DEMO_SONGS или PL_DEMO_DRONE
static volatile int s_live;      // подключён MIDI-контроллер (BSP)
static int s_drone;              // режим дрона: тянутся оба синта, их ведёт страница 9 TONAL
static int s_drone_clk;          // кусков до следующего тика автономного клока дрона
static int s_seq_mode;           // режим СЕКВ без BSP: ручки пользователя, внутренний клок, секвенсер
static acid_seq_t s_seq;         // секвенсер: переживает смену режима, во flash (STORE_SEQ)
static acid_seq_clk_t s_seq_clk; // внутренний клок секвенсера, когда клока BSP нет
static int s_seq_save;           // правили секвенсер: записать в тишине
static acid_link_t s_link;       // панель: подхват BSP, глушение ручек демо
static acid_custom_t s_custom;   // свои страницы: переживают смену демо <-> live
static acid_mod_t s_mod;         // модуляторы: так же, во flash (STORE_MOD)
static acid_scene_t s_scene;     // сцены: так же
static const acid_bank_t *s_bank; // тарелки из флэша, NULL - молчат

// Ядро 0 на каждый кусок c: тарелки и перкуссия куска c (ядро 1 тем временем считает синт 1 и ударку c),
// затем синт 2 и оба слоя на кусок вперёд (c + 1, пока ядро 1 сводит c). События MIDI до синта 2 и слоёв доходят на кусок
// позже (0.67 мс). Тарелки и перкуссия - тот же кусок: звучат вместе с остальной ударкой.
static float s_slot1[2][CHUNK];
static float s_layer0[2][CHUNK]; // слой синта 1: тоже на ядре 0 и на кусок вперёд (на ядре 1 запаса нет)
static int s_slot1_next; // какой буфер считает ядро 0
static acid_synth_side_t s_side;
static TaskHandle_t s_synth_task;
static TaskHandle_t s_slot1_task;
static int64_t s_slot1_worst_us;
static int64_t s_slot1_sum_us;
// Статистика раз в 10 с печатается не из synth_task: строка в COM на 115200 блокирует ~15 мс,
// а в DMA ЦАПа запас 6-8 мс (недогруз каждые 10 с, слышно). Печать - задача стата на ядре 0.
typedef struct {
    uint32_t underruns, new_underruns, xfer_errors, petri;
    int64_t worst_us, mean_us, slot1_worst_us, slot1_mean_us;
} stats_msg_t;
static QueueHandle_t s_stats_q;
static pl_load_acc_t s_load; // период индикатора загрузки панели: ядро 0 копит в slot1_task, ядро 1 - в цикле

// Стенд замеров (сборка с -DESPOSC_STRESS=1): вместо демо стресс-песня, BSP не слушаем, защита от перегруза
// выключена, чтобы видеть полную цену худшего случая.
#ifdef ESPOSC_STRESS
#ifdef ESPOSC_STRESS_LAYERS
#define DEMO_SONGS {&acid_stress_layers_song} // слои не влезают целиком: здесь проверяется защита, она включена
#define GUARD 1
#elif defined(ESPOSC_STRESS_PETRI)
#define DEMO_SONGS {&acid_stress_petri_song}
#define GUARD 0
#else
#define DEMO_SONGS {&acid_stress_song}
#define GUARD 0
#endif
#define BENCH 1
#else
#define DEMO_SONGS {&acid_metal_song, &acid_demo_song} // порядок PL_DEMO_*: тап по статусу панели листает
#define BENCH 0
#define GUARD 1
#endif
static const acid_demo_song_t *const s_songs[] = DEMO_SONGS;
#define NSONGS (int)(sizeof s_songs / sizeof s_songs[0])
#define MODE_SEQ (NSONGS + 1) // круг режимов без BSP: песни, дрон (NSONGS), секвенсер
_Static_assert(BENCH || (NSONGS == PL_DEMO_DRONE && MODE_SEQ == PL_DEMO_SEQ), "mode numbers shared with the panel");

static void slot1_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        acid_synth_render_side(&s_synth, &s_side, CHUNK);
        xTaskNotifyGive(s_synth_task);
        acid_synth_render_slot(&s_synth, 1, s_slot1[s_slot1_next], CHUNK);
        acid_synth_render_layer(&s_synth, 0, s_layer0[s_slot1_next], CHUNK);
        int64_t dt = esp_timer_get_time() - t0;
        s_slot1_worst_us = dt > s_slot1_worst_us ? dt : s_slot1_worst_us;
        s_slot1_sum_us += dt;
        s_load.c0_sum_us += (uint32_t)dt;
        s_load.c0_worst_us = (uint32_t)dt > s_load.c0_worst_us ? (uint32_t)dt : s_load.c0_worst_us;
        s_load.chunks++;
        xTaskNotifyGive(s_synth_task);
    }
}

// Блок кусками по CHUNK, события MIDI (BSP или демо) перед каждым куском. Пока ядро 0 занято, ядро 1 трогает
// только своё состояние (синт 1, ударка, потом смесь и эффекты); к концу куска ядро 0 свободно.
// Событие связки панели и следом цели слота своей страницы.
static void to_panel(int n, acid_link_ev_t *lev)
{
    if (n) {
        panel_uart_send_knob(lev);
        while (acid_link_take(&s_link, lev)) {
            panel_uart_send_knob(lev);
        }
    }
}

// Правки своих страниц, целей модуляторов и сцены с панели, шаг перехода сцены. Перед каждым куском, как события MIDI.
static void custom_and_scenes(void)
{
    static uint8_t last_used, last_state;
    static int last_cur = -1;
    pl_custom_edit_t ed;
    while (panel_uart_receive_edit(&ed)) {
        int page = ed.pe == PL_NONE ? ACID_CUSTOM_NONE : ed.pe >> 4;
        acid_link_custom_edit(&s_link, ed.up, ed.enc, ed.idx, page, ed.pe & 15, ed.lo, ed.hi);
    }
    pl_mod_edit_t med;
    while (panel_uart_receive_mod_edit(&med)) {
        acid_link_mod_edit(&s_link, &s_synth, med.mod, med.idx, med.page, med.enc);
    }
    pl_scene_t sq;
    while (panel_uart_receive_scene(&sq)) {
        if (sq.op == PL_SCENE_STORE) {
            acid_scene_store(&s_scene, sq.n, &s_synth, &s_custom);
        } else {
            acid_scene_recall(&s_scene, sq.n, sq.bars);
        }
    }
    if (acid_scene_step(&s_scene, &s_link, &s_synth)) {
        panel_uart_snapshot_due(); // переход: снимок не чаще 100 мс и в конце
    }
    if (s_link.custom_due) {
        panel_uart_custom_due(s_link.custom_due);
        s_link.custom_due = 0;
    }
    if (s_link.mod_due) {
        panel_uart_mod_due(s_link.mod_due);
        s_link.mod_due = 0;
    }
    if (s_scene.used != last_used || s_scene.cur != last_cur || s_scene.state != last_state) {
        last_used = s_scene.used;
        last_cur = s_scene.cur;
        last_state = (uint8_t)s_scene.state;
        panel_uart_scene_due();
    }
}

// Правки секвенсера с панели: в фазе событий, как ручки. Такты и песня пишутся во всех режимах (и в демо),
// ▶/■ - только где секвенсер играет (СЕКВ и live).
static void seq_frames(void)
{
    uint8_t type, p[PANEL_SEQ_MAX];
    while (panel_uart_receive_seq(&type, p)) {
        switch (type) {
        case PL_SEQ_STEP: acid_seq_set_step(&s_seq, p[0], p[1], p[2], p[3]); break;
        case PL_SEQ_OCT: acid_seq_set_oct(&s_seq, p[0], p[1], p[2] - 2); break;
        case PL_SEQ_SCALE: acid_seq_set_scale(&s_seq, p[0], p[1], p[2]); break;
        case PL_SEQ_ROW: acid_seq_set_row(&s_seq, p[0], p[1], p + 2, p[2 + ACID_SEQ_CELLS]); break;
        case PL_SEQ_CTRL:
            acid_seq_set_edit(&s_seq, p[0], p[2]);
            if (p[0] < ACID_SEQ_SYNTHS && p[1] != PL_SEQ_KEEP && (p[1] ? 1 : 0) != s_seq.t[p[0]].run) {
                acid_synth_seq_run(&s_synth, p[0], p[1]); // без секвенсера в синте (демо, дрон) - ничего
            }
            panel_uart_seq_pos_due(); // ▶ в демо синт не взял: панель должна увидеть настоящий run
            break;
        case PL_SEQ_BPM: acid_seq_set_bpm(&s_seq, p[0]); break;
        default: break;
        }
        s_seq_save = 1;
    }
}

static void render_block(float *l, float *r)
{
    static acid_midi_event_t demo_ev[DEMO_EVENTS];
    static acid_synth_src_t src;
    for (int off = 0; off < BLOCK; off += CHUNK) {
        acid_midi_event_t ev;
        acid_link_ev_t lev;
        while (midi_in_receive(&ev)) {
            if (!s_demo_on) {
                if (ev.type == ACID_MIDI_CLOCK) {
                    acid_seq_clk_ext(&s_seq_clk, SR); // клок BSP жив: внутренний молчит
                }
                to_panel(acid_link_midi(&s_link, &s_synth, &ev, ACID_SRC_BSP, &lev), &lev);
            }
        }
        if (s_demo_on) {
            int n = acid_demo_events(&s_demo, demo_ev, DEMO_EVENTS);
            for (int i = 0; i < n; i++) {
                to_panel(acid_link_midi(&s_link, &s_synth, &demo_ev[i], ACID_SRC_DEMO, &lev), &lev);
            }
        } else if (s_drone && !s_live && ++s_drone_clk >= ACID_DEMO_TICK) {
            s_drone_clk = 0;
            acid_midi_event_t clk = {.type = ACID_MIDI_CLOCK};
            acid_synth_midi(&s_synth, &clk);
        } else {
            // Свой клок секвенсера: в режиме СЕКВ всегда, с BSP - пока его клок молчит. Считает каждый кусок:
            // молчание BSP отсчитывается и при стоящем секвенсере.
            int n = acid_seq_clk_run(&s_seq_clk, s_seq.bpm, CHUNK, SR);
            if (s_seq_mode || (s_live && (s_seq.t[0].run || s_seq.t[1].run))) {
                for (; n > 0; n--) {
                    acid_midi_event_t clk = {.type = ACID_MIDI_CLOCK};
                    acid_synth_midi(&s_synth, &clk);
                }
            }
        }
        pl_set_t set;
        while (panel_uart_receive(&set)) {
            to_panel(acid_link_panel_set(&s_link, &s_synth, set.page, set.enc, set.value, &lev), &lev);
        }
        int page;
        while (panel_uart_receive_page(&page)) {
            // В демо страницы листает само демо: экран их не трогает, иначе ручки демо ушли бы не туда.
            if (!s_demo_on) {
                to_panel(acid_link_panel_page(&s_link, &s_synth, page, &lev), &lev);
            }
        }
        custom_and_scenes();
        seq_frames();
        acid_petri_frame_t pf;
        if (panel_uart_receive_petri(&pf)) {
            acid_synth_petri(&s_synth, &pf); // оба ядра свободны: ящики голосов чашки
        }
        to_panel(acid_link_step(&s_link, &s_synth, &lev), &lev); // цели макро: не больше 4 ручек за кусок
        if (!s_demo_on) {
            acid_mod_step(&s_mod, &s_synth, ACID_LINK_PER_CHUNK - s_link.step_n);
        }
        if (s_link.snapshot_due) {
            s_link.snapshot_due = 0; // сменился тип синта: у типа своя память, страница новая целиком
            panel_uart_snapshot_due();
        }
        int ready = s_slot1_next;
        s_slot1_next ^= 1;
        xTaskNotifyGive(s_slot1_task);
        acid_synth_render_src(&s_synth, &src, CHUNK);
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY); // тарелки и перкуссия готовы
        acid_synth_render_mix_lr(&s_synth, l + off, r + off, CHUNK, s_slot1[ready], s_layer0[ready], &src, &s_side);
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY); // синт 2 следующего куска готов, ядро 0 свободно
    }
}

static float block_peak(const float *l, const float *r)
{
    float p = 0.0f;
    for (int i = 0; i < BLOCK; i++) {
        p = fabsf(l[i]) > p ? fabsf(l[i]) : p;
        p = fabsf(r[i]) > p ? fabsf(r[i]) : p;
    }
    return p;
}

// Громкость блока плавно от g0 к g1.
static void block_fade(float *l, float *r, float g0, float g1)
{
    for (int i = 0; i < BLOCK; i++) {
        float g = g0 + (g1 - g0) * (float)i * (1.0f / BLOCK);
        l[i] *= g;
        r[i] *= g;
    }
}

// Синт с нуля: с BSP или в автономном дроне - сохранённые ручки пользователя, иначе демо с начала.
static void enter_mode(int live, const uint8_t *blob, int blob_ok)
{
    s_live = live;
    if (!live) {
        s_drone = s_demo_song == NSONGS;
    }
    s_seq_mode = !live && s_demo_song == MODE_SEQ;
    acid_synth_init(&s_synth, (float)SR);
    acid_synth_set_bank(&s_synth, s_bank);
    if (live || s_drone || s_seq_mode) {
        if (blob_ok) {
            acid_synth_import(&s_synth, blob, ACID_SYNTH_BLOB);
        }
        s_demo_on = 0;
    } else {
        acid_demo_init(&s_demo, s_songs[s_demo_song]);
        s_demo_on = 1;
    }
    acid_synth_use_seq(&s_synth, live || s_seq_mode ? &s_seq : NULL); // позиция с начала песни
    acid_seq_clk_init(&s_seq_clk);
    if (s_seq_mode) {
        acid_midi_event_t st = {.type = ACID_MIDI_START}; // свой клок: общий такт и Grids с начала
        acid_synth_midi(&s_synth, &st);
    }
    acid_link_init(&s_link);
    acid_link_use_custom(&s_link, &s_custom);
    acid_link_use_mod(&s_link, &s_mod);
    if (s_drone) {
        acid_synth_set_drone(&s_synth, 1);
        if (!live) {
            acid_midi_event_t st = {.type = ACID_MIDI_START};
            acid_synth_midi(&s_synth, &st);
            s_drone_clk = ACID_DEMO_TICK - 1;
        }
    }
    s_scene.state = 0; // переход прежнего режима не доигрываем: синт с нуля
    panel_uart_snapshot_due(); // синт с нуля: панели всё заново
}

// Перед сменой режима (синт начнётся с нуля): ручки пользователя - в blob, несохранённые - во flash.
// В демо ручки не наши: blob остаётся прежним.
static void keep_user_knobs(uint8_t *blob, int *blob_ok, int *save_pending)
{
    if (s_demo_on) {
        return;
    }
    acid_synth_export(&s_synth, blob, ACID_SYNTH_BLOB);
    *blob_ok = 1;
    if (*save_pending || acid_synth_take_save(&s_synth)) {
        store_save(STORE_KNOBS, blob, ACID_SYNTH_BLOB); // звук ушёл: можно писать
        *save_pending = 0;
    }
}

static uint8_t s_cblob[ACID_CUSTOM_BLOB];
static uint8_t s_sblob[ACID_SCENE_BLOB];
static uint8_t s_qblob[ACID_SEQ_BLOB];
static uint8_t s_mblob[ACID_MOD_BLOB];

// До старта связи с панелью: она шлёт кадры своих страниц из s_custom.
static void load_user_data(void)
{
    acid_custom_init(&s_custom);
    acid_scene_init(&s_scene);
    if (store_load(STORE_CUSTOM, s_cblob, sizeof s_cblob) == ACID_CUSTOM_BLOB &&
        acid_custom_import(&s_custom, s_cblob, sizeof s_cblob)) {
        ESP_LOGI(TAG, "custom pages restored");
    }
    size_t sn = store_load(STORE_SCENES, s_sblob, sizeof s_sblob); // старый blob короче: import его переносит
    if (sn > 0 && acid_scene_import(&s_scene, s_sblob, sn)) {
        ESP_LOGI(TAG, "scenes restored (%02x)", s_scene.used);
    }
    acid_seq_init(&s_seq);
    size_t qn = store_load(STORE_SEQ, s_qblob, sizeof s_qblob); // версия 1 короче: длину проверяет import
    if (qn > 0 && acid_seq_import(&s_seq, s_qblob, qn)) {
        ESP_LOGI(TAG, "sequencer restored");
    }
    acid_mod_init(&s_mod, (float)CHUNK / (float)SR);
    if (store_load(STORE_MOD, s_mblob, sizeof s_mblob) == ACID_MOD_BLOB &&
        acid_mod_import(&s_mod, s_mblob, sizeof s_mblob)) {
        ESP_LOGI(TAG, "modulators restored");
    }
}

static void synth_task(void *arg)
{
    (void)arg;
    static float out_l[BLOCK], out_r[BLOCK];
    static int16_t pcm[BLOCK * 2];
    static uint8_t blob[ACID_SYNTH_BLOB];
    uint32_t last_underruns = 0;
    int64_t worst_us = 0, sum_us = 0;
    int blocks = 0;
    int64_t next_stats = esp_timer_get_time() + STATS_PERIOD_US;
    int64_t next_load = esp_timer_get_time() + PL_LOAD_MS * 1000;
    uint32_t load_underruns = 0;
    int live = 0;
    int fade = 0; // блоков ухода громкости до смены режима
    int next_song = 0; // уход громкости - ради следующей песни демо, режим тот же
    int next_mode = 0; // какой режим без BSP включить после ухода (next_song)
    int save_pending = 0;
    int quiet = 0;

    s_bank = samples_part_open();
    // blob - ручки пользователя: из flash и после каждого сохранения. Демо их не трогает.
    acid_synth_init(&s_synth, (float)SR);
    size_t bn = store_load(STORE_KNOBS, blob, sizeof blob); // прежняя версия короче: import её переносит
    int blob_ok = bn > 0 && acid_synth_import(&s_synth, blob, bn);
    if (blob_ok) {
        acid_synth_export(&s_synth, blob, sizeof blob); // дальше blob - в нынешнем формате (enter_mode)
        ESP_LOGI(TAG, "knobs restored");
    }
    int custom_pending = 0, scene_pending = 0, mod_pending = 0;
    int seq_pending = 0, last_seq_run = s_seq.t[0].run | s_seq.t[1].run << 1;
    uint8_t mode;
    if (!BENCH && store_load(STORE_MODE, &mode, 1) == 1 && mode <= MODE_SEQ) {
        s_demo_song = mode; // режим без BSP, выбранный тапом по статусу
    } else if (!BENCH) {
        s_demo_song = PL_DEMO_ACID; // без выбора - акид: в метале перегруз, артефакты на слух не отличить
    }
    ESP_LOGI(TAG, "mode %d (0 metal, 1 acid, 2 drone, 3 seq)", s_demo_song);
    enter_mode(0, blob, blob_ok);
    if (BENCH) {
        ESP_LOGW(TAG, "STRESS BENCH build: stress song, BSP ignored, overload guard %s", GUARD ? "on" : "off");
    }
    ESP_LOGI(TAG, "free: internal %u, psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    xTaskCreatePinnedToCore(slot1_task, "slot1", 4096, NULL, 6, &s_slot1_task, 0);
    for (;;) {
        if (!audio_out_connected()) {
            // Ждём свисток молча, без перебора в цикле.
            audio_out_wait_connected(portMAX_DELAY);
            ESP_LOGI(TAG, "speaker ready");
            acid_midi_event_t stale;
            while (midi_in_receive(&stale)) {
                // Пока свистка не было, накопились старые события: выбрасываем.
            }
            if (live && !s_drone) {
                acid_synth_notes_off(&s_synth); // вместе с событиями ушли и note off: чистим стек нот
            }
        }
        if (!BENCH && midi_in_connected() != live && fade == 0) {
            fade = FADE_BLOCKS; // уходим без щелчка, потом меняем режим
            next_song = 0;
        }
        if (!BENCH && panel_uart_take_demo() && fade == 0) {
            if (live) {
                s_drone = !s_drone;
                acid_synth_set_drone(&s_synth, s_drone);
                panel_uart_snapshot_due();
                ESP_LOGI(TAG, "live drone %d", s_drone);
            } else {
                fade = FADE_BLOCKS; // тап по статусу панели: следующая песня или дрон, синт с нуля
                next_song = 1;
                next_mode = (s_demo_song + 1) % (MODE_SEQ + 1);
            }
        }
        int want = BENCH ? -1 : panel_uart_take_mode(); // панель выбрала режим из списка
        if (want >= 0 && fade == 0 && pl_mode_ok(live, want)) {
            if (live) {
                if ((want == PL_DEMO_DRONE) != s_drone) {
                    s_drone = !s_drone;
                    acid_synth_set_drone(&s_synth, s_drone);
                    panel_uart_snapshot_due();
                    ESP_LOGI(TAG, "live drone %d", s_drone);
                }
            } else if (want != s_demo_song) {
                fade = FADE_BLOCKS;
                next_song = 1;
                next_mode = want;
            }
        }
        int64_t t0 = esp_timer_get_time();
        render_block(out_l, out_r);
        if (fade > 0) {
            block_fade(out_l, out_r, (float)fade / FADE_BLOCKS, (float)(fade - 1) / FADE_BLOCKS);
            if (fade == 1) {
                keep_user_knobs(blob, &blob_ok, &save_pending); // звук ушёл: синт сейчас начнётся с нуля
            }
            if (--fade == 0 && next_song) {
                s_demo_song = next_mode;
                uint8_t mode = (uint8_t)s_demo_song;
                store_save(STORE_MODE, &mode, 1); // звук ушёл: можно писать
                enter_mode(0, blob, blob_ok);
                ESP_LOGI(TAG, "demo mode %d", s_demo_song);
            } else if (fade == 0) {
                live = !live;
                enter_mode(live, blob, blob_ok);
                ESP_LOGI(TAG, "%s", live ? "midi controller: live mode" : "no midi controller: playing demo");
            }
        }
        acid_pcm_lr_to_s16(out_l, out_r, pcm, BLOCK);
        int64_t dt = esp_timer_get_time() - t0;
        worst_us = dt > worst_us ? dt : worst_us;
        sum_us += dt;
        blocks++;
        s_load.c1_sum_us += (uint32_t)dt;
        s_load.c1_worst_us = (uint32_t)dt > s_load.c1_worst_us ? (uint32_t)dt : s_load.c1_worst_us;
        s_load.blocks++;
        // Защита от лавины: тяжёлый блок - перкуссия и тарелки на одном голосе, пока не отпустит.
        int was_tight = s_synth.tight;
        acid_synth_set_load(&s_synth, GUARD ? (float)dt / (float)BLOCK_US : 0.0f);
        if (s_synth.tight != was_tight) {
            ESP_LOGW(TAG, "overload guard step %d (block %lld us)", s_synth.tight, dt);
        }

        // Поменяли ручку или тип: память ручек пишем, когда выход помолчит (flash стопорит кэш) или при смене режима.
        // Ручки демо не сохраняем.
        save_pending |= acid_synth_take_save(&s_synth) && !s_demo_on;
        // Свои страницы, модуляторы и сцены - действия пользователя, пишутся и в демо.
        custom_pending |= s_custom.save;
        mod_pending |= acid_mod_take_save(&s_mod);
        // Секвенсер: правки с панели и смена ▶ (Stop BSP тоже) - во flash, run переживает выключение.
        int seq_run = s_seq.t[0].run | s_seq.t[1].run << 1;
        seq_pending |= s_seq_save || seq_run != last_seq_run;
        s_seq_save = 0;
        last_seq_run = seq_run;
        scene_pending |= s_scene.save;
        s_custom.save = s_scene.save = 0;
        quiet = block_peak(out_l, out_r) < QUIET_PEAK ? quiet + 1 : 0;
        if (quiet >= QUIET_BLOCKS) {
            if (save_pending) {
                store_save(STORE_KNOBS, blob, acid_synth_export(&s_synth, blob, sizeof blob));
                blob_ok = 1;
                save_pending = 0;
            }
            if (custom_pending) {
                store_save(STORE_CUSTOM, s_cblob, acid_custom_export(&s_custom, s_cblob, sizeof s_cblob));
                custom_pending = 0;
            }
            if (mod_pending) {
                acid_mod_export(&s_mod, s_mblob);
                store_save(STORE_MOD, s_mblob, sizeof s_mblob);
                mod_pending = 0;
            }
            if (seq_pending) {
                store_save(STORE_SEQ, s_qblob, acid_seq_export(&s_seq, s_qblob, sizeof s_qblob));
                seq_pending = 0;
            }
            if (scene_pending) {
                store_save(STORE_SCENES, s_sblob, acid_scene_export(&s_scene, s_sblob, sizeof s_sblob));
                scene_pending = 0;
            }
        }

        if (audio_out_write(pcm, BLOCK, 50) == 0) {
            // Запись не прошла (устройство пропало или в ошибке): не крутиться вхолостую.
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (t0 >= next_load) {
            // Панели - загрузка за полсекунды. Счётчики ядра 0 сбрасываются между его кусками: ядро 0 сейчас ждёт.
            uint32_t u = audio_out_underruns();
            s_load.underruns = u - load_underruns;
            load_underruns = u;
            pl_load_t l;
            pl_load_pack(&l, &s_load, s_synth.tight);
            panel_uart_send_load(&l);
            float spb = acid_clock_spb(&s_synth.clock);
            panel_uart_set_bsp_bpm(s_seq_clk.ext_left > 0 && spb > 0.0f ? (int)(SR * 60.0f / spb + 0.5f) : 0);
            memset(&s_load, 0, sizeof s_load);
            next_load = t0 + PL_LOAD_MS * 1000;
        }
        if (t0 >= next_stats) {
            uint32_t u = audio_out_underruns();
            int64_t mean_us = blocks ? sum_us / blocks : 0;
            int64_t slot1_mean_us = blocks ? s_slot1_sum_us / (blocks * (BLOCK / CHUNK)) : 0;
            stats_msg_t m = {
                .underruns = u,
                .new_underruns = u - last_underruns,
                .xfer_errors = audio_out_transfer_errors(),
                .petri = panel_uart_petri_count(),
                .worst_us = worst_us,
                .mean_us = mean_us,
                .slot1_worst_us = s_slot1_worst_us,
                .slot1_mean_us = slot1_mean_us,
            };
            xQueueOverwrite(s_stats_q, &m);
#ifdef ACID_PROFILE
            if (blocks) {
                uint32_t div = (uint32_t)blocks * 240; // такты -> мкс на блок
                ESP_LOGI(TAG, "us/block: cym %lu perc %lu synth1 %lu drums %lu fx %lu",
                         (unsigned long)(acid_prof[ACID_PROF_CYM] / div), (unsigned long)(acid_prof[ACID_PROF_PERC] / div),
                         (unsigned long)(acid_prof[ACID_PROF_SYNTH1] / div), (unsigned long)(acid_prof[ACID_PROF_DRUMS] / div),
                         (unsigned long)(acid_prof[ACID_PROF_FX] / div));
            }
            for (int k = 0; k < ACID_PROF_N; k++) {
                acid_prof[k] = 0;
            }
#endif
            last_underruns = u;
            worst_us = sum_us = 0;
            blocks = 0;
            s_slot1_worst_us = s_slot1_sum_us = 0;
            next_stats = t0 + STATS_PERIOD_US;
        }
    }
}

// Что пришло по USB-MIDI за 10 с: каналы 1..16 с нотами или CC, клок, Start/Stop, потери очереди.
static void log_midi_in(void)
{
    static acid_midi_stats_t last;
    static uint32_t last_packets, last_dropped;
    acid_midi_stats_t st;
    uint32_t packets, dropped;
    midi_in_stats(&st, &packets, &dropped);
    char buf[200];
    int len = 0;
    for (int ch = 0; ch < 16 && len < (int)sizeof buf - 32; ch++) {
        uint32_t nt = st.notes[ch] - last.notes[ch], cc = st.cc[ch] - last.cc[ch];
        if (nt || cc) {
            len += snprintf(buf + len, sizeof buf - len, " ch%d n%lu c%lu", ch + 1, (unsigned long)nt,
                            (unsigned long)cc);
        }
    }
    ESP_LOGI(TAG, "midi in: packets %lu, dropped %lu, clock %lu, start %lu, stop %lu,%s",
             (unsigned long)(packets - last_packets), (unsigned long)(dropped - last_dropped),
             (unsigned long)(st.clock - last.clock), (unsigned long)(st.start - last.start),
             (unsigned long)(st.stop - last.stop), len ? buf : " no notes/cc");
    last = st;
    last_packets = packets;
    last_dropped = dropped;
}

static void stats_task(void *arg)
{
    (void)arg;
    stats_msg_t m;
    for (;;) {
        xQueueReceive(s_stats_q, &m, portMAX_DELAY);
        if (m.new_underruns || m.worst_us > BLOCK_US * 7 / 10) {
            ESP_LOGW(TAG, "underruns %lu (+%lu), xfer errors %lu, render worst %lld mean %lld us of %d, "
                     "core 0 worst %lld mean %lld us of %d, petri %lu",
                     (unsigned long)m.underruns, (unsigned long)m.new_underruns, (unsigned long)m.xfer_errors,
                     m.worst_us, m.mean_us, BLOCK_US, m.slot1_worst_us, m.slot1_mean_us, CHUNK * 1000000 / SR,
                     (unsigned long)m.petri);
        } else {
            ESP_LOGI(TAG, "ok, render worst %lld mean %lld us of %d, core 0 worst %lld mean %lld us of %d, petri %lu",
                     m.worst_us, m.mean_us, BLOCK_US, m.slot1_worst_us, m.slot1_mean_us, CHUNK * 1000000 / SR,
                     (unsigned long)m.petri);
        }
        ESP_LOGI(TAG, "%s (1 poweron or en, 3 sw, 4 panic, 5-7 wdt, 9 brownout, 14 glitch)", s_boot_str);
        log_midi_in();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(store_start());
    boot_count();
    ESP_LOGI(TAG, "%s", s_boot_str);
    load_user_data();
    usb_host_start();
    ESP_ERROR_CHECK(midi_in_start());
    ESP_ERROR_CHECK(audio_out_start());
    ESP_ERROR_CHECK(panel_uart_start(&s_synth, &s_link, &s_custom, &s_scene, &s_seq, &s_mod, &s_demo_on, &s_live,
                                     &s_demo_song));
    s_stats_q = xQueueCreate(1, sizeof(stats_msg_t));
    xTaskCreatePinnedToCore(stats_task, "stats", 3072, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(synth_task, "synth", 4096, NULL, 6, &s_synth_task, 1);
}
