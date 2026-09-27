#pragma once
/*
 *  File: wt_osc.h
 *
 *  NTS-3 kaoss pad kit "WT" wavetable oscillator unit
 *
 *  A pure sound generator: the audio input is ignored and the output is the
 *  dry/wet mix of the incoming signal and the oscillator. The wavetable
 *  itself is baked at build time by tools/wav2table.py, which folds a wav
 *  from wt/ into a single mip-mapped int16 cycle; the Makefile generates one
 *  build variant (and one .nts3unit) per wav, so this source is compiled
 *  against whichever wt_data.h that variant was given.
 *
 *    - Band-limited wavetable playback: the mip level is picked so that no
 *      partial of the cycle lands above the bake rate, and MORPH crossfades
 *      neighbouring mip levels (both band limited, so the sweep stays clean)
 *    - Detuned unison pair, one per output channel, panned by DETUNE
 *    - Band-limited square sub oscillator one octave down
 *    - 4-stage Moog ladder low-pass per channel with tanh saturation
 *    - Amp envelope triggered by the KAOSS pad (2 ms attack, DECAY, 30 ms
 *      release); X sweeps cutoff, Y sweeps resonance
 */
#include "processor.h"
#include "unit_genericfx.h"
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
// only limit on how dark MORPH may go is interpolation quality -- below
// k_wt_min_step points per output sample the interpolator, not the table, is
// what shapes the waveform, so the sweep stops there:
//   L <= log2(WT_BASE * k_wt_min_step * freq / WT_RATE)
constexpr float k_wt_min_step = 4.f;
constexpr float k_wt_lsafe_gain =
    (float)WT_BASE * k_wt_min_step * (1.f / WT_RATE);

// Above the smallest table the bake rate runs out of harmonics, so fall back
// to the firmware's band-limited sine rather than alias.
constexpr float k_wt_sine_crossover = WT_RATE / (float)WT_MIN_POINTS;
} // namespace

class WavetableOsc : public Processor
{
public:
  // no external memory needed: the wavetable is const rodata
  uint32_t getBufferSize() const override final { return 0U; }

  enum
  {
    PARAM_PITCH = 0U,
    PARAM_MORPH,  // wavetable position (mip crossfade)
    PARAM_DETUNE, // unison spread in cents
    PARAM_SUB,    // sub oscillator level
    PARAM_CUTOFF,
    PARAM_RESON,
    PARAM_DECAY,
    PARAM_MIX, // dry/wet
    NUM_PARAMS
  };

  struct Params
  {
    float note;   // MIDI 0..127
    float morph;  // 0..1
    float detune; // cents, -1200..1200
    float sub;    // 0..1
    float cutoff; // 0..1
    float reson;  // 0..1
    float decay;  // seconds, 0..4
    float mix;    // -1..1

    void reset()
    {
      note = 360.f * 127.f / 1023.f; // ~A2
      morph = 512.f / 1023.f;
      detune = 8.f;
      sub = 384.f / 1023.f;
      cutoff = 640.f / 1023.f;
      reson = 300.f / 1023.f;
      decay = 1.2f; // 1200 ms
      mix = 0.f;
    }

    Params() { reset(); }
  };

  void init(float *allocated_buffer) override final;
  void teardown() override final { buffer_ = nullptr; }
  void reset() override final;

  void setParameter(uint8_t id, int32_t value) override final;

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final;

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final;

private:
  enum EnvState : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_RELEASE };

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

  // envelope
  float amp_;
  EnvState env_state_;
  bool gate_;

  // filter state, per output channel
  float s1l_, s2l_, s3l_, s4l_;
  float s1r_, s2r_, s3r_, s4r_;
};
