#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_drums.h"
#include "acid_env.h"
#include "acid_clock.h"
#include "acid_cym.h"
#include "acid_filter.h"
#include "acid_fx.h"
#include "acid_kick.h"
#include "acid_midi.h"
#include "acid_osc.h"
#include "acid_perc.h"
#include "acid_plaits.h"
#include "acid_tonal.h"

#define ACID_TYPES (1 + ACID_PLAITS_MODELS) // 0 - наш 303, 1..24 - модели Plaits
#define ACID_KNOBS 16

// Синт-слот: тип синтеза, голоса 303 и Plaits, ручки по типам, стек нот.
// Слот 0 - синт 1 (SEQ1), слот 1 - синт 2 (SEQ2).
typedef struct {
    int type;          // звучащий тип
    int sel_type;      // выбранный тип (звучит после ухода текущего)
    int next_type;     // куда переходим после ухода звука
    int fade;          // смена типа: > 0 уход звука, < 0 приход, сэмплов осталось
    uint8_t knob[ACID_TYPES][ACID_KNOBS]; // сырые CC энкодеров по типам, 0xFF - не трогали

    acid_osc_t osc;
    acid_ladder_t flt;
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
} acid_slot_t;

#define ACID_SLOTS 2

// Источники с ручкой панорамы. Бочка всегда в центре.
enum { ACID_PAN_SYNTH1, ACID_PAN_SYNTH2, ACID_PAN_HAT, ACID_PAN_CLAP, ACID_PAN_SNARE, ACID_PANS };

// Два синта и ударка, играют от MIDI (BSP или демо-трек). Поля публичные ради тестов, снаружи не трогать.
typedef struct {
    float sr;
    acid_slot_t slot[ACID_SLOTS];
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
    int tight, calm;  // защита от перегруза: перкуссия и тарелки на одном голосе; спокойных блоков подряд
    int page; // 0 микс, 1 ударка, 2 ударка 2, 3 томы, 4 рим, 5 ковбелл, 6 синт 1, 7 синт 2, 8 эффекты, 9 тональность

    int save;          // сменили тип или ручку слоя тональности: память ручек пора записать
} acid_synth_t;

// Память ручек для flash: метка, типы и сырые ручки обоих синтов.
#define ACID_SYNTH_BLOB (4 + ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS)

// Умолчания всех ручек, тишина до первой ноты.
void acid_synth_init(acid_synth_t *s, float sr);
// Набор сэмплов тарелок; init сбрасывает в NULL, после init ставить заново.
void acid_synth_set_bank(acid_synth_t *s, const acid_bank_t *b);
// Нагрузка прошлого блока (время рендера / бюджет блока), звать между блоками. Выше ACID_LOAD_HIGH
// перкуссия и тарелки уходят на один голос (лишние за 5 мс), назад - после ACID_LOAD_CALM блоков ниже
// ACID_LOAD_LOW: перегруз блока лавинный, лучше беднее звук, чем обрыв в тишину.
#define ACID_LOAD_HIGH 0.9f
#define ACID_LOAD_LOW 0.75f
#define ACID_LOAD_CALM 250 // 0.5 с блоками по 2 мс
void acid_synth_set_load(acid_synth_t *s, float load);
void acid_synth_render(acid_synth_t *s, float *out, size_t n);
// Рендер по частям (слот 1 на другом ядре): сырой выход слота k (сейчас только k = 1),
// затем всё остальное с готовым слотом 1 из буфера. Вместе звучат как acid_synth_render.
void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n);
void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1);
// Стерео: два плоских буфера. Моно-версии выше - среднее каналов.
void acid_synth_render_lr(acid_synth_t *s, float *l, float *r, size_t n);
void acid_synth_render_with_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1);
float acid_synth_pitch_hz(const acid_synth_t *s); // высота слота 0
// 1 и сброс флага, если с прошлого вызова сменили тип синтеза (пора записать память во flash).
int acid_synth_take_save(acid_synth_t *s);
// Память ручек в blob размера ACID_SYNTH_BLOB и обратно. import: 1 - применено, 0 - чужой blob.
size_t acid_synth_export(const acid_synth_t *s, void *buf, size_t cap);
int acid_synth_import(acid_synth_t *s, const void *buf, size_t len);
// s перед первым init должен быть обнулён (static): init создаёт голоса Plaits один раз.
// Отпустить все ноты обоих синтов (BSP переподключился, демо сменилось на BSP). Хвосты доигрывают.
void acid_synth_notes_off(acid_synth_t *s);
// Событие MIDI (от BSP или демо): синт 1 канал 5, синт 2 канал 6, барабаны канал 2, CC с любого.
void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev);
