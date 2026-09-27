### NTS-3 kaoss pad "WT" — Wavetable Oscillator Unit

A single-cycle **wavetable oscillator** for the NTS-3 kaoss pad, built as a `genericfx` unit. The audio input is ignored: the unit is a sound generator and simply replaces it (`out = wet`). Its distinguishing feature is the **build-time wavetable baker**: every `.wav` dropped into `wt/` is folded down to a single cycle, mip-mapped, quantized to `int16` and emitted as a generated C source. Each wav becomes **one build variant** (its own `.nts3unit`), so a folder of samples turns into a folder of units from a single `make`.

---

### 1. Parameters (8 slots, all of them)

| # | Parameter | Range | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | PITCH | `0`–`1023` | `k_unit_param_type_midi_note` | `360` (≈A2) | Base pitch. DSP reads `note = value * 127 / 1023` (arpeggiator convention), 0–127 → 8.27 Hz – 12.5 kHz. |
| **2** | MORPH | `0`–`1023` | `k_unit_param_type_none` | `512` | Wave position. `0` = darkest level the current pitch allows, `1023` = the full baked cycle. Crossfades adjacent mip levels, so the whole sweep is alias-free. |
| **3** | DETUNE | `-1200`–`1200` | `k_unit_param_type_cents` | `8` | Unison spread. Two wavetable oscillators, one per output channel, detuned by ±`DETUNE`/2 cents. `0` = mono. |
| **4** | SUB | `0`–`1023` | `k_unit_param_type_none` | `384` | Level of a band-limited square sub-oscillator (`osc_bl2_sqrf`) one octave below PITCH. |
| **5** | CUTOFF | `0`–`1023` | `k_unit_param_type_none` | `640` | Low-pass cutoff, exponential scale 25 Hz – 12.5 kHz, then multiplied by the envelope (up to 7x) and clamped to 100 Hz – 18 kHz. Default-mapped to the **X axis** of the KAOSS pad. |
| **6** | RESON | `0`–`1023` | `k_unit_param_type_none` | `300` | Filter resonance, feedback up to ~3.2. Default-mapped to the **Y axis**. |
| **7** | DECAY | `0`–`4000` | `k_unit_param_type_msec` | `1200` | Amp envelope decay time in ms (2 ms attack, 30 ms release are fixed). The filter follows the same envelope as a cutoff multiplier, so a low CUTOFF stays dark through the attack and closes again on release. |
| **8** | MIX | `-1024`–`1024` | `k_unit_param_type_drywet` | `0` (BAL) | Dry/wet balance. Default-mapped to the **DEPTH** knob. Kept bipolar so a value can be pushed fully wet either way. |

Descriptor rows use `{min, max, center, init, type, frac, frac_mode, reserved, {"NAME"}}`; only slots 1–8 are used (`num_params = 8`), and every mapping's `min/max/value` stays inside its descriptor range (the `DECAY` descriptor really is `0..4000`, unlike the acid unit).

Pad/knob defaults in `header.c`: `X → CUTOFF (0..1023, 640)`, `Y → RESON (0..1023, 300)`, `DEPTH → MIX (-1024..1024, 0, linear/unipolar)`, everything else `k_genericfx_param_assign_none` with the descriptor range (encoders only). Optional build switch `-DWTPAD_PITCH` swaps `X` to `PITCH` for a theremin-style playable unit.

---

### 2. Wavetable baking (`tools/wav2table.py`)

The `.wav` file is the source of truth; the baked C is a build artifact (`**/build` is git-ignored). No numpy, no external audio tools — pure Python 3 stdlib, deterministic output.

#### 2.1 Source layout

```
wavetable/wt/*.wav        <- one wav per build variant (bass.wav, …)
wavetable/tools/wav2table.py
wavetable/build/gen/<stem>/wt_data.h    <- generated
wavetable/build/gen/<stem>/wt_data.c    <- generated
```

`<stem>` is the file name without extension and is used verbatim as the variant name, the `-DWT_NAME` macro value and the on-device display name (`"WT " + stem`, truncated to 19 chars).

#### 2.2 Reading the wav

`wave` from the stdlib rejects `WAVE_FORMAT_IEEE_FLOAT`, so the baker parses RIFF itself: walk chunks, honour `fmt ` (`format`, `channels`, `samplerate`, `bits`), skip everything that is not `data` (e.g. the `clm ` chunk in `bass.wav`). Supported: PCM 8/16/24/32-bit int (8-bit is unsigned with 128 as zero, so it is biased before scaling), IEEE float 32/64, any channel count (down-mixed to mono), any sample rate (recorded as `WT_RATE`).

`wt/bass.wav` as measured: `RIFF/IEEE float, mono, 44100 Hz, 32-bit`, `data` = 524288 samples = 11.889 s (with a `clm ` chunk at offset 36). Peak 0.99 / RMS 0.71 in the first 2 s, decaying to a steady RMS 0.40 with a 0.64 peak. Fundamental ≈ **21.77 Hz** (MIDI 17.0, `WT_F0_HZ 21.771f`), harmonics at 43.5 / 65.3 / 87.1 / 108.8 / 130.6 Hz — a sub-heavy bass, which is why 2048 points per cycle is the right base size for it (one cycle of 21.77 Hz is 2026 samples at 44.1 kHz). Its cycle carries harmonics 1–4 at 0.844 / 0.176 / 0.076 / 0.026 and almost nothing above that, which caps how much MORPH can do **for this particular wav** (see §4).

#### 2.3 Bake algorithm

1. **DC removal** — subtract the mean of the analysis window.
2. **Analysis window** — default: the highest-RMS 1.0 s window, scanned in 0.05 s hops from 10 % into the file. Overridable with `--start <sec>` / `--len <sec>`. Deterministic, one pass.
3. **f0 estimate** — box-decimate to ~4 kHz, normalized autocorrelation over lags spanning 20–500 Hz, take the highest peak above 0.5 × the global max, then refine at full rate over ±3 % with parabolic interpolation of the ACF. Result: 21.77 Hz for `bass.wav` (printed as `WT_F0_HZ`). If the ACF is flat (noise, no pitch), fall back to a 2048-point table and warn.
4. **Synchronous cycle folding** — fold the whole window into exactly one cycle of `WT_BASE = 2048` points, averaging over `K = floor(len / period)` integer cycles with linear-interpolated reads. Averaging cancels everything that is not harmonically locked to f0, so a played note is steady and click-free.
5. **Normalize** — remove the residual DC of the folded cycle, scale to ±1.0 peak, quantize `round(v * 32767)`.
6. **Mip chain** — each step low-passes the cycle with a 63-tap Hamming windowed sinc at `fs/4` (wrapping indices, since the signal is exactly one period) and then decimates 2:1, renormalized to the base level's RMS with peak headroom, down to 8 points:
   `2048, 1024, 512, 256, 128, 64, 32, 16, 8` → 9 levels, 4088 samples, **8176 bytes** of `int16`.
   The low-pass is what makes the chain a spectral tilt: level `L` keeps harmonics 1..`npts/2` and nothing above, so each level is an octave darker **and** band-limited at its own Nyquist. A plain 2:1 box average only nulls partials near `npts/2`, which leaves every level spectrally identical (measured on `bass.wav`: h1 0.8441 at level 0 *and* 0.8444 at level 6).
7. **Emit** — one flat array plus an offset table, then print a one-line report (`stem, f0, K, total bytes, unit_id`). The files are written **only if the content changed** (write to `.tmp`, compare, rename), so a rebuild does not cascade into recompiles.

Flags: `--base N` (default 2048, may be raised to 4096 for f0 < 15 Hz at 2× the size cost), `--start`, `--len`, `--quiet`.

#### 2.4 Generated interface (`wt_data.h`)

```c
#pragma once
#include <stdint.h>

#define WT_NAME_STR     "BASS"    /* sanitized stem, uppercased        */
#define WT_DISPLAY_NAME "WT BASS"  /* what the unit shows on the kaoss   */
#define WT_UNIT_ID      0x017AU   /* FNV-1a 32 of the stem, & 0x1FF,   */
                                   /* | 0x100 -> always 0x100..0x1FF   */
#define WT_F0_HZ        21.771f   /* baked fundamental                  */
#define WT_RATE         44100.0f  /* nominal rate the table was folded at */
#define WT_BASE         2048      /* points in the largest level        */
#define WT_LEVEL_COUNT  9         /* 2048 .. 8                          */
#define WT_TOTAL        4088
#define WT_MIN_POINTS   8

extern const int16_t wt_table[WT_TOTAL];                 /* all levels, base first */
extern const uint16_t wt_level_offset[WT_LEVEL_COUNT + 1]; /* index ranges per level  */
```

`unit_id` is a hash of the stem, not a hand-assigned constant, so adding a wav to `wt/` needs no ID bookkeeping. Bit 8 is forced on, which pins every wavetable unit to `0x100`–`0x1FF` — deliberately above the `0x00`–`0xFF` block the fork's other `sZXZ` units occupy (`0x01` noise-generator, `0x02` acid/arpeggiator, `0x03` evolving_arp, `0x10` granular, and the `0x1X`/`0x2X`/`0x3X`/`0x4X`/`0x5X` acid + drums feature bits), so the shared vendor namespace cannot collide with them.

`wt_table.c` is a plain `const int16_t` array → lands in `.rodata` inside the `text` PT_LOAD segment. Symbols are *not* per-variant (only one generated source is ever linked per variant), which is why each variant gets its own `-I` directory instead of macro token pasting.

---

### 3. Makefile variant scheme

Mirrors the `acid` variant pattern (recursive sub-make carrying `UDEFS`, one `build/<variant>/` object dir each), extended with wavetable discovery. The `foreach`/`eval` pair is the part that turns a wildcard into per-variant variables — verified end-to-end with two wavs before writing this spec.

```make
WTDIR   := $(PROJDIR)/wt
GENDIR  := $(PROJDIR)/build/gen
BAKER   := $(PROJDIR)/tools/wav2table.py
WTSRCS  := $(wildcard $(WTDIR)/*.wav)
WTSTEMS := $(notdir $(basename $(WTSRCS)))

ifeq ($(strip $(WTSRCS)),)
$(error no wav files in $(WTDIR) -- nothing to build)
endif

# One variant per wav file: the stem is the variant name and -DWT_NAME
VARIANTS := $(WTSTEMS)

define WT_VARIANT
variant_$(1)_defines := -DWT_NAME=$(1)
variant_$(1)_srcs := $(GENDIR)/$(1)/wt_data.c
variant_$(1)_hdrs := $(GENDIR)/$(1)/wt_data.h
variant_$(1)_incs := $(GENDIR)/$(1)
endef
$(foreach v,$(WTSTEMS),$(eval $(call WT_VARIANT,$(v))))

GEN := $(patsubst $(WTDIR)/%.wav,$(GENDIR)/%/wt_data.c,$(WTSRCS))
```

`all` bakes first, then builds one unit per wav; the sub-make gets the generated source, header and include dir (command-line `UCSRC`/`UINCDIR`/`UHDR` override `config.mk`):

```make
all: PRE_ALL $(GEN) $(addprefix $(PROJDIR)/, $(addsuffix .nts3unit, $(VARIANTS))) POST_ALL

$(GENDIR)/%/wt_data.c: $(WTDIR)/%.wav $(BAKER)
	@echo "=== Baking wavetable $* ==="
	@mkdir -p $(dir $@)
	@python3 $(BAKER) $< $@ --name $*

# One explicit rule per variant (see below), not a pattern rule
$(PROJDIR)/bass.nts3unit: $(UCSRC) $(UCXXSRC) $(variant_bass_srcs) $(variant_bass_hdrs) \
                          $(UHDR) $(HDRS) Makefile config.mk
	@echo "=== Building variant bass ==="
	$(MAKE) --no-print-directory PROJECT=bass UDEFS="$(variant_bass_defines)" \
		UCSRC="$(UCSRC) $(variant_bass_srcs)" UINCDIR="$(UINCDIR) $(variant_bass_incs)" \
		UHDR="$(UHDR) $(variant_bass_hdrs)" install
```

**Two GNU make traps this ran into, both of which silently break the build:**

1. A target that already exists is considered up to date, so the recipe above
   never runs and an edited source produces a stale `.nts3unit`. The
   prerequisites are what force the wrapper to be re-entered. It cannot be
   `.PHONY`: listing a target in `.PHONY` creates an *empty explicit rule*, and
   an explicit rule shadows the pattern rule, so make reports "Nothing to be
   done".
2. `$*` is not substituted inside variable references in a pattern rule's
   prerequisite list (macOS ships make 3.81), so `$(variant_$*_srcs)` expands to
   nothing. Hence one `eval`'d explicit rule per variant instead of a pattern
   rule — same `foreach`/`eval` idiom as the variant variables above.

`HDRS` (project + common headers) is also a prerequisite of every object rule,
so editing `wt_osc.h` rebuilds instead of silently reusing stale objects.

Also required in the `Makefile`:

- `bake: $(GEN)` — re-bake only.
- `install` keeps acid's `mv $(BUILDDIR)/$(PRODUCT) $(INSTALLDIR)/$(PRODUCT)`; `BUILDDIR := $(PROJDIR)/build/$(PROJECT)` so variants never share objects.
- `clean` drops `$(PROJDIR)/build`, `$(PROJDIR)/sim` and `$(PROJDIR)/*.nts3unit` (the acid line `$(PROJECT_ROOT)/$(PROJECT)*.nts3unit` no longer matches, since variants are named after wavs).
- `wasm` uses `WTNAME ?= $(firstword $(WTSTEMS))`, copies `$(GENDIR)/$(WTNAME)/wt_data.{c,h}` into `$(WASMDIR)` and adds `-DWT_NAME=$(WTNAME) -I$(GENDIR)/$(WTNAME)` to the emcc line. `emrun` is kept **out** of it in a separate `wasmrun` target: it serves the page and blocks until the browser tab is closed, so `make WTNAME=bass wasm` builds and exits, `make WTNAME=bass wasmrun` also opens it.

---

### 4. DSP (`wt_osc.h` / `wt_osc.cpp`)

- **Band-limited level selection.** Every level is band-limited at its own Nyquist by the baker, so no level can fold back into the audio band whatever the pitch; the only limit on how dark `MORPH` may go is interpolation quality. From `f = midi_to_hz(note)`:

  ```
  L_safe = clamp(log2(WT_BASE * 4.0 * f / WT_RATE), 0, WT_LEVEL_COUNT-1)   // k_wt_min_step = 4
  L      = L_safe * (1.0 - MORPH)            // MORPH 1023 -> level 0, the full cycle
  ```

  split into `floor(L)` and `frac`; the output is a linear crossfade of the two neighbouring levels, so the bright end of the sweep sounds the same at every pitch and neither side of the crossfade can alias. `k_wt_min_step = 4` keeps at least 4 table points per output sample.
  Above `WT_RATE / WT_MIN_POINTS = 5512.5 Hz` the unit falls back to the SDK's band-limited sine, because past that point even the 8-point level is read at less than one point per sample.
- **Table read** — wrap-around position, 4-point Catmull-Rom on the `int16` samples. Phase advances by `f / 48000` per sample, minus the integer part.
- **Unison** — two read pointers, one per output channel, detuned `±DETUNE/2` cents with `fx_pow2f` (exactly symmetric), equal-power pan widening with DETUNE, plus an 8 ms one-pole glide on pitch changes so encoder steps do not click.
- **Sub** — `osc_bl2_sqrf` one octave down, harmonic count `clamp(note - 12, 0, 115) * 6 / 127`, level `SUB/1023 * 0.5`, summed before the filter.
- **Filter** — 4-stage Moog ladder (tanh-saturated) per channel, cutoff `25 Hz * exp(6.215 * CUTOFF/1023)` (25 Hz – 12.5 kHz), resonance feedback up to 3.2, one-pole coefficient `k = 1 - exp(-2*pi*fc/48000)`. The envelope multiplies the cutoff by `1 + 6 * (0.35 + 0.65*RESON)` rather than adding Hz, and the result is clamped to 100 Hz – 18 kHz; an additive envelope offset used to swamp CUTOFF entirely (a 3.3 kHz floor made `CUTOFF = 0` and `CUTOFF = 1023` measure identically).
- **Envelope** — triggered by `k_unit_touch_phase_began` (2 ms linear attack, `DECAY` fall, 30 ms release on `ended`/`cancelled`); drives amp and the filter cutoff. Silent until the pad is touched.
- **Output** — unison pair panned per DETUNE, `fx_softclipf` on the ladder output scaled by amp, then `MIX` as a constant-power dry/wet crossfade (`sqrt(0.5*(1∓m))`).
- **Loader safety** — no stdio, no heap, no `snprintf`; `getBufferSize() == 0` and `unit_init` only requires the `sdram_alloc` hook if a buffer is actually wanted (the table is `const` rodata), so the table never competes with the 3 MB external budget. `tanhf`/`sqrtf` come from libm; `wt_sine_lut_f`, `wt_sqr_lut_f`, `midi_to_hz_lut_f`, `pow2_lut_f` are firmware exports. The linked unit's only `UND` symbols are those four tables plus weak `__sf_fake_*` refs — the same set as the `acid` reference unit.

---

### 5. Files

| File | Role |
| :--- | :--- |
| `spec.md` | This document. |
| `Makefile` | acid-style build + wav discovery, bake rule, per-variant recursion, wasm. |
| `config.mk` | `PROJECT := wavetable`, `UCSRC = header.c`, `UCXXSRC = unit.cc wt_osc.cpp`, `ULIBS = -lm`. |
| `header.c` | Unit descriptor: `dev_id = 0x735A585A` ('sZXZ', the vendor ID shared with the other units in this fork), `unit_id = WT_UNIT_ID`, name `WT_DISPLAY_NAME`, 8 descriptors, default mappings. Includes `"wt_data.h"`. |
| `unit.cc` | Runtime callbacks (acid's file, same pattern: validation in `unit_init`, `cached_values[]` getters, no `<climits>` guards). The `sdram_alloc` hook is only required when `getBufferSize() > 0`, which for this unit never is. |
| `wt_osc.h` / `wt_osc.cpp` | `Processor` implementation: mip selection, wavetable/unison/sub oscillators, ladder filter, envelopes, mixer. |
| `tools/wav2table.py` | The baker (§2). |
| `wt/*.wav` | Wavetable sources; one variant per file. |

---

### 6. Build & verify

- `make` → bakes every wav, then emits `<stem>.nts3unit` per wav (e.g. `bass.nts3unit` in the project root). Re-running `make` with nothing changed is a silent no-op.
- `make bake` → re-bake only. `make WTNAME=bass wasm` → that table built in `sim/bass.html`; `make WTNAME=bass wasmrun` also opens it in a browser.
- Size budget: table 8176 B + offsets 20 B + code ≈ 18 KB per unit, against the **~32 KB max unit size / 32 KB max RAM load**.
- Loader check: `arm-none-eabi-nm -D --undefined-only build/bass/bass.elf` must list only the firmware tables (`wt_*`, `pow2_lut_f`, `midi_to_hz_lut_f`) and weak `__sf_fake_*` data refs.
- Sanity check the bake before trusting it: the baker prints f0 and total bytes, and `build/gen/bass/wt_data.c` is a plain int16 table — feed it back through a DFT and confirm (a) the strongest partial sits at the printed f0 and (b) **the mip levels are actually different**, e.g. level 8's h8/h1 is 0.000 while level 0's is 0.006. A flat pyramid means the low-pass in `build_mips` is missing or mis-tuned, and MORPH will be inaudible.

#### Measured on the current build

`arm-none-eabi-size build/bass/bass.elf`: `text 17123, data 444, bss 136` = **17703 B**, packaged as an 18824 B `bass.nts3unit`. Verified with a host harness that renders the compiled `wt_osc.cpp` against the real baked table and firmware LUT stubs, then FFTs the result (`/var/folders/.../opencode/wt_test`, throwaway):

| Check | Result |
| :--- | :--- |
| Pitch tracking (`PITCH` 360/483/725/886/1023) | within 0.1 % of `440*2^((v*127/1023 - 69)/12)` at every pitch |
| Aliasing above 16 kHz, `MORPH` at both extremes | −57 … −151 dB relative to the fundamental |
| MORPH, bright → dark at 1480 Hz | h2 −1.0 dB, h3 −0.5 dB, h4 −1.6 dB (small: see below) |
| CUTOFF 0 → 1023 at 587 Hz | −22 dB level, h2 12 % → 1 %, h3 15 % → 0.3 % (clearly audible) |
| MIX 0 (balanced) | input 0.25 → 0.179 and wet 0.1382 → 0.0977, both exactly ×`sqrt(0.5)` |
| DETUNE 1200 cents | symmetric octave pair, −13.6 dB side signal, 0.0 dB balance |
| Above 5512.5 Hz | sine fallback, pitch exact to 0.00 % |

**On MORPH and this particular wav.** The mechanism is correct and general — proven on a synthetic saw source, where the pyramid is a clean one-octave-per-level tilt (h8/h1 0.125 → 0.119 → 0.112 → 0.000). But `bass.wav`'s single cycle carries harmonics 1–4 and essentially nothing above (−28 dB and falling by h5), so every mip level of *this* table is spectrally the same and MORPH's whole sweep can only ever move the top few harmonics by ~1 dB. That is a property of the sample, not a bug: a wavetable morph cannot brighten or darken energy the cycle does not have. Drop a saw or a reedy wav into `wt/` and the same code gives the full sweep; the timbral range that this wav *does* have is covered by CUTOFF/RESON.
