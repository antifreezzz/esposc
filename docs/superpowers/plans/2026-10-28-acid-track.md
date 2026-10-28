# Acid track - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use `- [ ]`.

**Goal:** 64-bar looping acid track with hats, clap, 4 bass patterns, arrangement automation and per-cycle mutation.
**Architecture:** New pure-C modules in `components/acid/`: `acid_noise` (PRNG + SVF), `acid_drums` (hat, clap), `acid_arr` (patterns, section table, automation, mutation). `acid_synth` drives them per step; LFO removed.
**Tech Stack:** C11, host CMake/ctest, ESP-IDF v6.1.
**Spec:** `docs/superpowers/specs/2026-10-28-acid-track-design.md`

## Global Constraints
- 48 kHz, peak <= 0.5012, 130 BPM, 16 steps/bar, 64 bars/cycle.
- `components/acid/` free of ESP-IDF headers; new .c files added to `components/acid/CMakeLists.txt`.
- ESP render < 7000 us per 480-frame block.

## Review Focus
1. Cycle boundary bar 63 -> bar 0: no click, mutation applied once per cycle (`test_track_loops_and_mutates`).
2. Mutation never moves rests or notes (`test_mutate_keeps_notes_and_rests`).
3. Break sections: no kick/clap energy (`test_track_break_has_no_kick`).
4. Transposed section pitch correct (`test_track_transpose`).
5. Long render (3 cycles) finite and within -6 dBFS (`test_track_long_render`).

### Task 1: Noise, SVF, hat, clap
Files: `acid_noise.{h,c}`, `acid_drums.{h,c}`, `host/test_drums.c`
Produces:
- `uint32_t acid_rand(uint32_t *state);` `float acid_noise(uint32_t *state);` in [-1,1]
- `typedef struct { float ic1, ic2; } acid_svf_t;` `void acid_svf_bp_hp(acid_svf_t *f, float in, float g, float k, float *bp, float *hp);` (TPT SVF, g = tan(pi fc/sr), k = 1/Q)
- `typedef struct {...} acid_hat_t;` `void acid_hat_init(acid_hat_t*, float sr);` `void acid_hat_trigger(acid_hat_t*, int open);` `float acid_hat_next(acid_hat_t*);` (closed decay 0.03 s, open 0.2 s, HP 7 kHz)
- `typedef struct {...} acid_clap_t;` `acid_clap_init/trigger/next` (BP 1.2 kHz, 3 bursts 10 ms apart, tail decay 0.15 s)
Tests: silent until trigger; closed hat RMS in 60-80 ms < 0.1x of 0-20 ms, open hat ratio > 0.3; hat rising zero crossings per 10 ms > 30; clap has >= 3 local energy peaks in first 35 ms; clap tail 400-500 ms RMS < 0.01.

### Task 2: Patterns, arrangement, mutation
Files: `acid_arr.{h,c}` (replaces use of `acid_pattern_default` in synth; keep the symbol as pattern A), `host/test_arr.c`
Produces:
- `enum { ACID_PAT_A, ACID_PAT_B, ACID_PAT_C, ACID_PAT_F, ACID_PAT_COUNT };`
- `typedef struct { uint8_t pattern, fill_pattern; int8_t transpose; uint8_t wave; uint8_t drums; float cut_from, cut_to, res_from, res_to, env_scale; } acid_section_t;` drums bits `ACID_DR_KICK=1, ACID_DR_HAT=2, ACID_DR_OHAT=4, ACID_DR_CLAP=8`; fill_pattern used on the 4th bar of the section when != pattern.
- `#define ACID_SECTION_BARS 4`, `#define ACID_SECTIONS 16`, `extern const acid_section_t acid_track[ACID_SECTIONS];`
- `const acid_section_t *acid_arr_section(int bar);` (bar wraps mod 64)
- `float acid_arr_progress(int bar, int step);` in [0,1) within section
- `void acid_mutate(acid_step_t *dst, const acid_step_t *src, int len, uint32_t seed);` flips ACCENT/SLIDE on ~2-3 non-rest steps, keeps notes/rests, accents stay in [2,6].
Tests: section lookup + wrap; progress 0 at section start and ~0.98 at last step; break sections have no KICK; mutate deterministic for same seed, different for different seed, notes and rests identical to src, >= 1 flag differs, accents in [2,6].

### Task 3: Synth drives the track
Modify `acid_synth.{h,c}`: global step counter `bar`, `step_in_bar`, `cycle`; per bar pick section + pattern (mutated copy per cycle, seed = cycle), transpose notes, wave; cutoff base = exp interpolation cut_from->cut_to by progress, res linear, env amount x env_scale; drums by section bits (kick on beats, closed hat every 2 steps, open hat on steps 2,6,10,14 when OHAT replacing closed there, clap on steps 4,12). Remove LFO. Keep `acid_synth_set_pattern` for tests: it pins a fixed pattern and disables arrangement (`s->arr_on = 0`), drums per `kick_on` as before.
Tests (`host/test_track.c`): break has no kick (energy at beat onsets in section 11 bars < 1% of section 13); transpose (section with +5: pitch at a non-slide note = midi(note+5)); loops and mutates (bar counter wraps 63->0, cycle increments, pattern copy differs between cycle 0 and 1); long render 3 cycles finite, peak <= 0.5012; all existing tests still pass.

### Task 4: ESP check and listening
Build, flash, read render time (< 7000 us), user listens. Commit.
