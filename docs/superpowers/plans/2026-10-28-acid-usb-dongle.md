# Acid pattern via USB dongle - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32-S3 plays a looping 16-step acid pattern (saw/square, resonant ladder, accent, slide) through a UAC 1.0 USB dongle behind a USB hub.

**Architecture:** Pure-C synth core in `components/acid/` (no ESP-IDF includes), tested and rendered to WAV on the PC via a separate host CMake build. The ESP-IDF app in `main/` runs the core in a task and pushes int16 stereo frames to an `audio_out` interface implemented over `espressif/usb_host_uac`.

**Tech Stack:** C11, CMake + ctest (host), ESP-IDF v6.1 (`~/esp/esp-idf-v6.1`), managed components `espressif/usb` ^1.6, `espressif/usb_host_uac` ^1.5.

**Spec:** `docs/superpowers/specs/2026-10-28-acid-usb-dongle-design.md`

## Global Constraints

- Sample rate 48000 Hz, output S16_LE stereo; synth is mono, duplicated L=R.
- Synth peak never above -6 dBFS (|sample| <= 0.5012 in float before conversion).
- 130 BPM, 16 steps, step = 1/16 note.
- `components/acid/` must not include any ESP-IDF / FreeRTOS header.
- Code, identifiers, commit messages in English; comments may be RU, no em dashes.
- Git identity antifreezzz <me@kvatech.ru>; commit after each task.
- Native USB port is USB host only; console on UART0 (CH343, `/dev/ttyACM0`).
- Never plug the board's USB connector into the PC (hub variant, no IN-OUT bridge).

## Review Focus

1. Dongle unplugged and replugged mid-playback: log line, no crash, sound resumes (Task 9 manual check).
2. Max resonance + accent on consecutive steps: output stays finite and within -6 dBFS (Task 5 test `test_synth_stress_peak`).
3. Slide on step 15 wrapping to step 0: glide continues across loop boundary, no envelope retrigger (Task 4 test `test_seq_slide_wraps`).
4. Slide into a rest step: rest wins, gate closes, no glide to a stale note (Task 5 test `test_synth_slide_into_rest`).
5. Boot without dongle/hub connected: app waits quietly, no log flood, synth task does not spin (Task 9 manual check).

---

### Task 1: Host test harness, note table, oscillator

**Files:**
- Create: `components/acid/include/acid.h` (umbrella), `components/acid/include/acid_osc.h`, `components/acid/acid_osc.c`
- Create: `host/CMakeLists.txt`, `host/test.h`, `host/test_osc.c`
- Create: `components/acid/CMakeLists.txt` (`idf_component_register(SRCS ... INCLUDE_DIRS include)`, guarded so host build ignores it)

**Interfaces (produces):**
- `float acid_midi_to_hz(int note);` (A4 = 69 = 440 Hz)
- `typedef struct { float phase; float inc; } acid_osc_t;`
- `void acid_osc_set_freq(acid_osc_t *o, float hz, float sr);`
- `float acid_osc_saw(acid_osc_t *o);` / `float acid_osc_square(acid_osc_t *o);` both PolyBLEP, range [-1, 1], advance phase.
- `host/test.h`: `TEST(name)`, `CHECK(cond)`, `CHECK_NEAR(a, b, eps)`, `RUN(name)`, `TEST_MAIN_END()` returns nonzero on any failure. Each `host/test_*.c` is its own ctest executable.

- [ ] **Step 1: Write failing tests** in `host/test_osc.c`:
  - `test_midi_to_hz`: `CHECK_NEAR(acid_midi_to_hz(69), 440.0f, 0.01f)`, `CHECK_NEAR(acid_midi_to_hz(57), 220.0f, 0.01f)`, `CHECK_NEAR(acid_midi_to_hz(81), 880.0f, 0.02f)`.
  - `test_saw_frequency`: 110 Hz at 48000, render 48000 samples, count rising zero crossings = 110 +/- 1.
  - `test_square_frequency`: same with square, 110 +/- 1.
  - `test_osc_range`: 10 s at 2000 Hz, every sample in [-1.05, 1.05] (PolyBLEP overshoot allowed).
- [ ] **Step 2:** `cmake -S host -B host/build && cmake --build host/build && ctest --test-dir host/build` -> FAIL (undefined symbols).
- [ ] **Step 3:** Implement `acid_osc.c`.
- [ ] **Step 4:** Rerun ctest -> PASS.
- [ ] **Step 5:** Commit `feat(acid): oscillator, note table, host test harness`.

### Task 2: Envelopes

**Files:** Create `components/acid/include/acid_env.h`, `components/acid/acid_env.c`, `host/test_env.c`

**Interfaces (produces):**
- Decay env (filter): `typedef struct { float value; float coef; } acid_decay_t;`
  `void acid_decay_init(acid_decay_t *e, float decay_s, float sr);` (coef so value falls to 1/e in decay_s)
  `void acid_decay_set(acid_decay_t *e, float decay_s, float sr);`
  `void acid_decay_trigger(acid_decay_t *e, float level);` (value = level)
  `float acid_decay_next(acid_decay_t *e);`
- Amp env (gate): `typedef struct { float value; float att, rel; int gate; } acid_amp_t;`
  `void acid_amp_init(acid_amp_t *e, float attack_s, float release_s, float sr);`
  `void acid_amp_gate(acid_amp_t *e, int on);`
  `float acid_amp_next(acid_amp_t *e);` (one-pole toward 1 when gated, toward 0 when not)

- [ ] **Step 1: Failing tests:**
  - `test_decay_time_constant`: init 0.2 s @48k, trigger 1.0, after 9600 samples `CHECK_NEAR(value, 0.3679f, 0.01f)`.
  - `test_decay_retrigger`: mid-decay trigger 1.0 -> next value >= 0.99.
  - `test_amp_attack_release`: attack 0.003 s, release 0.01 s; gate on, after 0.02 s value > 0.99; gate off, after 0.05 s value < 0.01.
  - `test_amp_gate_held_no_retrigger`: gate on twice in a row -> value continuous (no drop to 0 between samples).
- [ ] **Step 2:** ctest -> FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** ctest -> PASS.
- [ ] **Step 5:** Commit `feat(acid): decay and gate envelopes`.

### Task 3: Ladder filter

**Files:** Create `components/acid/include/acid_filter.h`, `components/acid/acid_filter.c`, `host/test_filter.c`

**Interfaces (produces):**
- `typedef struct { float s[4]; } acid_ladder_t;`
- `void acid_ladder_reset(acid_ladder_t *f);`
- `float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr);`
  `res` in [0, 1]; ~0.95+ self-oscillates; cutoff clamped to [20, 0.45*sr]. 4-pole TPT/ZDF ladder with `tanhf` on input of feedback (implementer's choice of exact topology). Gain compensation so passband loss at high res is moderate.

- [ ] **Step 1: Failing tests** (measure RMS of steady-state sine after 0.1 s settle):
  - `test_ladder_passes_low`: res 0, cutoff 2000, 100 Hz sine amp 0.5 -> out RMS / in RMS in [0.8, 1.2].
  - `test_ladder_cuts_high`: res 0, cutoff 500, 8000 Hz sine -> ratio < 0.01 (>= 40 dB down).
  - `test_ladder_resonance_peak`: cutoff 1000, res 0.8: 1000 Hz sine ratio > 2x ratio at res 0.
  - `test_ladder_stable_max_res`: res 1.0, input saw 55 Hz amp 1.0, cutoff swept 20..20000 over 10 s -> all outputs finite and |y| < 4.
- [ ] **Step 2:** ctest -> FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** ctest -> PASS.
- [ ] **Step 5:** Commit `feat(acid): resonant ladder filter`.

### Task 4: Sequencer and default pattern

**Files:** Create `components/acid/include/acid_seq.h`, `components/acid/acid_seq.c`, `components/acid/acid_pattern.c`, `host/test_seq.c`

**Interfaces (produces):**
- `enum { ACID_REST = 1, ACID_ACCENT = 2, ACID_SLIDE = 4 };`
  `typedef struct { int8_t note; uint8_t flags; } acid_step_t;` (ACID_SLIDE on step i = glide from step i into step i+1, gate held)
- `typedef struct { const acid_step_t *steps; int len; double samples_per_step; double pos; int index; } acid_seq_t;`
- `void acid_seq_init(acid_seq_t *s, const acid_step_t *steps, int len, float bpm, float sr);`
- `int acid_seq_tick(acid_seq_t *s);` called once per sample; returns new step index at the first sample of a step, else -1. First call returns 0.
- `int acid_seq_gate(const acid_seq_t *s);` 1 while within first 50% of a non-rest step, or for the whole step if it has ACID_SLIDE and next (wrapping) step is not a rest.
- `const acid_step_t *acid_seq_next_step(const acid_seq_t *s);` (wraps)
- `extern const acid_step_t acid_pattern_default[16];` original pattern, around A minor, 3-5 accents, 2-4 slides, at least one slide on step 15 to exercise wrap, at least 2 rests.

- [ ] **Step 1: Failing tests:**
  - `test_seq_step_timing`: 130 BPM @48k -> samples_per_step = 48000*60/130/4 = 5538.46; over 16 steps tick returns 0..15 at sample offsets floor(k*5538.46) +/- 1, then 0 again.
  - `test_seq_rest_gate`: pattern with step 1 rest -> gate 0 throughout step 1.
  - `test_seq_gate_length`: normal step -> gate 1 for first ~2769 samples, 0 after.
  - `test_seq_slide_wraps`: 16-step pattern with SLIDE on step 15 -> gate stays 1 from step 15 through sample 0 of step 0; `acid_seq_next_step` at step 15 returns step 0.
  - `test_default_pattern_shape`: count accents in [3,5], slides in [2,4], rests >= 2, step 15 has SLIDE.
- [ ] **Step 2:** ctest -> FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** ctest -> PASS.
- [ ] **Step 5:** Commit `feat(acid): 16-step sequencer and default pattern`.

### Task 5: Synth voice and output conversion

**Files:** Create `components/acid/include/acid_synth.h`, `components/acid/acid_synth.c`, `components/acid/include/acid_pcm.h`, `components/acid/acid_pcm.c`, `host/test_synth.c`

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces:
  - `typedef struct { ... } acid_synth_t;` (public struct, fields are implementation detail)
  - `void acid_synth_init(acid_synth_t *s, float sr);` uses `acid_pattern_default`, 130 BPM, saw.
  - `void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len);` (for tests)
  - `void acid_synth_render(acid_synth_t *s, float *out, size_t n);` mono, peak <= 0.5012.
  - `float acid_synth_pitch_hz(const acid_synth_t *s);` current osc freq (for tests).
  - `void acid_pcm_to_s16_stereo(const float *in, int16_t *out, size_t frames);` clamp to [-1, 1], scale 32767 and round with `lrintf`, out has 2*frames samples L,R.

Voice values (my calls, tune by ear later): cutoff base 300 Hz, env mod up to +3000 Hz (accent +5000 Hz, decay 0.12 s vs 0.25 s normal), res 0.85, slide = one-pole glide with 60 ms time constant in log-frequency, accent gain 1.4x, LFO on cutoff base 200..1200 Hz exponential, period 12 bars, final output gain such that stress test peak holds -6 dBFS (soft clip with `tanhf` then scale 0.5 allowed).

- [ ] **Step 1: Failing tests:**
  - `test_synth_finite_and_peak`: render 60 s default pattern -> all finite, max |x| <= 0.5012, RMS > 0.02 (not silent).
  - `test_synth_stress_peak`: pattern all 16 steps ACCENT, note 36, res forced 1.0 -> 30 s, all finite, max <= 0.5012.
  - `test_synth_pitch_follows_step`: pattern of non-slide notes 45, 57 -> at mid-step 0 pitch ~110 Hz, mid-step 1 ~220 Hz (+/- 1%).
  - `test_synth_slide_glides`: step 0 note 45 SLIDE, step 1 note 57 -> 10 ms into step 1 pitch strictly between 110 and 220; amp envelope never drops below 0.5 across the boundary.
  - `test_synth_slide_into_rest`: step 0 note 45 SLIDE, step 1 REST -> output RMS over last half of step 1 < 0.001 (rest wins).
  - `test_pcm_convert`: {0, 0.5, -1, 2.0} -> stereo {0,0, 16384,16384, -32767,-32767, 32767,32767}.
- [ ] **Step 2:** ctest -> FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** ctest -> PASS.
- [ ] **Step 5:** Commit `feat(acid): acid voice and pcm conversion`.

### Task 6: WAV render tool and dongle check on PC

**Files:** Create `host/render_wav.c` (target `acid_render` in `host/CMakeLists.txt`)

**Interfaces:** Consumes `acid_synth_*`, `acid_pcm_to_s16_stereo`. CLI: `acid_render <out.wav> <seconds>`; writes 48 kHz S16 stereo RIFF.

- [ ] **Step 1: Failing test** `host/test_wav.c` `test_wav_header`: call `acid_write_wav(path, frames, n)` (exported from `host/wav.c`, `int acid_write_wav(const char *path, const int16_t *stereo, size_t frames)`) for 480 frames -> file size 44 + 1920, bytes 0-3 "RIFF", 8-11 "WAVE", sample rate field 48000, channels 2, bits 16.
- [ ] **Step 2:** ctest -> FAIL. **Step 3:** implement `host/wav.c` + `render_wav.c`. **Step 4:** ctest -> PASS.
- [ ] **Step 5:** `host/build/acid_render /tmp/.../acid.wav 30` then `aplay -D plughw:CARD=Audio acid.wav` with dongle in hub's USB-A, hub in PC. User listens: acid pattern, no hum/clicks. Record result in commit message.
- [ ] **Step 6:** Commit `feat(host): wav renderer`.

### Task 7: ESP-IDF toolchain and USB host skeleton

**Files:** Create `CMakeLists.txt` (top-level IDF project `esposc`), `main/CMakeLists.txt`, `main/main.c`, `main/idf_component.yml`, `sdkconfig.defaults`, `README.md` (build/flash commands, hardware wiring, IN-OUT warning)

**Interfaces (produces):** `main/usb_host_task.c`: `void usb_host_start(void);` installs USB Host Library, runs `usb_host_lib_handle_events` in its own task, logs every new device (VID:PID, class) via `usb_host_client` new-device callback.

- [ ] **Step 1:** Install toolchain: `~/esp/esp-idf-v6.1/install.sh esp32s3` (needs `python3-venv`; if missing, ask user to `sudo apt install python3-venv`). Verify `. ~/esp/esp-idf-v6.1/export.sh && idf.py --version` -> `v6.1`.
- [ ] **Step 2:** `sdkconfig.defaults`: `CONFIG_IDF_TARGET="esp32s3"`, `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`, `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `CONFIG_USB_HOST_HUBS_SUPPORTED=y`. `idf_component.yml`: `espressif/usb: "^1.6"`, `espressif/usb_host_uac: "^1.5"`.
- [ ] **Step 3:** `idf.py set-target esp32s3 && idf.py build` -> success.
- [ ] **Step 4:** `idf.py -p /dev/ttyACM0 flash monitor`, hub on board USB with charger in hub PD port, dongle in hub USB-A. Expected log: `05e3:0610` hub, `0bda:8152`, `001f:0b21`. If no devices at all: hub gives no VBUS upstream -> stop, report to user (fallback: IN-OUT bridge).
- [ ] **Step 5:** Commit `feat: esp-idf usb host skeleton with hub support` (include observed log lines).

### Task 8: audio_out over UAC, 440 Hz test tone

**Files:** Create `main/audio_out.h`, `main/audio_out_uac.c`; modify `main/main.c`

**Interfaces (produces):**
- `esp_err_t audio_out_start(void);` installs `uac_host`, waits for speaker device (UAC_HOST_DRIVER_EVENT_TX_CONNECTED), opens it, starts stream 48000/2ch/16bit, sets volume to a conservative level (start ~50%, verify by ear; dongle reports odd range -11520..0).
- `size_t audio_out_write(const int16_t *stereo, size_t frames, uint32_t timeout_ms);` blocks up to timeout, returns frames accepted; returns 0 when no device is connected.
- `int audio_out_connected(void);`
- `uint32_t audio_out_underruns(void);`

- [ ] **Step 1:** In `main.c` generate 440 Hz sine at -12 dBFS in 480-frame blocks with `audio_out_write`.
- [ ] **Step 2:** Build, flash, monitor. Expected: log of opened stream 48000 Hz 2 ch 16 bit; user hears steady 440 Hz with no clicks for 1 minute.
- [ ] **Step 3:** Commit `feat: uac audio output, 440 Hz test tone`.

### Task 9: Acid on ESP, robustness

**Files:** Modify `main/main.c`, `main/CMakeLists.txt` (REQUIRES acid)

**Interfaces:** Consumes `acid_synth_*`, `acid_pcm_to_s16_stereo`, `audio_out_*`.

- [ ] **Step 1:** Synth task (core 1): render 480-frame blocks, convert, `audio_out_write(..., 50)`. When `!audio_out_connected()`, block on a connection semaphore/event instead of looping (Review Focus 5). Log underruns every 10 s only if the count changed.
- [ ] **Step 2:** Disconnect handling in `audio_out_uac.c`: on `UAC_HOST_DRIVER_EVENT_DISCONNECTED` close device, log once, return to waiting; reconnect reopens (Review Focus 1).
- [ ] **Step 3:** Build, flash. Manual checks with user: acid pattern audible; underruns 0 over 3 min; unplug dongle -> one log line, replug -> sound returns; boot with hub unplugged -> quiet log, plug in -> plays.
- [ ] **Step 4:** Commit `feat: acid pattern through usb dongle`.
