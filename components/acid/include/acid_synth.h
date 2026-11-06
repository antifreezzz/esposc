#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_drive.h"
#include "acid_drummer.h"
#include "acid_drums.h"
#include "acid_env.h"
#include "acid_beat.h"
#include "acid_clock.h"
#include "acid_cym.h"
#include "acid_filter.h"
#include "acid_fx.h"
#include "acid_kick.h"
#include "acid_midi.h"
#include "acid_osc.h"
#include "acid_perc.h"
#include "acid_petri.h"
#include "acid_plaits.h"
#include "acid_seq.h"
#include "acid_tonal.h"

#define ACID_TYPE_PETRI (1 + ACID_PLAITS_MODELS) // 25 - чашка Петри (у слоя нет)
#define ACID_TYPES (2 + ACID_PLAITS_MODELS)      // 0 - наш 303, 1..24 - модели Plaits, 25 - чашка Петри
#define ACID_LAYER_OFF ACID_TYPES                // тип слоя «выкл» (у синтов его нет)
#define ACID_KNOBS 16
// Страницы ручек внутри: 0-9 штатные, 10-11 слои синтов, 12 Grids, 13 чашка Петри; свои страницы - следом
// (acid_custom.h). На BSP номера другие: свои 10-15, слои 16-17, чашка 18, у Grids номера нет - перевод только
// на границе (энкодер 1).
#define ACID_PAGES 14
#define ACID_PAGE_LAYER1 10
#define ACID_PAGE_GRIDS 12
#define ACID_PAGE_PETRI 13
#define ACID_BSP_PETRI 18
#define ACID_BSP_USER_FIRST 10
#define ACID_BSP_LAYER1 16

// Синт-слот: тип синтеза, голоса 303 и Plaits, ручки по типам, стек нот.
// Слот 0 - синт 1 (SEQ1), слот 1 - синт 2 (SEQ2).
typedef struct {
    int idx;           // синт 0 или 1 (у слоя - его синт): тональность, каналы
    int type;          // звучащий тип
    int sel_type;      // выбранный тип (звучит после ухода текущего)
    int next_type;     // куда переходим после ухода звука
    int fade;          // смена типа: > 0 уход звука, < 0 приход, сэмплов осталось
    int half;          // слой на половинной частоте: считается каждый второй сэмпл
    int hph;           // фаза половинной частоты: 1 - этот сэмпл посчитан
    float hlast;       // последний посчитанный сэмпл слоя
    uint8_t knob[ACID_TYPES][ACID_KNOBS]; // сырые CC энкодеров по типам, 0xFF - не трогали

    acid_osc_t osc;
    acid_ladder_t flt;
    acid_drive_t drv;  // перегруз слота: энкодеры 14-16 у 303 и Plaits
    acid_decay_t fenv;
    acid_amp_t amp;
    float pitch_log2;  // текущая высота, log2(Гц)
    float target_log2;
    float glide_coef;
    int gliding;
    float accent_gain;
    float fenv_amount; // Гц на пике огибающей фильтра
    float res;
    int wave;          // 0 пила, 1 меандр
    float g;           // коэффициент фильтра с управляющей частоты
    acid_ladder_coefs_t lc; // величины ladder от g и res: в сэмпле без делений
    int ctrl_left;     // сэмплов до следующего пересчёта
    // Ручки: их крутят энкодеры (BSP или демо), до этого стартовые значения.
    float cut_base;    // базовый срез, Гц (в режиме трека его задаёт автоматизация)
    float env_hz;      // глубина огибающей фильтра обычной ноты, Гц
    float decay_s;     // спад огибающей фильтра обычной ноты
    float accent_amt;  // усиление громкости акцента
    float gain;        // громкость в миксе
    int transpose;     // полутоны
    // Live: ноты от MIDI.
    int8_t held[8];    // удерживаемые ноты, последняя сверху
    uint32_t held_at[8]; // когда нажата (now)
    int nheld;
    uint32_t now;      // сэмплов с init
    int pend_note;     // короткая нота с отложенным отпусканием (дребезг пэда), -1 если нет
    uint32_t pend_at;  // когда отпустить
    // Plaits.
    acid_plaits_t *plaits;
    acid_plaits_params_t pp;
    float pbuf[ACID_PLAITS_BLOCK];
    int ppos;          // позиция в pbuf
    int retrig;        // новая нота: опустить триггер на один блок
    int accented;      // текущая нота с акцентом
    float p_accent;    // насколько обычная нота тише акцентной, 0..0.75
    int idle;          // блоков тишины подряд: молчащий Plaits не считаем
    // Чашка Петри: голоса у синтов (у слоёв NULL - слой не бывает PETRI).
    acid_petri_t *pet;
    float pet_env;                  // глубина огибающей фильтра 0..1 (до 6 октав)
    float pet_cut_oct;              // log2 среза: считается при смене ручки, не в рендере
    float pet_mod[ACID_PETRI_MODS]; // модуляции звучащей таблицы, сглаженные
} acid_slot_t;

#define ACID_SLOTS 2

// Источники с ручкой панорамы. Бочка всегда в центре.
enum { ACID_PAN_SYNTH1, ACID_PAN_SYNTH2, ACID_PAN_HAT, ACID_PAN_CLAP, ACID_PAN_SNARE, ACID_PANS };

// Два синта и ударка, играют от MIDI (BSP или демо-трек). Поля публичные ради тестов, снаружи не трогать.
typedef struct {
    float sr;
    acid_slot_t slot[ACID_SLOTS];
    // Слои: второй голос каждого синта на его нотах, в его шине (панорама, посыл, громкость MIX - общие).
    // Оба слоя считаются на ядре 0 с синтом 2 (там запас): слой 1 - в его буфере, слой 0 - своим буфером,
    // сводится в шину синта 1. Тип ACID_LAYER_OFF не считается.
    acid_slot_t layer[ACID_SLOTS];
    acid_petri_t petri[ACID_SLOTS]; // голоса чашки Петри синтов 1 и 2
    float petri_depth[3];           // маршруты: пожиратели -> срез, мутаген -> резонанс, вихрь и прыжок -> огибающая
    float layer_gain[ACID_SLOTS]; // FX энкодеры 13-14
    int layer_half;    // FX энкодер 15 (64 и выше): слои на половинной частоте
    acid_kick_t kick;
    acid_hat_t hat;
    acid_clap_t clap;
    acid_snare_t snare;

    // Микс: его крутят ручки, до этого стартовые значения.
    float kick_gain, hat_gain, clap_gain, snare_gain;
    float drive, master;
    float kick_vel, hat_vel, clap_vel, snare_vel; // velocity последнего удара, 0..1
    // Звук ударки (ручки страницы 1): сеттеры голосов берут все параметры сразу.
    float kick_tune, kick_sweep, kick_pitch_s, kick_amp_s;
    float hat_tone, hat_closed_s, hat_open_s;
    float clap_tone, clap_tail_s;
    float snare_tune, snare_snappy, snare_decay_s;
    acid_pump_t pump;  // пампинг от ноты бочки (страница 0, энкодеры 10-11)
    acid_djf_t djf;    // DJ-фильтр мастера (страница 0, энкодеры 12-13)
    // Страница 8: хруст, дилэй, статтер по темпу MIDI clock.
    acid_clock_t clock;
    uint32_t clock_now; // сэмплов с init: время для тиков клока
    acid_crush_t crush;
    acid_delay_t delay;
    acid_stutter_t stutter;
    int16_t *fx_mem;   // буфер дилэя, выделяется один раз (s обнулён до первого init)
    float *st_mem;     // буфер статтера, так же
    float send1, send2, send_drums; // посылы в дилэй 0..1
    float pan_l[ACID_PANS], pan_r[ACID_PANS]; // усиления каналов по источникам
    int dl_time, dl_fb, dl_tone;    // сырые ручки дилэя
    acid_tonal_t tonal; // страница 9: круг транспозиций синтов по тактам
    // Тарелки (страница 2): набор сэмплов снаружи, NULL - молчат. Сырые ручки: крэш, райд.
    acid_cym_t cym;
    const acid_bank_t *bank;
    uint8_t cym_tail[2], cym_tune[2], cym_pan[2];
    acid_perc_t perc; // страницы 3-5: томы, рим, ковбелл
    // Ступень защиты: 0 нет, 1 перкуссия и тарелки на одном голосе, 2 и слои на половинной частоте, 3 и слои молчат.
    int tight, calm;  // ступень; спокойных блоков
    int hot;          // блоков подряд выше порога на ступени 1 или 2
    int calm_need, was2; // спокойных блоков для ухода со ступеней слоёв 2-3 (растёт при повторах); заходили на них с последнего 0
    float layer_duck[ACID_SLOTS]; // громкость слоёв от защиты: 0 - слой не считается
    float load_avg;   // сглаженная нагрузка блоков
    // Зеркало для панели: сырые значения страниц 0-2 и 8 (у синтов, перкуссии и тональности - свои knob[]).
    uint8_t raw[ACID_PAGES][ACID_KNOBS];
    // Автобарабаны (страница Grids): шаги от клока, барабанщик шлёт ноты в ударку на тике, не в рендере.
    acid_beat_t beat;
    acid_drummer_t drummer;
    int grids_step;      // шаг паттерна Grids 0..31 последнего сыгранного шага, -1 - выключен или стоп
    uint32_t grids_hits; // нот от Grids с init
    acid_seq_t *seq;     // встроенный секвенсер (живёт вне синта), NULL - нет (демо, дрон)
    uint32_t bsp_held[2][4]; // ноты, зажатые BSP на синте 1 и 2 (бит ноты): общую ноту отпускает последний
    int page; // 0 микс, 1 ударка, 2 ударка 2, 3 томы, 4 рим, 5 ковбелл, 6 синт 1, 7 синт 2, 8 эффекты, 9 тональность
    int drone; // режим дрона: оба синта непрерывно тянут ноту, её высоту ведёт страница 9 TONAL

    int save;          // сменили тип или ручку слоя тональности: память ручек пора записать
} acid_synth_t;

#define ACID_DRONE_NOTE 36 // базовая нота дрона по умолчанию (C2, 65.41 Гц)

// Память ручек для flash: метка, типы и сырые ручки обоих синтов.
#define ACID_SYNTH_BLOB (4 + 2 * ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS) // синты, слои

// Умолчания всех ручек, тишина до первой ноты.
void acid_synth_init(acid_synth_t *s, float sr);
// Режим дрона: 1 - запустить бесконечное звучание обоих синтов (и слоёв) под управлением 9 TONAL, 0 - отпустить.
void acid_synth_set_drone(acid_synth_t *s, int on);
// Номер страницы на экране BSP -> страница внутри и обратно; не страница - -1.
int acid_page_from_bsp(int v);
int acid_page_to_bsp(int page);
// Набор сэмплов тарелок; init сбрасывает в NULL, после init ставить заново.
void acid_synth_set_bank(acid_synth_t *s, const acid_bank_t *b);
// Нагрузка прошлого блока (время рендера / бюджет блока), звать между блоками. Среднее (~ACID_LOAD_AVG блоков) выше ACID_LOAD_HIGH
// перкуссия и тарелки уходят на один голос (лишние за 5 мс), назад - после ACID_LOAD_CALM блоков ниже
// ACID_LOAD_LOW: перегруз блока лавинный, лучше беднее звук, чем обрыв в тишину.
#define ACID_LOAD_HIGH 0.9f
#define ACID_LOAD_LOW 0.75f
#define ACID_LOAD_CALM 250 // 0.5 с блоками по 2 мс
#define ACID_LOAD_AVG 8.0f
#define ACID_LOAD_OVER 1.0f // среднее выше бюджета: перкуссия не спасёт, слои сразу на половинную частоту
#define ACID_LOAD_STEP 50 // блоков подряд выше порога - ступень выше (1 -> 2 половинная частота, 2 -> 3 слои молчат)
#define ACID_LOAD_SETTLE 25 // блоков на ступени 2, пока среднее не увидит половинную частоту: раньше не глушить
#define ACID_DUCK_DOWN (1.0f / 480.0f)  // за сэмпл: 10 мс до тишины
#define ACID_DUCK_UP (1.0f / 2400.0f)   // 50 мс назад
void acid_synth_set_load(acid_synth_t *s, float load);
void acid_synth_render(acid_synth_t *s, float *out, size_t n);
// Рендер по частям (слот 1 на другом ядре): сырой выход слота k (сейчас только k = 1),
// затем всё остальное с готовым слотом 1 из буфера. Вместе звучат как acid_synth_render.
void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n);
void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1);
// Стерео: два плоских буфера. Моно-версии выше - среднее каналов.
void acid_synth_render_lr(acid_synth_t *s, float *l, float *r, size_t n);
void acid_synth_render_with_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1);
// Тот же with_lr по ядрам, кусок n <= ACID_SPLIT_MAX. side - тарелки и перкуссия (их состояние трогает
// только он), src - синт 1 и ударка; side и src независимы и могут считаться одновременно на разных ядрах,
// mix сводит их со слотом 1 и прогоняет через эффекты. Вместе бит в бит как with_lr.
#define ACID_SPLIT_MAX 64
typedef struct {
    float cyl[ACID_SPLIT_MAX], cyr[ACID_SPLIT_MAX], pcl[ACID_SPLIT_MAX], pcr[ACID_SPLIT_MAX];
} acid_synth_side_t;
typedef struct {
    float y[ACID_SPLIT_MAX], kick[ACID_SPLIT_MAX], hat[ACID_SPLIT_MAX], clap[ACID_SPLIT_MAX], snare[ACID_SPLIT_MAX];
} acid_synth_src_t;
void acid_synth_render_side(acid_synth_t *s, acid_synth_side_t *side, size_t n);
void acid_synth_render_src(acid_synth_t *s, acid_synth_src_t *src, size_t n);
// layer0 - слой синта 1 (acid_synth_render_layer), NULL - нет.
void acid_synth_render_mix_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1, const float *layer0,
                              const acid_synth_src_t *src, const acid_synth_side_t *side);
// Слой синта k отдельно, уже с громкостью (k = 0: слой синта 1 на ядре 0 рядом с синтом 2).
void acid_synth_render_layer(acid_synth_t *s, int k, float *out, size_t n);
float acid_synth_pitch_hz(const acid_synth_t *s); // высота слота 0
// 1 и сброс флага, если с прошлого вызова сменили тип синтеза (пора записать память во flash).
int acid_synth_take_save(acid_synth_t *s);
// Память ручек в blob размера ACID_SYNTH_BLOB и обратно. import: 1 - применено, 0 - чужой blob.
size_t acid_synth_export(const acid_synth_t *s, void *buf, size_t cap);
int acid_synth_import(acid_synth_t *s, const void *buf, size_t len);
// s перед первым init должен быть обнулён (static): init создаёт голоса Plaits один раз.
// Отпустить все ноты обоих синтов (BSP переподключился, демо сменилось на BSP). Хвосты доигрывают.
void acid_synth_notes_off(acid_synth_t *s);
// Встроенный секвенсер: играет с тика клока в фазе событий. NULL - отключить. Позиция с начала песни.
void acid_synth_use_seq(acid_synth_t *s, acid_seq_t *q);
// ▶/■ секвенсера синта (0 или 1) с панели; стоп сразу отпускает его ноту.
void acid_synth_seq_run(acid_synth_t *s, int synth, int on);
// Событие MIDI (от BSP или демо): синт 1 канал 5, синт 2 канал 6, барабаны канал 2, CC с любого.
void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev);
// Ручка любой страницы как CC энкодера на ней, страницу BSP не меняет. enc с 0 (0 - энкодер 1, страница; игнор).
void acid_synth_set_knob(acid_synth_t *s, int page, int enc, int v);
// Таблица чашки Петри (с панели) обоим синтам: в фазе событий, как MIDI. Синт типа PETRI возьмёт её после
// текущего кроссфейда.
void acid_synth_petri(acid_synth_t *s, const acid_petri_frame_t *f);
// Действующее значение ручки 0..127 (как на экране BSP); нетронутая - умолчание. Вне диапазона - 0.
int acid_synth_knob(const acid_synth_t *s, int page, int enc);
