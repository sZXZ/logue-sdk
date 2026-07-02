# Auto Tune DSP Bug Analysis

After analyzing the `effect.h` code for the Auto Tune DSP, several critical flaws were found that explain the "harsh loud distortion" and lack of proper autotuning.

Here are the identified bugs:

## 1. Shared Read Pointer Mutation in Stereo Processing (Critical)
**Location:** `Process()` and `readSample()`
**Description:** `readSample()` has a side effect: it advances the class member `read_ptr1` by `ratio`. Because the `Process()` loop calls `readSample()` twice per frame (once for `tuned_l` and once for `tuned_r`), `read_ptr1` is advanced **twice** per sample.
**Impact:** 
- The pitch shift ratio is effectively doubled, completely ruining the intended tuning.
- The left and right channels become completely desynchronized because the right channel reads from an advanced offset compared to the left channel, causing severe phase issues.

## 2. Flawed Circular Buffer Wrap-Around Check (Critical)
**Location:** `readSample()`
**Description:** The collision detection between the read and write pointers is implemented as `abs((int)i1 - (int)shift_idx) < 10`. This absolute difference check fails completely at the circular buffer boundaries. If `i1 = 2047` and `shift_idx = 0`, they are physically adjacent (1 sample apart), but the absolute difference evaluates to `2047`.
**Impact:** The safety jump fails to trigger at the buffer boundaries. The read pointer seamlessly crosses the write pointer, reading discontinuous, disjointed audio data every time a boundary wrap occurs, resulting in loud, harsh glitches.

## 3. Instantaneous Pointer Jumps without Crossfading (Critical)
**Location:** `readSample()`
**Description:** When a collision is successfully detected, `read_ptr1` is instantaneously teleported to the opposite side of the buffer (`read_ptr1 = (shift_idx + SHIFT_BUFFER_SIZE / 2) % SHIFT_BUFFER_SIZE;`). 
**Impact:** Jumping the playback pointer instantly creates a massive waveform discontinuity (a loud pop/click). Because pitch shifters do this constantly, the continuous string of pops manifests as an aggressive, harsh buzzing distortion. A proper pitch shifter must use a dual-tap crossfading approach to smoothly fade out the old pointer while fading in the new one. (Note: Variables like `xfade_active` are declared but completely unused).

## 4. Stale Read Pointer After Gating
**Location:** `Process()` loop
**Description:** When `is_gated` evaluates to `true`, the pitch shifting logic is bypassed, and `read_ptr1` is not updated. However, the write pointer (`shift_idx`) continues to advance. 
**Impact:** By the time the gate opens and pitch shifting resumes, `read_ptr1` may be right on top of the write pointer, triggering an immediate and noisy jump. The read pointer must be kept in sync or intelligently reset when resuming from a gated state.

## 5. Unsafe Unsigned Underflow in Index Calculation
**Location:** `detectPitch()`
**Description:** The AMDF buffer lookups use the formula `(amdf_idx - i + AMDF_BUFFER_SIZE) % AMDF_BUFFER_SIZE`. Because `amdf_idx` and `i` are unsigned, `amdf_idx - i` will underflow to `UINT32_MAX` when `i > amdf_idx`. 
**Impact:** This technically "works" right now only because `AMDF_BUFFER_SIZE` happens to be exactly 1024 (a power of 2) and `UINT32_MAX % 1024` happens to yield the correct wrap-around index. This is extremely fragile. If the buffer size is ever changed to a non-power-of-2 (e.g., 1000), this will immediately break and cause out-of-bounds memory access. 
**Fix:** It should be rewritten to guarantee positive operands before modulo: `(amdf_idx + AMDF_BUFFER_SIZE - i) % AMDF_BUFFER_SIZE`.

## Summary
To fix the distortion, you will need to:
1. Pass the read pointer as a reference/argument to `readSample` or update it outside the function so it only increments once per frame.
2. Implement proper modulo distance calculations for the pointer collision check.
3. Fully implement the missing dual-tap crossfading logic using the unused `xfade_*` variables to eliminate clicks.
