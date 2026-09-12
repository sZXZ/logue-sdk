### The 8-Parameter "DRUMS" Spec

Generative drum machine implemented as an NTS-3 kaoss pad generic FX unit. It runs a tempo-synced 16-step pattern of up to three voices (kick, snare, hi-hat) while you hold the pad. Like the acid unit, the generated drum signal is mixed **on top of** the dry input (`out = in + drums`), so the unit is transparent and silent until the pad is touched.

| # | Parameter | Range | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | PATTERN | `0`–`1023` | `k_unit_param_type_none` | `512` | Seed that deterministically generates the kick/snare/hat step pattern via an LCG. Rhythmic patterns are distributed across the range. |
| **2** | DENSITY | `1`–`16` | `k_unit_param_type_enum` | `8` | Euclidean pulse count `K` — how many of the 16 steps contain at least one active voice. Shared rhythm skeleton for all three voices. |
| **3** | KICK | `0`–`1023` | `k_unit_param_type_none` | `768` | Kick drum level and character. Low values → soft thud; high values → loud, punchy kick with a longer swept sine tone (~170 → 40 Hz) and longer note. Mapped to KAOSS X-axis. |
| **4** | SNARE | `0`–`1023` | `k_unit_param_type_none` | `512` | Snare drum level and snap. Blends a short tuned body (200 Hz sine, ~20 ms) with broadband noise burst. High values increase the noise ratio and crack. |
| **5** | HIHAT | `0`–`1023` | `k_unit_param_type_none` | `384` | Hi-hat level. The parameter also selects between closed (0–511) and open (512–1023) character: closed hats decay in ~40 ms, open hats in ~180 ms. Both scaled by `DECAY`. Mapped to KAOSS Y-axis. |
| **6** | DECAY | `0`–`2000` | `k_unit_param_type_msec` | `400` | Master note length for every voice. Each voice envelope falls linearly to silence over a per-voice note length; `DECAY` scales every length multiplicatively (0.5× at 0 ms → 3.0× at 2000 ms), so short decay = tight plucky hits and long decay = ringing tails. Displayed in milliseconds (0–2000 ms). |
| **7** | TONE | `0`–`1023` | `k_unit_param_type_none` | `512` | Overall tonal brightness. Acts as a shared high-shelf boost/cut and slightly tunes the snare body frequency (150–280 Hz). |
| **8** | DRIVE | `0`–`1023` | `k_unit_param_type_none` | `256` | Analog grit macro: applies soft-clip saturation to the summed drum bus, mimicking the transistor saturation in the original hardware. 0 = clean, 1023 = heavily clipped. |

The `k_unit_param_type_msec` descriptor expresses values up to 2000 ms (beyond the standard 0–1023 knob range); only pad/x-y assigned knobs follow the 0–1023 convention.

---

### Behavior

- **Sequencer**: free-running 16th-note clock synced to the host tempo (`setTempo(float bpm)` / `unit_set_tempo`). Audio only sounds while the pad is touched.
- **Generative pattern**: `PATTERN` seeds a lightweight LCG that fills three independent 16-step boolean arrays:
  - **Kick** steps: sparse, biased toward steps 1, 5, 9, 13 (quarter-note grid) with probabilistic fills.
  - **Snare** steps: typically falls on beats 5 and 13 (2 & 4) with occasional ghost hits.
  - **Hi-hat** steps: dense, often every step or every other step, thinned by `DENSITY`.
- **Euclidean rhythm**: `DENSITY` applies a Björklund algorithm to mask the raw LCG pattern, keeping `K` of the 16 steps active across the combined grid.
- **Evo drift (build option **`-DAUTODRIFT`**, built as **`drum_evo`**)**: turns the static pattern into an evolving one. Once per bar 1–4 hits are toggled or nudged (kick/snare/hat, hats kept on active Euclidean steps) so the groove drifts over time; the downbeat kick always stays.
- **Kick voice**: pitched sine sweep (~170 → 40 Hz using `osc_sinf`), driven by a ~1 ms attack ramp and a linear decay to silence over the kick note length. Sweep depth and note length scale with `KICK` and `DECAY`.
- **Snare voice**: sum of a short sine body (tuned by `TONE`, ~150–280 Hz) and a filtered noise burst. Noise generated via `osc_rand`, high-pass filtered to ~800 Hz. Amplitude envelope: fast attack, linear decay (~20–65 ms default, scaled by `DECAY`).
- **Hi-hat voice**: band-pass filtered white noise (`osc_rand`) centered around 8–10 kHz, with `TONE` tilting the center frequency. Decay controlled jointly by `HIHAT` range (closed/open) and `DECAY`.
- **Envelopes**: all voices use a ~1 ms attack ramp (rounds onsets, no click transients) and *linear* amplitude decay to silence over the note length. Linear decay makes the `DECAY` knob audibly stretch/compress each drum hit. Note lengths default: kick 60–400 ms (level-scaled), snare 20–65 ms, closed hat 40 ms, open hat 180 ms; `DECAY` scales all multiplicatively (0.5×–3.0×).
- **DRIVE / Saturation**: summed drum bus passed through `fx_softclipf` with an input gain of `0.75 + 1.75 * (drive / 1023.0f)` (soft at the default, heavy clipping at maximum).
- **KAOSS pad**: touching the pad gates the sequencer on; releasing starts fade-out. X-axis → `KICK`; Y-axis → `HIHAT` via default mappings in `header.c`.

### Files

| File | Role |
| :--- | :--- |
| `effect.h` | All DSP: sequencer, pattern LCG, Euclidean generator, kick/snare/hat voice synthesis, envelopes, saturation bus. |
| `header.c` | Unit descriptor: name, 8 parameter descriptors + default pad mappings. |
| `unit.cc` | C bridge to the runtime (init / render / param / touch / tempo callbacks). |
| `wasm.cc` | WASM bindings for the browser simulator. |

### Build

- **Hardware**: `make` builds and installs both variants:
  - `drum.nts3unit` — static pattern (default).
  - `drum_evo.nts3unit` — automatic pattern drift (`-DAUTODRIFT`), display name "Drum Evo".
  - Single variant: `make` target per unit (e.g. `make` in a clean dir, or `make PROJECT=drum_evo UDEFS=-DAUTODRIFT install`).
- **Simulator**: restage `sim/` assets from `websim/` and compile `wasm.cc header.c unit.cc effect.h` with the emscripten SDK (see `Makefile`).
