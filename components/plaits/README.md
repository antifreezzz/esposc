# Plaits

Копия DSP-части Plaits из https://github.com/pichenettes/eurorack (коммит f4cfa76) и нужной части stmlib.
Автор Emilie Gillet, лицензия MIT (текст в заголовке каждого файла).

Взято: `plaits/dsp`, `plaits/resources.*`, `plaits/user_data.h`, `stmlib/stmlib.h`, `stmlib/dsp`,
`stmlib/utils/{buffer_allocator.h,dsp.h,random.*}`. Собирается с `-DTEST` (переносимый режим без ассемблера ARM).

Наши правки помечены комментарием `acid:`:

- `plaits/dsp/fm/voice.h`: DX7 не сбрасывает фазу операторов на ноте (щелчок на голосе, который ещё звучит).
- `plaits/dsp/voice.cc`: chiptune идёт через LPG, иначе гейт его не глушит.
- `plaits/dsp/physical_modelling/resonator.h`: в `ResonatorSvf` деления заменены на `EspFastRcp` (битовый трюк
  и три шага Ньютона). Деление float на S3 - программный вызов, а тут их было 4 на сэмпл на modal:
  синт на modal подешевел на ~60 мкс на блок 2 мс. Звук совпадает с точностью float.

- `stmlib/dsp/dsp.h`: `EspRcp` и `EspRsqrt` (битовый трюк и три шага Ньютона, точность float). На них переведены
  `SoftLimit`, `Sqrt`, коэффициенты `Svf` и `OnePole` (`filter.h`), `Limiter`, `ParameterInterpolator`, наклоны в
  `variable_shape_oscillator.h` и `variable_saw_oscillator.h`, `grainlet_oscillator.h`, усиление в `noise_engine.cc`
  и `particle.h`. У FPU S3 нет ни деления, ни корня: оригинал рассчитан на Cortex-M4F, где они - одна инструкция.
- AUX не считается нигде (обёртка берёт только OUT): второй вариант в `bass_drum_engine.cc`, `snare_drum_engine.cc`,
  `hi_hat_engine.cc`, свободный осциллятор в `phase_distortion_engine.cc`, Z-осциллятор в `grain_engine.cc`, второй шум
  и полосовые в `noise_engine.cc`, синусы в `swarm_engine.h`, ФВЧ в `virtual_analog_vcf_engine.cc`, выход в
  `wave_terrain_engine.cc` (раньше - в VA, FM, waveshaping, additive).
- `plaits/dsp/voice.*`, `fx/low_pass_gate.h`: `Voice::Render` отдаёт float -1..1 (`ProcessFloat`), без 16-битной сетки
  под ЦАП модуля.
- `resonator.h`: `ResonatorSvf::Process` собран без превращения копий состояния в вызовы `memcpy`.

`acid_plaits.cc` - C-обёртка (строй: нота сдвинута на -0.0461 полутона, Plaits считает высоту от 47872.34 Гц своего
кодека, а играем на 48000): гейт ведёт LPG через level, триггер запускает внутреннюю огибающую.
