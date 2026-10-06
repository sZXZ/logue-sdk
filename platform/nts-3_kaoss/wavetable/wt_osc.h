#pragma once
/*
 *  File: wt_osc.h
 *
 *  NTS-3 kaoss pad kit "WT" wavetable oscillator unit
 *
 *  A wavetable voice that layers under the pad: the audio input passes through
 *  untouched on both channels and the generated signal is mixed on top of it,
 *  the same additive pass-through the acid unit uses (no dry/wet crossfade, no
 *  level trim -- the soft clip is applied to the wet signal before it is
 *  added). The wavetable itself is baked at build time by tools/wav2table.py,
 *  which folds a wav from wt/ into a single mip-mapped int16 cycle; the
 *  Makefile generates one build variant (and one .nts3unit) per wav, so this
 *  source is compiled against whichever wt_data.h that variant was given.
 *
 *    - Band-limited wavetable playback: POSITION crossfades neighbouring mip
 *      levels. Every level is band limited at its own rate by the baker, so
 *      the sweep is clean at any pitch and stays identical above the point
 *      where the interpolator, not the table, shapes the waveform
 *    - Free-running LFO sweeping POSITION, so a held note keeps moving
 *    - Detuned unison pair, one per output channel, panned by DETUNE
 *    - Band-limited square sub oscillator one octave down
 *    - ADSR amp envelope: the arpeggiator unit's 5 preset morph (Pluck, Pad,
 *      Percussive, Swell, Long release) over 0..819, with a DRONE zone at the
 *      top that holds full sustain and sounds without touch
 *    - Optional generative 16 step pattern (PATTERN seed, tempo synced to the
 *      host 4PPQN clock) played under the touch, like the acid unit
 *
 *  Build options, the acid unit's set, cross producted with the wav list by the
 *  Makefile (so every wav in wt/ builds as six units):
 *    - -DAUTODRIFT ("<wav>_evo") turns the static 16 step pattern into an
 *      evolving one: once per bar a few steps are softly mutated, so the line
 *      drifts over time instead of looping forever
 *    - -DUNIT_OUT_LEFT / -DUNIT_OUT_RIGHT ("<wav>_L" / "<wav>_R") add the
 *      voice to a single output channel while the input still passes on both,
 *      so two copies can be chained and mixed as separate instruments
 *
 *  There is deliberately no filter and no dry/wet crossfade: on the kaoss
 *  those are another unit's job, and CUTOFF/RESON/MIX are better spent on
 *  wavetable specific controls. Mixing itself is the acid unit's additive
 *  pass-through, so this unit layers with the input instead of replacing it.
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "utils/float_math.h"
#include "wt_data.h" // generated per wav variant (build/gen/<wav>/wt_data.h)

namespace
{
constexpr float k_sr = 48000.f;
constexpr float k_sr_recip = 1.f / k_sr;

// int16 tables hold +-1.0 over +-32768
constexpr float k_int16_recip = 1.f / 32768.f;

// Index of the darkest mip level (largest index = fewest points).
constexpr float k_lmax = (float)(WT_LEVEL_COUNT - 1);

// The baker low-passes every level to half of its own rate, so a level never
// folds back into the audio band no matter which pitch reads it: level L keeps
// partials up to (WT_BASE / 2^L) / 2 harmonics over WT_BASE / 2^L points.  The
// only limit on how dark POSITION may go is interpolation quality -- below
// k_wt_min_step points per output sample the interpolator, not the table, is
// what shapes the waveform, so the sweep stops there:
//   L <= log2(WT_BASE * k_wt_min_step * freq / WT_RATE)
constexpr float k_wt_min_step = 4.f;
constexpr float k_wt_lsafe_gain =
    (float)WT_BASE * k_wt_min_step * (1.f / WT_RATE);

// Above the smallest table the bake rate runs out of harmonics, so fall back
// to the firmware's band-limited sine rather than alias.
constexpr float k_wt_sine_crossover = WT_RATE / (float)WT_MIN_POINTS;

// Free running LFO range, exponential 0.05 Hz .. 20 Hz.
constexpr float k_lfo_rate_min = 0.05f;
constexpr float k_lfo_rate_range = 400.f; // 0.05 * 400 = 20 Hz
} // namespace

class WavetableOsc : public Processor
{
public:
  // no external memory needed: the wavetable is const rodata
  uint32_t getBufferSize() const override final { return 0U; }

  enum
  {
    PARAM_PITCH = 0U,
    PARAM_POSITION, // wavetable position (mip crossfade), X axis
    PARAM_LFO_RATE,
    PARAM_LFO_DEPTH, // LFO -> POSITION, Y axis
    PARAM_ADSR,      // envelope morph, DRONE at the top
    PARAM_PATTERN,   // 0 = off, else seed of the 16 step generator
    PARAM_SUB,
    PARAM_DETUNE,
    NUM_PARAMS
  };

  // The arpeggiator unit's ADSR macro, ported verbatim so the two units feel
  // the same: a 0..1023 morph across five presets. Above k_drone_threshold
  // the preset morph is replaced by a binary DRONE mode (full sustain, no
  // envelope, no touch required).
  struct AdsrPreset
  {
    float attack;   // seconds
    float decay;    // seconds
    float sustain;  // 0..1
    float release;  // seconds
  };

  // 0..k_adsr_span is the arpeggiator's five preset morph, the rest is DRONE.
  static constexpr uint16_t k_adsr_span = 820;
  static constexpr uint16_t k_drone_threshold = k_adsr_span;
  static constexpr float k_adsr_zone = (float)k_adsr_span * 0.25f;

  struct Params
  {
    float note;      // MIDI 0..127, root of the pattern
    float position;  // 0..1
    float lfo_rate;  // Hz
    float lfo_depth; // 0..1
    uint16_t adsr;   // 0..1023 raw
    int32_t pattern; // 0..1023 seed, 0 = sequencer off
    float sub;       // 0..1
    float detune;    // cents

    void reset()
    {
      note = 360.f * 127.f / 1023.f;
      position = 512.f / 1023.f;
      lfo_rate = k_lfo_rate_min * 1.f; // set properly below
      lfo_depth = 0.f;                // LFO is opt in
      adsr = 256;
      pattern = 0;                    // no sequencer until asked
      sub = 384.f / 1023.f;
      detune = 8.f;
      lfo_rate = rate_from_param(512);
    }

    // Exponential 0.05 .. 20 Hz over a 0..1023 param.
    static float rate_from_param(int32_t value)
    {
      return k_lfo_rate_min * fasterexpf(5.991f * ((float)value * (1.f / 1023.f)));
    }

    Params() { reset(); }
  };

  void init(float *allocated_buffer) override final;
  void teardown() override final { buffer_ = nullptr; }
  void reset() override final;

  void setParameter(uint8_t id, int32_t value) override final;
  const char *getParameterStrValue(uint8_t id, int32_t value) const override final;

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final;

  // Host clock: 16.16 fixed point BPM and 4PPQN ticks (one per 16th note).
  void setTempo(float bpm) override final;
  void tempo4ppqnTick(uint32_t counter) override final;

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final;

private:
  enum EnvState : uint8_t
  {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
  };

  // ---- ADSR -----------------------------------------------------------------
  static inline AdsrPreset morphAdsr(float t, AdsrPreset a, AdsrPreset b)
  {
    AdsrPreset r;
    r.attack = a.attack + t * (b.attack - a.attack);
    r.decay = a.decay + t * (b.decay - a.decay);
    r.sustain = a.sustain + t * (b.sustain - a.sustain);
    r.release = a.release + t * (b.release - a.release);
    return r;
  }

  // Four equal zones across 0..k_adsr_span walk the arpeggiator unit's five
  // presets in the same order, so the knob lands on Pluck / Pad / Perc /
  // Swell / Long release at the same five points it does there.
  inline AdsrPreset currentAdsr() const
  {
    // Function local static keeps this C++11 ODR safe.
    static const AdsrPreset kPresets[5] = {
        {0.002f, 0.06f, 0.0f, 0.04f},  // 0: Pluck
        {0.4f, 0.3f, 0.8f, 0.6f},      // 1: Pad
        {0.001f, 0.12f, 0.0f, 0.015f}, // 2: Percussive
        {0.25f, 0.4f, 0.9f, 0.35f},    // 3: Swell
        {0.01f, 0.2f, 0.6f, 1.2f},     // 4: Long Release
    };
    const float v = clipminmaxf(0.f, (float)params_.adsr, (float)(k_adsr_span - 1));
    const uint8_t zone = (uint8_t)(v / k_adsr_zone); // 0..3
    return morphAdsr((v - (float)zone * k_adsr_zone) * (1.f / k_adsr_zone),
                     kPresets[zone], kPresets[zone + 1]);
  }

  inline bool droneMode() const { return params_.adsr >= k_drone_threshold; }

  // ---- Generative 16 step pattern (ported from the acid unit) ---------------
  static constexpr uint8_t k_num_steps = 16;
  static constexpr uint8_t k_step_mask = k_num_steps - 1;

  // Minor pentatonic, the acid unit's scale.
  static const int8_t *scale();

  static inline uint32_t lcgNext(uint32_t &state)
  {
    state = state * 1664525u + 1013904223u;
    return state >> 16;
  }

  static inline void euclid(uint8_t pulses, uint8_t steps_, uint8_t gates[16])
  {
    for (uint8_t i = 0; i < steps_; ++i)
      gates[i] = 0;
    if (pulses == 0)
      return;
    const uint8_t pitch = steps_ / pulses;
    const uint8_t rem = steps_ % pulses;
    uint8_t index = 0;
    for (uint8_t i = 0; i < pulses; ++i)
    {
      gates[index] = 1;
      index = (uint8_t)(index + pitch + (i < rem ? 1 : 0));
      if (index >= steps_)
        index = (uint8_t)(index - steps_);
    }
  }

  void regenPattern();
#ifdef AUTODRIFT
  void driftPattern(); // once per bar, mutate a few steps (evolving line)
#endif
  void triggerStep();   // fire the current step's note (sequencer running)
  void triggerRoot();   // fire the root note (sequencer off)

  // Cubic (Catmull-Rom) read of one cycle, wrapping at the table length.
  static inline float tap(const int16_t *__restrict t, int n, float pos)
  {
    const int i = (int)pos;
    const float fr = pos - (float)i;
    const int n1 = i + 1 == n ? 0 : i + 1;
    const int i0 = i == 0 ? n - 1 : i - 1;
    const int n2 = n1 + 1 == n ? 0 : n1 + 1;

    const float y0 = (float)t[i0] * k_int16_recip;
    const float y1 = (float)t[i] * k_int16_recip;
    const float y2 = (float)t[n1] * k_int16_recip;
    const float y3 = (float)t[n2] * k_int16_recip;

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * fr + c2) * fr + c1) * fr + y1;
  }

  float *buffer_; // unused, but part of the Processor contract
  Params params_;

  // oscillator phases
  float phase_a_, phase_b_, phase_sub_;
  float note_target_, note_now_;
  float lfo_phase_;

  // envelope
  float amp_;
  float env_phase_; // level captured when the release starts
  EnvState env_state_;
  float step_accent_;
  bool gate_;

  // sequencer
  int8_t pitch_[k_num_steps];
  uint8_t hit_[k_num_steps];
  uint8_t slide_[k_num_steps];
  uint8_t accent_[k_num_steps];
  uint8_t step_;
  float clock_accum_;
  float samples_per_step_;
  uint32_t host_counter_;
  bool host_sync_valid_;
  bool pattern_dirty_;
#ifdef AUTODRIFT
  uint32_t drift_state_; // evolving-pattern mutation RNG
#endif
};
