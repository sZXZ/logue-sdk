### The 8-Parameter "DRUMS" Spec

Generative drum machine implemented as an NTS-3 kaoss pad generic FX unit. It runs a tempo-synced 16-step pattern of up to three voices (kick, snare, hi-hat) while you hold the pad. Like the acid unit, the generated drum signal is mixed **on top of** the dry input (`out = in + drums`), so the unit is transparent and silent until the pad is touched.

| # | Parameter | Range | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | PATTERN | `0`–`1023` | `k_unit_param_type_none` | `512` | Seed that deterministically generates the kick/snare/hat step pattern via an LCG. Rhythmic patterns are distributed across the range. |
| **2** | DENSITY | `1`–`16` | `k_unit_param_type_enum` | `8` | Euclidean pulse count `K` — how many of the 16 steps contain at least one active voice. Shared rhythm skeleton for all three voices. |
| **3** | KICK | `0`–`1023` | `k_unit_param_type_none` | `768` | Kick drum level and character. Low values → soft thud; high values → loud, punchy kick with a longer pitched sine sweep (60 → 30 Hz, ~45 ms). Mapped to KAOSS X-axis. |
| **4** | SNARE | `0`–`1023` | `k_unit_param_type_none` | `512` | Snare drum level and snap. Blends a short tuned body (200 Hz sine, ~20 ms) with broadband noise burst. High values increase the noise ratio and crack. |
| **5** | HIHAT | `0`–`1023` | `k_unit_param_type_none` | `384` | Hi-hat level. The parameter also selects between closed (0–511) and open (512–1023) character: closed hats decay in ~40 ms, open hats in ~300 ms. Mapped to KAOSS Y-axis. |
| **6** | DECAY | `0`–`2000` | `k_unit_param_type_msec` | `400` | Master envelope fall time applied to all voices (scales proportionally; voices have their own floor minimums). Displayed in milliseconds (0–2000 ms). |
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
- **Kick voice**: exponential pitch sweep from ~60 Hz to ~30 Hz using a sine wave (`osc_sinf`), driven by a fast attack / exponential decay amplitude envelope. Pitch sweep depth and duration scale with `KICK`.
- **Snare voice**: sum of a short sine body (tuned by `TONE`, ~150–280 Hz) and a filtered noise burst. Noise generated via `osc_rand`, high-pass filtered to ~800 Hz. Amplitude envelope: fast attack, ~20–60 ms decay.
- **Hi-hat voice**: band-pass filtered white noise (`osc_rand`) centered around 8–10 kHz, with `TONE` tilting the center frequency. Decay controlled jointly by `HIHAT` range (closed/open) and `DECAY`.
- **Envelopes**: all voices use a linear attack (~0.5 ms) and exponential decay; individual decay floors are 30 ms (kick), 15 ms (snare body), 10 ms (snare noise), 20 ms (closed hat), 150 ms (open hat). `DECAY` scales all floors multiplicatively.
- **DRIVE / Saturation**: summed drum bus passed through `fx_softclipf` with input gain `1 + 3 * (drive / 1023.0f)`.
- **KAOSS pad**: touching the pad gates the sequencer on; releasing starts fade-out. X-axis → `KICK`; Y-axis → `HIHAT` via default mappings in `header.c`.

### Files

| File | Role |
| :--- | :--- |
| `effect.h` | All DSP: sequencer, pattern LCG, Euclidean generator, kick/snare/hat voice synthesis, envelopes, saturation bus. |
| `header.c` | Unit descriptor: name, 8 parameter descriptors + default pad mappings. |
| `unit.cc` | C bridge to the runtime (init / render / param / touch / tempo callbacks). |
| `wasm.cc` | WASM bindings for the browser simulator. |

### Build

- **Hardware**: `make` → `make install` produces `drums.nts3unit`.
- **Simulator**: restage `sim/` assets from `websim/` and compile `wasm.cc header.c unit.cc effect.h` with the emscripten SDK (see `Makefile`).
