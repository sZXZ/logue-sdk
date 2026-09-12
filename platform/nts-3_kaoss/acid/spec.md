### The 8-Parameter "ACID303" Spec

Generative TB-303-style acid synthesizer implemented as an NTS-3 kaoss pad generic FX unit. It runs a tempo-synced 16-step acid bassline while you hold the pad. Unlike a traditional FX, it ignores the practice of only processing input: the generated acid signal is mixed **on top of** the dry input (`out = in + synth`), so the unit is transparent and silent until the pad is touched.

| # | Parameter | Range | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | WAVE | `0`–`1023` | `k_unit_param_type_none` | `0` (full SAW) | Continuous Saw ↔ Square crossfade. `0` = pure Saw, `1023` = pure Square. |
| **2** | ROOT | `0`–`1023` | `k_unit_param_type_midi_note` | `362` (≈A2) | Base pitch. The 10-bit value scales to MIDI 0–127 in DSP (`value * 127 / 1023`, same as the arpeggiator), displayed as a musical pitch. |
| **3** | PATTERN | `0`–`1023` | `k_unit_param_type_none` | `512` | Seed that deterministically generates the pitch / accent / slide sequence via an LCG. |
| **4** | DENSITY | `1`–`16` | `k_unit_param_type_enum` | `12` | Euclidean pulse count `K` — how many of the 16 steps are active. |
| **5** | CUTOFF | `0`–`1023` | `k_unit_param_type_none` | `256` | Base cutoff frequency (exponential scale ~30 Hz – 15 kHz). Mapped to the KAOSS X-axis. |
| **6** | RESON | `0`–`1023` | `k_unit_param_type_none` | `384` | Filter resonance (feedback up to ~3.5, self-oscillating near the top). Mapped to the KAOSS Y-axis. |
| **7** | DECAY | `0`–`2000` | `k_unit_param_type_msec` | `500` | Envelope fall time, displayed in milliseconds (0–2000 ms). |
| **8** | ACID | `0`–`1023` | `k_unit_param_type_none` | `384` | Unit-defining macro: scales filter squelch (resonance + cutoff env sweep up to ~5.6 kHz), glide time (0–150 ms) and accent punch (volume / brightness / decay / snappier release). |

The `k_unit_param_type_msec` descriptor can express values larger than the usual 10-bit knob range (up to 2000 here); only the pad/x-y assigned knobs follow the 0–1023 convention.

---

### Behavior

- **Sequencer**: free-running 16th-note clock synced to the host tempo (`setTempo(float bpm)` / `unit_set_tempo`). Audio only sounds while the pad is touched.
- **Generative pattern**: `PATTERN` seeds a lightweight LCG that fills 16 steps with:
  - **Pitch** from a natural-minor scale (`{0, 2, 3, 5, 7, 8, 10, 12}` semitones), biased strongly toward the root and fifth for a driving line. Step 1 is always the root + accent.
  - **Accent** on ~44% of steps, **Slide** on ~25%.
- **Evo drift (build option **`-DAUTODRIFT`**, built as **`acid_evo`**)**: turns the static pattern into an evolving one. Once per bar 1–4 steps are softly mutated (pitch nudged toward the root/fifth or another scale degree, accents/slides toggled) so the bassline drifts over time; beat 1 always stays root + accent.
- **Euclidean rhythm**: `DENSITY` uses a Björklund algorithm to spread `K` active steps evenly across the 16-step bar.
- **Envelope**: linear attack (~1.2 ms), decay (= `DECAY`), release (~25 ms, shortened by `ACID`). Legato slides continue from the current amplitude (no re-attack → click-free), otherwise steps retrigger.
- **Oscillator**: band-limited wavetable Saw / Square (`osc_bl2_sawf` / `osc_bl2_sqrf`) with fractional-note phase stepping.
- **Glide (Portamento)**: one-pole smoothing on pitch, time 0–150 ms scaled by `ACID`.
- **Filter**: 4-stage Moog-ladder low-pass with tanh saturation. Cutoff base 25 Hz–13 kHz, pushed upward by the envelope + accent (`env_amount = 6000 Hz * (0.35 + 0.65 * acid)`).
- **VCA**: accent gain boost `1 + (0.9 * acid) * (0.4 + step_accent)`, output scaled 0.5× and soft-clipped (`fx_softclipf`).
- **KAOSS pad**: touching the pad gates the sequencer on; releasing starts the envelope release. X/Y offsets are wired to CUTOFF / RESONANCE via the default mappings in `header.c`.

### Files

| File | Role |
| :--- | :--- |
| `effect.h` | All DSP: sequencer, pattern LCG, Euclidean generator, envelope, oscillator, glide, Moog ladder, VCA. |
| `header.c` | Unit descriptor: name, 8 parameter descriptors + default pad mappings. |
| `unit.cc` | C bridge to the runtime (init / render / param / touch / tempo callbacks). |
| `wasm.cc` | WASM bindings for the browser simulator. |

### Build

- **Hardware**: `make` builds and installs both variants:
  - `acid.nts3unit` — static pattern (default).
  - `acid_evo.nts3unit` — automatic pattern drift (`-DAUTODRIFT`), display name "ACID Base Evo".
  - Single variant: `make` target per unit (e.g. `make` in a clean dir, or `make PROJECT=acid_evo UDEFS=-DAUTODRIFT install`).
- **Simulator**: restage `sim/` assets from `websim/` and compile `wasm.cc header.c unit.cc` with the emscripten SDK (see `Makefile`).