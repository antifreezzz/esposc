# Plaits

Копия DSP-части Plaits из https://github.com/pichenettes/eurorack (коммит f4cfa76) и нужной части stmlib.
Автор Emilie Gillet, лицензия MIT (текст в заголовке каждого файла).

Взято: `plaits/dsp`, `plaits/resources.*`, `plaits/user_data.h`, `stmlib/stmlib.h`, `stmlib/dsp`,
`stmlib/utils/{buffer_allocator.h,dsp.h,random.*}`. Собирается с `-DTEST` (переносимый режим без ассемблера ARM).

Наши правки помечены комментарием `acid:`:

- `plaits/dsp/fm/voice.h`: DX7 не сбрасывает фазу операторов на ноте (щелчок на голосе, который ещё звучит).
- `plaits/dsp/voice.cc`: chiptune идёт через LPG, иначе гейт его не глушит.

`acid_plaits.cc` - C-обёртка: гейт ведёт LPG через level, триггер запускает внутреннюю огибающую.
