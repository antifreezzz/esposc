# esposc

**English** | [Русский](README.ru.md)

**A standalone groovebox on an ESP32-S3:** a drum machine plus two independent synths, played from an
Arturia BeatStep Pro over USB MIDI (the board is the USB host) and an optional 4" touch panel. Audio goes out
through a PCM5102A I2S DAC at 48 kHz stereo. With nothing plugged in it plays a built-in demo song.

**Sound**

- Two synths, each one of: a TB-303 style bass voice, any of the 24 Mutable Instruments Plaits models, or
  "Petri dish", a wavetable voice whose waveform is an orbit through a live Gray-Scott reaction-diffusion
  simulation running on the touch panel. Each synth has a second layer voice, glide, and its own step sequencer
  (8 bars of 16 steps).
- Drums: synthesized kick, snare, clap and hats with velocity; sampled crash/ride/bell (Naked Drums, IMA ADPCM
  from a separate flash partition); toms, rimshot and cowbell with five synthesis models each.
- Mutable Instruments Grids generator with a "drummer" that maps it to limbs, plus a bank of classic patterns,
  4 user patterns, swing and a triplet grid.
- Master section: sidechain-style pumping, DJ filter, drive, tempo-synced delay, stutter, stereo panning, and a
  tonal layer that transposes the synths through up to 8 steps per bar cycle.
- Four modulators (LFO shapes, S&H, drift, envelope; free-running, tempo-synced or triggered by notes and drums),
  up to 4 targets each; custom knob pages, macros, scenes and an XY pad.
- Every knob is remembered across power cycles (NVS).

**Engineering**

- Dual-core real-time rendering: 2 ms blocks, core 1 runs synth 1, drums, mix and FX; core 0 runs cymbals,
  percussion and synth 2 one chunk ahead. Voices sleep when silent, and a 3-stage load guard sheds work
  before the audio can underrun.
- The DSP core (`components/acid`) is plain C with no ESP-IDF dependency: it builds on a PC, renders the demo to
  WAV and has ~56 host test suites (`host/`).
- Touch panel: Guition ESP32-S3-4848S040 (480x480, LVGL 9), its own firmware in `panel/`, linked to the synth over
  a 1 Mbaud UART protocol (`components/panel_link`).

**Hardware:** ESP32-S3-DevKitC-1 N8R2, a USB hub with power delivery (BeatStep Pro goes into the hub, the hub
into the board's native USB port), PCM5102A DAC on GPIO 4/5/6 (BCK/WS/DIN), optional Guition 4848S040 panel on
UART1 (GPIO 17/18). Details and wiring: [README.ru.md](README.ru.md#железо).

**Build** (ESP-IDF 6.1):

```sh
idf.py build && idf.py -p /dev/ttyACM0 flash monitor     # synth firmware
idf.py -C panel -p /dev/ttyUSB0 -b 460800 flash           # touch panel firmware
cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build   # PC tests
host/build/acid_render --song metal out.wav 180           # render the demo to WAV
```

Never connect the board's native USB port to a PC: it is a host port powered by the hub. Flash through the
COM (UART bridge) port. The cymbal sample image is built and flashed separately, see [README.ru.md](README.ru.md#тарелки-сборка-и-заливка-набора).

**Docs:** the knob reference is [docs/controls.html](docs/controls.html) (visual, open in a browser); the 3D
enclosure is [docs/enclosure3d.html](docs/enclosure3d.html). The full documentation (every knob, page, panel
screen and internals) is in Russian: [README.ru.md](README.ru.md), machine translation handles it well. Issues and PRs in English are welcome.

**License:** GPLv3. Includes Mutable Instruments Grids (GPLv3) and Plaits/stmlib (MIT) by Emilie Gillet,
Naked Drums samples by Wilkinson Audio (CC-BY 4.0, not in the repo), Barlow fonts (SIL OFL 1.1).
