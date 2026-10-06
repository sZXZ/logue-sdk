# Wavetable (WT) Unit — Known Bugs

Two implementation bugs were found while comparing `spec.md` to the code. They are
**documented here, not fixed** — both are latent (they do not misbehave in the default
build), but they are real divergences between the code's stated intent and its
behaviour. Both are also described in `spec.md` (§1), which treats the implementation
as the source of truth.

## 1. `-DWTPAD_PITCH` never remaps the pad, only the defaults (Medium)

**Location:** `header.c`, `default_mappings`, lines 109–126.

**Description:** The two `#ifdef WTPAD_PITCH` / `#else` blocks wrap only the
`min/max/default` value fields of the **POSITION** row (line 109–116) and the **LFO DEPTH**
row (line 120–126). The `k_genericfx_param_assign_*` field sits outside the guards in
both rows, so the *assignment never changes*: `X → POSITION`, `Y → LFO DEPTH` in both
build configurations, and PITCH is never mapped anywhere.

The build option exists to select a playable theremin layout — the comments inside the
guards say so: "X plays the wavetable across the pad" / "…and Y sweeps its position
instead". The guards were meant to wrap the **whole rows**: the PITCH row turning into
`assign_x` and the POSITION row turning into `assign_y`. They sit one row late (they
guard the value slot of the row *after* the one they intend to swap).

**Impact:** `make WTPAD_PITCH=1` silently produces a unit with the *same* pad mapping
as the default build. Its only effect today is that the two pad defaults move:
`X → POSITION` becomes `(0..1023, 360)` and `Y → LFO DEPTH` becomes `(0..1023, 512)`.
A user expecting the documented theremin behaviour gets POSITION/LFO DEPTH instead.

**Fix options:**
- Move the guards up so they wrap the assignment fields, giving `X → PITCH` and
  `Y → POSITION` (and a PITCH default value) under `-DWTPAD_PITCH`.
- Or, if the theremin layout is no longer wanted, delete the `#ifdef`s, the
  `WT_EXTRA_DEFINES` plumbing in the Makefile, and the `spec.md` §1 paragraph.

## 2. `Params::reset()` PITCH seed disagrees with the descriptor init (Low)

**Location:** `wt_osc.h`, `Params::reset()`, line 128 (vs `header.c` line 71 and the
memory-map row default at line 107).

**Description:** `reset()` seeds `note = 360.f * 127.f / 1023.f; // ~A2`, but the
descriptor init in `header.c` is `570` (`{0, 1023, 0, 570, k_unit_param_type_midi_note,
…}`). Commit `213ff97` *"updates defaults"* changed the descriptor init 360 → 570 but
left the DSP seed unchanged.

**Impact:** Nearly none on device: the host pushes the descriptor init (`570`) on unit
load, so what actually sounds is B4. The stale `360` seed only shows in cold-start /
offline renders where no host parameter push happens (the WASM sandbox, the host test
harness). It is the same convention gap the acid unit carries (its `Params::reset()`
seeds root 45 while the descriptor init is 237).

**Fix:** Seed `note` from a shared constant so the two cannot drift,
e.g. `#define k_pitch_init 570` in one header included by both `header.c` and
`wt_osc.h`, or simply change the `reset()` line to `570.f * 127.f / 1023.f`.

## 3. Misleading comment: `adsr = 256; // exactly the Pad preset` (Trivial)

**Location:** `wt_osc.h`, `Params::reset()`, line 132.

**Description:** In this unit's grid the ADSR morph spans `0..819` in four `205`-wide
zones, so the five presets sit at `0 / 205 / 410 / 615 / 820` and `256` is a quarter of
the way from Pad (205) toward Perc (410) — `≈0.30 s` attack / `0.26 s` decay / `0.60`
sustain / `0.45 s` release. The "exactly the Pad preset" comment is copy-pasted from the
arpeggiator unit, where the morph spans the full `0..1023` in `256`-wide zones and Pad
is exactly at `256`.

**Impact:** None functionally; the comment misleads anyone reading the defaults.

**Fix:** Delete or correct the comment (e.g. `// a quarter of the way from Pad toward Perc`).