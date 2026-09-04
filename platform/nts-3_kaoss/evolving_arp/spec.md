# EvolvingArp Plugin Specification (NTS-3 Kaoss Pad)

## Overview
EvolvingArp is a **continuous LFO-driven drone** for the KORG NTS-3 Kaoss Pad Kit. There are **no discrete arpeggiator steps**. Instead, a single free-running LFO phasor simultaneously drives three evolving dimensions — chord-note sweep, pitch drift, and wave morph — creating a living, slowly-morphing drone texture with no user intervention required.

Touch the pad to sustain the drone; the sound evolves on its own.

## Project Architecture
- **`header.c`**: Unit name `"EvolvingArp"`, 7 parameters. `PATTERN`, `MODE`, and `WAVE` are all raw `0–1023` sliders representing LFO range/depth.
- **`effect.h`**: `EvolvingArp` class — LFO logic, Hz interpolation across chord notes, ASR envelope.
- **`unit.cc`**: Standard SDK callback bridge.
- **`wasm.cc`**: Web Audio simulator (Emscripten).

## Parameter Specifications

| # | Parameter | Control | Range | Description |
|---|:----------|:--------|:------|:------------|
| 0 | **ROOT** | X-Axis | 0–1023 | Root note of the chord (MIDI note range). |
| 1 | **CHORD** | Y-Axis | 0–8 | Chord type: Single / Major / Minor / Dom 7 / Maj 7 / Min 7 / Sus 4 / Dim / Aug |
| 2 | **PATTERN** | Menu | 0–1023 | LFO chord-sweep range. 0 = drone stays on root note. 1023 = LFO sweeps the full chord span (root → highest note). |
| 3 | **MODE** | Menu | 0–1023 | LFO pitch-drift depth. 0 = no drift. 1023 = quadrature LFO adds ±12 semitones of drift on top of the chord sweep. |
| 4 | **WAVE** | Menu | 0–1023 | LFO wave-morph range. 0 = fixed sine. 1023 = unipolar LFO sweeps the full Sine → Triangle → Square → Saw → Sine morph. |
| 5 | **LEVEL** | DEPTH Knob | 0–1023 | Output volume. |
| 6 | **LFO** | Menu | 0–1023 | Master LFO speed: 0 = frozen, 1–1023 → ~0.02 Hz to ~8 Hz (exponential scale). Controls **all three** of the above simultaneously. |

## Signal Flow

```
Touch pad ──► ASR Envelope
                 │
LFO phasor ──────┤──► [PATTERN] → float note pos → Hz lerp ──► target_hz
                 │                                                   │
                 └──► [MODE] → cosine LFO drift (semitones) ──► drift_ratio
                                                                     │
                                                            target_hz × drift_ratio
                                                                     │
                                                            freq glide (slow smooth)
                                                                     │
                 ┌──► [WAVE] → unipolar LFO → wave morph ──► oscillator
                 │                                                   │
                 └──────────────── amp × level × 0.15 ──────────────►  out
```

## Technical Implementation

### LFO System
A single `lfo_phase_` (0..1 phasor) advances per-sample at the rate set by LFO_RATE:

```
lfo_hz  = 0.02 * exp(t * 5.9915)   where t = LFO_RATE / 1023
lfo_inc = lfo_hz / samplerate
```

Two LFO signals are derived each sample:
- **`lfo_uni`** = `sin(lfo_phase) * 0.5 + 0.5` → `[0..1]` — drives PATTERN and WAVE
- **`lfo_quad`** = `sin(lfo_phase + 0.25)` → `[-1..+1]` — drives MODE (90° offset for independence)

### PATTERN — Chord Note Sweep
The active chord note list is pre-computed as Hz values (`noteHz_[]`). Each sample:
```
max_pos  = (note_count - 1) * (PATTERN / 1023)
note_pos = lfo_uni * max_pos                 // float index into noteHz_[]
hz       = lerp(noteHz_[floor], noteHz_[ceil], frac)
```
The result is a smooth, continuous glide between chord tones at LFO speed.

### MODE — Pitch Drift
A quadrature (cosine-phase) LFO applies an additional semitone bend:
```
drift_semitones = lfo_quad * 12.0 * (MODE / 1023)
drift_ratio     = exp(drift_semitones * ln2/12)   // accurate pitch ratio
target_hz       = chord_hz * drift_ratio
```
At `MODE=0` there is no drift. At `MODE=1023` the pitch wobbles up to ±12 semitones.

### Frequency Glide
All pitch motion is smoothed via a single pole filter with coefficient `0.0008`:
```
smooth_hz += (target_hz - smooth_hz) * 0.0008
```
This produces a very slow, organic glide (~60 ms half-glide time) that prevents any clicks between chord positions.

### WAVE — Wave Morph
```
w = lfo_uni * WAVE          // 0..WAVE
```
The oscillator linearly interpolates across four waveforms in segments:
- `0–256`: Sine → Triangle
- `256–512`: Triangle → Square
- `512–768`: Square → Saw
- `768–1023`: Saw → Sine (wraps back)

At `WAVE=0`, `w` stays at 0 → pure sine. At `WAVE=1023`, the morph cycles through all shapes.

### Envelope (ASR)
No gate, no re-trigger. Simple three-stage amplitude envelope:
| Stage | Time | Trigger |
|:------|:-----|:--------|
| Attack | 40 ms | Touch began |
| Sustain | Full (1.0) | While pad held |
| Release | 300 ms | Touch ended |

### Chord Reference
| Index | Name | Semitone offsets |
|:------|:-----|:-----------------|
| 0 | Single | `{0}` |
| 1 | Major | `{0, 4, 7}` |
| 2 | Minor | `{0, 3, 7}` |
| 3 | Dom 7 | `{0, 4, 7, 10}` |
| 4 | Maj 7 | `{0, 4, 7, 11}` |
| 5 | Min 7 | `{0, 3, 7, 10}` |
| 6 | Sus 4 | `{0, 5, 7}` |
| 7 | Dim | `{0, 3, 6}` |
| 8 | Aug | `{0, 4, 8}` |

## Build
```bash
./docker/run_cmd.sh build nts-3_kaoss/evolving_arp
```
Output: `evolving_arp.nts3unit` — load via NTS-3 Librarian.