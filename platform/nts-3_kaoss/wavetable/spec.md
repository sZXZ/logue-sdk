### NTS-3 kaoss pad "WT" — Wavetable Oscillator Unit

A single-cycle **wavetable oscillator** for the NTS-3 kaoss pad, built as a `genericfx` unit. It **mixes with** the audio input rather than replacing it: the input passes through on both channels at unity and the generated wavetable voice is added on top, the same additive pass-through the acid unit uses. Its distinguishing feature is the **build-time wavetable baker**: every `.wav` dropped into `wt/` is folded down to a single cycle, mip-mapped, quantized to `int16` and emitted as a generated C source. Each wav becomes **one build variant** (its own `.nts3unit`), so a folder of samples turns into a folder of units from a single `make`.

---

### 1. Parameters (8 slots, all of them)

| # | Parameter | Range | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | PITCH | `0`–`1023` | `k_unit_param_type_midi_note` | `360` (≈A2) | Base pitch. DSP reads `note = value * 127 / 1023` (arpeggiator convention), 0–127 → 8.18 Hz – 12.54 kHz. With `PATTERN = 0` this is the note that sounds; otherwise it is the **root** the generated line plays around. |
| **2** | POSITION | `0`–`1023` | `k_unit_param_type_none` | `512` | Wave position. `0` = darkest level the current pitch allows, `1023` = the full baked cycle. Crossfades adjacent mip levels, so the whole sweep is alias-free. Default-mapped to the **X axis** of the KAOSS pad. |
| **3** | LFO RATE | `0`–`1023` | `k_unit_param_type_none` | `512` (= **1.00 Hz**) | Free-running LFO rate, exponential `0.05 * 400^(v/1023)` = 0.05 Hz – 20 Hz. The default sits at the geometric centre of the range. |
| **4** | LFO DEPTH | `0`–`1023` | `k_unit_param_type_none` | `0` | How far the LFO sweeps POSITION, ±`LFO DEPTH`/1023, clamped to `0..1`. Default `0` keeps the unit static until asked. Default-mapped to the **Y axis**. |
| **5** | ADSR | `0`–`1023` | `k_unit_param_type_none` | `256` (exactly **Pad**) | Envelope morph. `0..819` walks the arpeggiator unit's five presets — **Pluck → Pad → Perc → Swell → Long release** — as four equal 205-wide zones, interpolating attack/decay/sustain/release between neighbours. `820..1023` is **Drone**. `getParameterStrValue` names the zone it is crossing (`Pluck`/`Pad`/`Perc`/`Swell`, then `Drone`). |
| **6** | PATTERN | `0`–`1023` | `k_unit_param_type_none` | `0` (**Off**) | `0` disables the sequencer and the unit is a plain touch-played oscillator. Any other value is a **seed**: it generates a deterministic 16-step acid line (see §4) and plays it under the touch. Default-mapped to the **DEPTH** knob. |
| **7** | SUB | `0`–`1023` | `k_unit_param_type_none` | `384` | Level of a band-limited square sub-oscillator (`osc_bl2_sqrf`) one octave below PITCH, at `SUB/1023 * 0.5`. |
| **8** | DETUNE | `-1200`–`1200` | `k_unit_param_type_cents` | `8` | Unison spread. Two wavetable oscillators, one per output channel, detuned by ±`DETUNE`/2 cents. `0` = mono. |

Descriptor rows use `{min, max, center, init, type, frac, frac_mode, reserved, {"NAME"}}`; only slots 1–8 are used (`num_params = 8`), and every mapping's `min/max/value` stays inside its descriptor range. Six of the eight are plain `0..1023`; PITCH is a `midi_note` and DETUNE is a bipolar `cents` knob.

**There is deliberately no filter and no dry/wet crossfade.** The output is `in + wet`, never a crossfade, so the unit layers under whatever the pad is already playing; CUTOFF/RESON are not just unused — the slots went to `LFO RATE`, `LFO DEPTH` and `PATTERN`, which are wavetable-specific controls a filter unit cannot provide.

Pad/knob defaults in `header.c`: `X → POSITION (0..1023, 512)`, `Y → LFO DEPTH (0..1023, 0)`, `DEPTH → PATTERN (0..1023, 0)`, everything else `k_genericfx_param_assign_none` with the descriptor range (encoders only). Optional build switch `-DWTPAD_PITCH` swaps the X/Y pair to `X → PITCH`, `Y → POSITION` for a playable theremin-style unit.


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

`wt/bass.wav` as measured: `RIFF/IEEE float, mono, 44100 Hz, 32-bit`, `data` = 524288 samples = 11.889 s (with a `clm ` chunk at offset 36). Peak 0.99 / RMS 0.71 in the first 2 s, decaying to a steady RMS 0.40 with a 0.64 peak. Fundamental ≈ **21.77 Hz** (MIDI 17.0, `WT_F0_HZ 21.771f`), harmonics at 43.5 / 65.3 / 87.1 / 108.8 / 130.6 Hz — a sub-heavy bass, which is why 2048 points per cycle is the right base size for it (one cycle of 21.77 Hz is 2026 samples at 44.1 kHz). Its cycle carries harmonics 1–4 at 0.844 / 0.176 / 0.076 / 0.026 and almost nothing above that, which caps how much POSITION can do **for this particular wav** (see §6).

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

- **Band-limited level selection.** Every level is band-limited at its own Nyquist by the baker, so no level can fold back into the audio band whatever the pitch; the only limit on how dark `POSITION` may go is interpolation quality. From the smoothed `freq`:

  ```
  L_safe = clamp(log2(WT_BASE * 4.0 * freq / WT_RATE), 0, WT_LEVEL_COUNT-1)   // k_wt_min_step = 4
  L      = L_safe * (1.0 - position)           // POSITION 1 -> level 0, the full cycle
  ```

  split into `floor(L)` and `frac`; the output is a linear crossfade of the two neighbouring levels, so the bright end of the sweep sounds the same at every pitch and neither side of the crossfade can alias. `k_wt_min_step = 4` keeps at least 4 table points per output sample.
  Above `WT_RATE / WT_MIN_POINTS = 5512.5 Hz` the unit falls back to the SDK's band-limited sine, because past that point even the 8-point level is read at less than one point per sample.
- **Table read** — wrap-around position, 4-point Catmull-Rom on the `int16` samples. Phase advances by `freq / 48000` per sample, minus the integer part.
- **LFO → POSITION** — a free-running sine (SDK `osc_sinf`) sweeps the position value itself: `pos = clip01(position + lfo * LFO DEPTH)`, phase advancing at `rate / 48000`. Because it moves POSITION and not the pitch, a held note keeps changing timbre without drifting in tuning; the *rate* is what makes it sound like an LFO rather than vibrato.
- **Unison** — two read pointers, one per output channel, detuned `±DETUNE/2` cents with `fx_pow2f` (exactly symmetric), equal-power pan widening with DETUNE, plus an 8 ms one-pole glide on pitch changes so encoder steps and pattern jumps do not click.
- **Sub** — `osc_bl2_sqrf` one octave down, harmonic count `clamp(note - 12, 0, 115) * 6 / 127`, level `SUB/1023 * 0.5`, summed into both channels before the VCA.
- **Envelope** — triggered by `k_unit_touch_phase_began` and by every sequencer step. The shape is `currentAdsr()`: the arpeggiator unit's five presets (`0.002/0.06/0.0/0.04`, `0.4/0.3/0.8/0.6`, `0.001/0.12/0.0/0.015`, `0.25/0.4/0.9/0.35`, `0.01/0.2/0.6/1.2` s) walked as four equal zones over `0..819` (`k_adsr_span = 820`, `k_adsr_zone = 205`), so the knob lands on Pluck / Pad / Perc / Swell / Long at the same five points it does on the arpeggiator unit, interpolating all four coefficients between neighbours. Default `256` is exactly Pad.
  **Release is linear from the captured level** — `env_phase_` latches `amp_` when the pad lifts and the release falls by `rel_inc * env_phase_` — matching the arpeggiator unit's linear tail, instead of the exponential decay a `1 - e^-kt` law would give.
  A retrigger from the sequencer **resumes the attack from the current level** rather than snapping to zero, so a fast sequence never clicks.
- **Drone** — `ADSR >= 820` bypasses the whole envelope: `amp_ = 1` every sample, sustain forced to 1, touch and PATTERN ignored entirely (a drone is already sounding, so there is nothing for a gate to do). It keeps oscillating until the parameters change. This is why the morph was capped at `820` instead of `1023`: the tail of the knob is a mode, not a sixth envelope shape.
- **Generative 16-step pattern** — ported from the acid unit so both units generate lines that feel like siblings. `PATTERN` is a seed, not a density: `regenPattern()` mixes an LCG with the minor pentatonic scale `{0, 2, 3, 5, 7, 8, 10, 12}`, biased to root (50 %) and fifth (25 %), adds ~44 % accents and ~25 % legato slides, and picks its own Euclidean hit density of 5–12 of 16. Step 0 is forced to be the root, accented and unslid, so the bar has a downbeat. Changing PATTERN while touched regenerates, rewinds to step 0 and triggers immediately, so the new pattern is heard at once; with the pad released the change waits for the next touch.
- **Host clock** — `setTempo` sets `samples_per_step_` for one 16th note, and `tempo4ppqnTick` records the host counter. **While the host is sending 4PPQN the two are exclusive** (`if (host_sync_valid_) … else …`): the unit follows `host_counter_ & 15` and runs no second clock. This is a real bug that was measured, not a theoretical one — advancing on both triggers every step twice a few samples apart, and the second trigger cuts the first note short. The internal clock is only used when no valid host tick has ever arrived, which is what makes the WASM/browser and offline renders keep time.
- **Output** — unison pair panned per DETUNE plus sub, scaled by `amp_ * step_accent_ * 0.5` (accents are `1.0` vs `0.55`), then `fx_softclipf(0.15, …)` per channel, then **added to the input**: `out = in + softclip(wet)`, both channels, exactly the acid unit's `write_out` default path. The soft clip is applied to the wet signal *before* the sum so the dry path stays a clean passthrough, and the `in` pointer is advanced with `out` in the sample loop so each output sample gets its own input sample.
  Two consequences worth stating: an idle unit is a **bit-exact passthrough** (measured deviation 0.0), and the wet never sees the input, so nothing in this unit can ring or self-oscillate off the pad's own signal.
- **Loader safety** — no stdio, no heap, no `snprintf`; `getBufferSize() == 0` and `unit_init` only requires the `sdram_alloc` hook if a buffer is actually wanted (the table is `const` rodata), so the table never competes with the 3 MB external budget. The DSP leans on the SDK's fast approximations (`fasterexpf`, `fastlog2f`, `fastercosfullf`, `fastersinfullf`, `linintf`, `clipminmaxf`) rather than libm, so the only firmware tables it needs are `wt_sine_lut_f`, `wt_sqr_lut_f`, `midi_to_hz_lut_f`, `pow2_lut_f`. Those four are the linked unit's *entire* `UND` list — the same set as the `acid` reference unit. (`-lm` stays in `config.mk` from acid; nothing in this unit needs it any more now that the ladder filter's `tanhf` is gone.)



---

### 5. Files

| File | Role |
| :--- | :--- |
| `spec.md` | This document. |
| `Makefile` | acid-style build + wav discovery, bake rule, per-variant recursion, wasm. |
| `config.mk` | `PROJECT := wavetable`, `UCSRC = header.c`, `UCXXSRC = unit.cc wt_osc.cpp`, `ULIBS = -lm`. |
| `header.c` | Unit descriptor: `dev_id = 0x735A585A` ('sZXZ', the vendor ID shared with the other units in this fork), `unit_id = WT_UNIT_ID`, name `WT_DISPLAY_NAME`, 8 descriptors, default mappings. Includes `"wt_data.h"`. |
| `unit.cc` | Runtime callbacks (acid's file, same pattern: validation in `unit_init`, `cached_values[]` getters, no `<climits>` guards). `setTempo` and `tempo4ppqnTick` forward the host transport to the DSP. The `sdram_alloc` hook is only required when `getBufferSize() > 0`, which for this unit never is. |
| `wt_osc.h` / `wt_osc.cpp` | `Processor` implementation: mip selection and table read, LFO, unison, sub, ADSR morph + drone, the generative sequencer and its host/internal clock, softclip, additive mix with the input. |
| `tools/wav2table.py` | The baker (§2). |
| `wt/*.wav` | Wavetable sources; one variant per file. |
| `wasm.cc` | Shared genericfx WebAssembly wrapper (unmodified by this redesign): it enumerates `unit_header.common.params[]` at runtime, so the browser harness picks up the new eight parameters and the removed ones automatically. Only the generated `wt_data.c/h` is swapped per variant. |

---

### 6. Build & verify

- `make` → bakes every wav, then emits `<stem>.nts3unit` per wav (e.g. `bass.nts3unit` in the project root). Re-running `make` with nothing changed is a silent no-op.
- `make bake` → re-bake only. `make WTNAME=bass wasm` → that table built in `sim/bass.html`; `make WTNAME=bass wasmrun` also opens it in a browser.
- `make clean` → drops `build/`, `sim/` and the `*.nts3unit` files. The three variants must be rebuilt from scratch, not incrementally, to be trusted.
- Size budget: table 8176 B + offsets 20 B + code ≈ 17 KB per unit, against the **~32 KB max unit size / 32 KB max RAM load**.
- Loader check: `arm-none-eabi-nm -u build/bass/bass.elf` must list exactly `wt_sine_lut_f`, `wt_sqr_lut_f`, `midi_to_hz_lut_f`, `pow2_lut_f` and nothing else.
- Sanity check the bake before trusting it: the baker prints f0 and total bytes, and `build/gen/bass/wt_data.c` is a plain int16 table — feed it back through a DFT and confirm (a) the strongest partial sits at the printed f0 and (b) **the mip levels are actually different**, e.g. level 8's h8/h1 is 0.000 while level 0's is 0.006. A flat pyramid means the low-pass in `build_mips` is missing or mis-tuned, and POSITION will be inaudible.

#### Measured on the current build

`arm-none-eabi-size build/<v>/<v>.elf`: `text 16889, data 364, bss 200` = **17453 B**, packaged as an 18512 B `<v>.nts3unit` — identical for all three variants, since only the table's contents differ. (The `data`/`text` split is what the loader actually cares about: `text` is the resident code + rodata that holds the 8176 B table, and it is the same size for all three because the array is the same length.)

The DSP is verified by a host harness that renders the **compiled** `wt_osc.cpp` against the real baked table and firmware LUT stubs, then FFTs / autocorrelates the result (`/var/folders/.../opencode/wt_test`, throwaway). It is a behavioural suite, not a golden-file comparison: it asserts relationships (partials are harmonics, timings are close, the played pattern is the generated pattern), so it survives DSP tuning. **228 checks, all passing, for each of `bass`, `cr_ch` and `evolve`.**

| Check | Result |
| :--- | :--- |
| Pitch tracking (`PITCH` 360/483/725/886/1023) | fundamental within **0.04 %** of the expected note; every strong partial is an exact harmonic or its exact fold image (`48000 - 6*f0`, `48000 - 8*f0` at the top end) |
| `POSITION` sweep, alias above 16 kHz | **0.000000** at dark / mid / bright; 3–9 kHz energy rises 0.010 → 0.030 → 0.050 |
| ADSR vs the arpeggiator unit's presets | Pad 363/589 ms (want 400/600), Swell 224/344 ms (250/350), Long 11/1171 ms (10/1200), Pluck and Perc silent before the lift as designed — all inside the suite's ±45 ms attack / ±60 ms release tolerance |
| Envelope linearity | the release is a straight line from the captured level, not an exponential tail; sustain steady to worst 0.027 per 10 ms window over a held note |
| DRONE (`ADSR >= 820`) | sounds untouched (level ratio 1.00 against the drone+pattern case), keeps sounding through PATTERN, and a normal ADSR is **completely silent** before touch |
| LFO | on the `cr_ch` wav (deliberately the sweep-heaviest of the three, so the test has the most to see) the 3–9 kHz fraction swings **0.041 → 0.359** over one cycle at both 1.00 Hz and 0.22 Hz, while `f0` stays at 2347.9/2348.8 Hz — timbre, not pitch. Per variant the swing is `bass` 0.020, `cr_ch` 0.317, `evolve` 0.295; `bass` is small for the reason in the note below, and its `f0` still does not move (586.7/586.9) |
| PATTERN | generated hits `[0,2,4,6,8,10,11,12,13,14,15]` are played **exactly**, no extra step triggers, every heard note is the note the generator chose, step 0 is the root, accent ratio 1.71 (design 1.00/0.55 = 1.82) |
| Host sync | 4PPQN-synced and free-running renders agree on onset time to **0.0 ms**, worst 16th-grid error **2.5 ms**; both trigger repeatedly, stop on release, and the drone does not |
| Input mixing | `out - in` recovered from a render fed a stereo sine matches an otherwise identical silent-input render to **3e-8** (float32 rounding) — i.e. exactly `out = in + wet`; with the unit idle the output is **bit-exact** the input; a drone over a DC input keeps the input's 0.25 and adds its own 0.42 rms beside it |

**On POSITION and these particular wavs.** The mechanism is correct and general — proven on a synthetic saw source, where the pyramid is a clean one-octave-per-level tilt (h8/h1 0.125 → 0.119 → 0.112 → 0.000). But the shipped wavs are each harmonic-poor in a different way, so how far POSITION can actually travel depends on the sample:

- `bass.wav`'s single cycle carries harmonics 1–4 and essentially nothing above (−28 dB and falling by h5), so every mip level of *this* table is spectrally the same and POSITION's whole sweep can only ever move the top few harmonics by ~1 dB. A wavetable morph cannot brighten or darken energy the cycle does not have — that is a property of the sample, not a bug. Drop a saw or a reedy wav into `wt/` and the same code gives the full sweep.
- `cr_ch.wav` is the opposite case and shows the sweep working: its cycle is dominated by h6/h8 with the fundamental **25 dB down**, which is why its LFO/timbre measurements swing far harder than the bass's (0.317 vs 0.020). At high pitches those dominant partials legitimately fold (`h6`→`48000 - 6*f0`, `h8`→`48000 - 8*f0`) and the suite asserts they land exactly on the fold image rather than treating it as an aliasing bug.
- `evolve.wav` sits between the two: LFO swing 0.295, i.e. it has the spectral content to make the sweep audible, just without `cr_ch`'s extreme 6th-harmonic dominance.

**On the fundamental.** The pitch tests deliberately look for h1 in the *full* peak list (floor −29 dB) rather than the strong-peak subset (−15 dB), because `cr_ch` keeps its fundamental 25 dB down. Selecting h1 only among strong peaks reports a missing fundamental for that wav even though it is plainly there — a property of the source, not a defect in the unit.


---

### 7. Relationship to the acid unit (signal-chain deviations)

The acid unit is the sibling this one was ported from, so the differences are worth writing down rather than discovering by ear. Everything below was read off the two sources; nothing is inferred. The **input/output stage is now identical to acid's default variant**: dry passes both channels at unity, the wet is soft-clipped at 0.15 and then added, with the same 0.5 VCA trim.

#### Same as acid

- Input/output stage: `out = in + fx_softclipf(0.15, wet * 0.5)`, both channels (`write_out`'s default path). The `_L`/`_R` build variants are the only difference here, see below.
- Sequencer skeleton: 16 steps of 16ths at host tempo, seeded by `PATTERN` with the same LCG (`0x7F4A7C15 + (seed+1)*0x9E3779B9`), the same minor pentatonic `{0,2,3,5,7,8,10,12}`, the same root/fifth bias, the same ~44 % accents and ~25 % legato slides, step 0 forced to the root, slides gliding without a re-attack, and a retrigger that resumes the attack from the current level so a fast line never clicks. **The same PATTERN seed yields the same 16 notes in both units.**
- Audio only while the pad is touched; the clock keeps running; legato slides and accent-weighted retriggers behave identically.
- Same vendor `dev_id` (`0x735A585A`, 'sZXZ').

#### Deviations, in signal-flow order

| # | Stage | Acid | Wavetable unit |
| :--- | :--- | :--- | :--- |
| 1 | Output routing | `_L` / `_R` build variants put the voice on one channel only (ids `0x12`/`0x22`/`0x32`/`0x42`), so two copies can be chained as separate instruments | no such variant; the voice always goes to both channels |
| 2 | Oscillators | one band-limited oscillator, saw↔square crossfade by `WAVE` | two (unison), one per channel, ±`DETUNE`/2 cents, equal-power pan |
| 3 | Waveform source | synthesised: `osc_bl2_sawf` / `osc_bl2_sqrf`, band-limited by harmonic count | baked: `int16` mip-mapped cycle, 4-point Catmull-Rom, band-limited by picking a mip level from pitch and POSITION |
| 4 | Top of the range | just stops adding harmonics | explicit crossover to the firmware band-limited sine above 5512.5 Hz |
| 5 | Filter | 4-stage Moog ladder, tanh-saturated, resonance feedback to ~3.5, cutoff `30 Hz * exp(6.215*cutoff)` swept up by the envelope (`+4000*(0.2+1.2*ACID)` Hz) | **none** — no filter state, no resonance, no cutoff, no tanh harmonics |
| 6 | Pad axes | X → CUTOFF, Y → RESON | X → POSITION, Y → LFO DEPTH |
| 7 | Envelope | fixed 1.2 ms attack, decay 10–2000 ms from `DECAY`, release 35→12.5 ms scaled by `ACID`, **no sustain level** (decay always reaches zero) | the arpeggiator's 5-preset morph: attack 1–400 ms, decay 60–400 ms, **sustain 0–0.9**, release 15 ms–1.2 s; defaults to Pad rather than something short and percussive |
| 8 | Envelope destination | drives the VCA **and** the filter cutoff | drives the VCA only — there is no filter to sweep |
| 9 | Release curve | constant decrement, so the release time depends on the level it starts from | linear scaled by the level captured at release, so it is time-correct from any level |
| 10 | Drone | none | `ADSR >= 820` bypasses the envelope entirely, holds full sustain and sounds with no touch |
| 11 | Accent meaning | `accent ? 1.0 : 0.0` — an un-accented step is **silent**, and the accent also brightens the filter via `acc_boost` | `accent ? 1.0 : 0.55` — every hit sounds, accents are 1.8× louder, with no brightness coupling |
| 12 | Modulators | none at all | free-running LFO, 0.05–20 Hz, sweeping POSITION by ±depth |
| 13 | Sub | none; the low end comes from the filter | band-limited square one octave down at `SUB` level, summed into both channels |
| 14 | Density | user parameter `DENSITY`, 1–16 Euclidean pulses (default 12) | no control; the seed derives 5–12 pulses, so turning PATTERN changes the rhythm as well as the notes |
| 15 | Pattern change | only sets a dirty flag; picked up lazily, no rewind, no immediate trigger | while touched: regenerate, rewind to step 0, clear the clock and trigger immediately |
| 16 | Sequencer off | impossible — the unit always sequences | `PATTERN = 0` is a mode: no sequencer, the pad plays the root note |
| 17 | Pattern drift | `-DAUTODRIFT` mutates a few steps once per bar so the line drifts | no drift; a seed is a fixed 16-step loop |
| 18 | Clock | the host snap and the free-running clock are two independent `if`s, so if the two grids ever drift the same step can be triggered twice a few samples apart and the second trigger cuts the note short | mutually exclusive `if`/`else`, so it cannot double-trigger; host and free-running renders agree to 0.0 ms. **The acid unit still carries the looser version** |
| 19 | Parameters | `WAVE ROOT PATTERN DENSITY CUTOFF RESON DECAY ACID` | `PITCH POSITION LFO RATE LFO DEPTH ADSR PATTERN SUB DETUNE` — only `PATTERN` shares a name, and `ROOT` is `PITCH` |
| 20 | Identity | fixed ids `0x02`/`0x12`/`0x22`/`0x32`/`0x42` | one id per wav, `(FNV-1a(stem) & 0x1FF) \| 0x100` → `0x100`–`0x1FF` (bass `0x017A`, cr_ch `0x0106`, evolve `0x01D0`), keeping `0x00`–`0xFF` free for the fork's other units so both can be installed together |
