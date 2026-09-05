#pragma once
/*
 *  File: effect.h
 *
 *  NTS-3 kaoss pad kit "NTS-303" acid generator effect
 *
 *  A TB-303 style acid synthesizer. Ignores the audio input and generates
 *  a self-contained mono acid bassline:
 *
 *    - 16-step tempo-synced sequencer (16ths) playing a built-in acid
 *      pattern transposed by the ROOT note parameter
 *    - Band-limited (wavetable) Saw / Square oscillator
 *    - One-pole smoothed oscillator pitch (Glide / portamento)
 *    - 4-stage Moog-ladder resonant low-pass filter with tanh saturation
 *    - Simple exponential Decay envelope on the VCA and VCF
 *
 *  The sequencer advances at the device/external tempo (setTempo). Notes are
 *  only audible while the KAOSS pad is touched: touching gates the envelope
 *  and each subsequent 16th-note step retriggers the pattern. X axis sweeps
 *  the filter cutoff, Y axis controls the resonance.
 *
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "fx_api.h"
#include "osc_api.h"

namespace
{
constexpr float k_sr = 48000.f;
constexpr float k_sr_recip = 1.f / k_sr;

// 16-step acid bassline (A A A A | E D F ...), transposed by ROOT.
// pitch: semitone offset from root (valid only when hit == 1)
const int8_t k_pitch[16] = {0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 10, 0, 8, 0, 0, 0};
// hit:  1 = step sounds
const uint8_t k_hit[16] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0};
// accent: 1 = emphasized step (louder, brighter, tighter decay)
const uint8_t k_accent[16] = {1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0};
// slide: 1 = legato glide from previous note (no re-attack)
const uint8_t k_slide[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0};
constexpr uint8_t k_num_steps = 16;
constexpr uint8_t k_step_mask = k_num_steps - 1;
}

class Effect : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0x40000U; } // 1 MB

  // audio parameters
  enum
  {
    PARAM_WAVE = 0U,
    PARAM_ROOT,
    PARAM_CUTOFF,
    PARAM_RESONANCE,
    PARAM_ENV_MOD,
    PARAM_DECAY,
    PARAM_ACCENT,
    PARAM_GLIDE,
    NUM_PARAMS
  };

  // Note: Make sure that default param values correspond to declarations in header.c
  struct Params
  {
    float wave;      // 0 = Saw, 1 = Square
    float root;      // MIDI note number 24..84 (pattern root)
    float cutoff;    // 0..1
    float resonance; // 0..1
    float env_mod;   // 0..1
    float decay;     // 0..1 (10ms .. 2000ms)
    float accent;    // 0..1
    float glide;     // 0..1 (0ms .. 60ms)

    void reset()
    {
      wave = 0.f;
      root = 45.f;          // A2
      cutoff = 256.f / 1023.f;
      resonance = 384.f / 1023.f;
      env_mod = 512.f / 1023.f;
      decay = 384.f / 1023.f;
      accent = 256.f / 1023.f;
      glide = 192.f / 1023.f;
    }

    Params() { reset(); }
  };

  inline void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PARAM_WAVE:
      params_.wave = param_10bit_to_f32(value);
      break;

    case PARAM_ROOT:
      params_.root = static_cast<float>(value); // MIDI note 24..84
      break;

    case PARAM_CUTOFF:
      params_.cutoff = param_10bit_to_f32(value);
      break;

    case PARAM_RESONANCE:
      params_.resonance = param_10bit_to_f32(value);
      break;

    case PARAM_ENV_MOD:
      params_.env_mod = param_10bit_to_f32(value);
      break;

    case PARAM_DECAY:
      params_.decay = param_10bit_to_f32(value);
      break;

    case PARAM_ACCENT:
      params_.accent = param_10bit_to_f32(value);
      break;

    case PARAM_GLIDE:
      params_.glide = param_10bit_to_f32(value);
      break;

    default:
      break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *wave_names[2] = {"SAW", "SQR"};

    switch (index)
    {
    case PARAM_WAVE:
      if (value >= 0 && value < 2)
        return wave_names[value];
      break;

    default:
      break;
    }

    return nullptr;
  }

  // life-cycle methods
  void init(float *allocated_buffer) override final
  {
    buffer_ = allocated_buffer;
    params_.reset();

    phase_ = 0.f;
    note_target_ = params_.root;
    note_now_ = params_.root;
    amp_ = 0.f;
    env_state_ = ENV_IDLE;
    gate_ = false;
    note_sounding_ = false;
    step_accent_ = 0.f;

    step_ = 0;
    clock_accum_ = 0.f;
    samples_per_step_ = k_sr_recip_16th(120.f);

    s1_ = s2_ = s3_ = s4_ = 0.f;
  }

  void teardown() override final { buffer_ = nullptr; }

  void reset() override final
  {
    phase_ = 0.f;
    note_now_ = params_.root;
    amp_ = 0.f;
    env_state_ = ENV_IDLE;
    gate_ = false;
    note_sounding_ = false;
    step_accent_ = 0.f;
    step_ = 0;
    clock_accum_ = 0.f;
    s1_ = s2_ = s3_ = s4_ = 0.f;
  }

  // Tempo sync: BPM in beats per minute (float). Sequencer clocks 16th notes.
  inline void setTempo(float bpm) override final
  {
    if (bpm < 1.f)
      bpm = 1.f;
    samples_per_step_ = k_sr_recip_16th(bpm);
  }

  // audio processing callbacks
  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in; // self-contained generator, ignore input

    // Cache current parameter values
    const Params p = params_;

    // Base filter cutoff frequency, 25 Hz .. 13 kHz
    const float kfb = 25.f + 13000.f * clip01f(p.cutoff);

    // Resonance feedback (max ~1.6, gently self-oscillates near the top)
    const float fb = 1.6f * clip01f(p.resonance);

    // Envelope linear increment per sample.
    // Attack ~1.2 ms constant-increment ramp (smooth, glitch-free like the
    // reference arpeggiator), decay 10 ms .. 2000 ms, release ~25 ms.
    const float atk_inc = k_sr_recip / 0.0012f;
    const float dec_inc = k_sr_recip / (0.010f + 1.990f * p.decay);
    const float rel_inc = k_sr_recip / 0.025f;

    // Glide (portamento) coefficient, 0 ms .. 60 ms
    const float kg = 1.f - fasterexpf(-k_sr_recip / (0.001f + 0.060f * p.glide));

    // VCF envelope modulation depth, scaled by env mod
    const float env_amount = 4800.f * clip01f(p.env_mod);
    const float acc = p.accent;

    for (const float *out_end = out + frames * 2; out != out_end; out += 2)
    {
      // --- Sequencer clock (16th notes) --------------------------------------
      clock_accum_ += 1.f;
      if (clock_accum_ >= samples_per_step_)
      {
        clock_accum_ -= samples_per_step_;
        step_ = (step_ + 1) & k_step_mask;

        if (gate_ && k_hit[step_])
        {
          const float n = p.root + (float)k_pitch[step_];
          if (k_slide[step_] && env_state_ != ENV_IDLE)
          {
            // Legato slide: glide the pitch, no re-attack
            note_target_ = n;
          }
          else
          {
            // Retrigger. Legato if the note is still sounding: keep the
            // current amp and resume the attack from there (no click).
            note_target_ = n;
            note_now_ = n;
            if (env_state_ == ENV_IDLE)
              amp_ = 0.f;
            env_state_ = ENV_ATTACK;
            note_sounding_ = true;
          }
          step_accent_ = k_accent[step_] ? 1.f : 0.f;
        }
      }

      // --- Gate close -> release ---------------------------------------------
      if (!gate_ && env_state_ != ENV_IDLE)
        env_state_ = ENV_RELEASE;

      // --- Envelope state machine (linear attack, decay, release) ------------
      switch (env_state_)
      {
      case ENV_ATTACK:
        amp_ += atk_inc;
        if (amp_ >= 1.f)
        {
          amp_ = 1.f;
          env_state_ = ENV_DECAY;
        }
        break;
      case ENV_DECAY:
        amp_ -= dec_inc;
        if (amp_ <= 0.f)
        {
          amp_ = 0.f;
          env_state_ = ENV_IDLE;
          note_sounding_ = false;
        }
        break;
      case ENV_RELEASE:
        amp_ -= rel_inc;
        if (amp_ <= 0.f)
        {
          amp_ = 0.f;
          env_state_ = ENV_IDLE;
          note_sounding_ = false;
        }
        break;
      case ENV_IDLE:
      default:
        amp_ = 0.f;
        break;
      }

      // Accent: louder + brighter on accented steps
      const float acc_boost = 1.f + 0.6f * acc * (0.4f + step_accent_);

      // --- Glide: one-pole smoothing toward the target note ------------------
      note_now_ += kg * (note_target_ - note_now_);

      // Convert smoothed pitch to fractional MIDI note and frequency
      const float note_f = clipminmaxf(0.f, note_now_, 150.f);
      const uint8_t n0 = (uint8_t)note_f;
      const float frac = note_f - n0;

      // --- Oscillator phase ---
      const float freq = linintf(frac, osc_notehzf(n0), osc_notehzf(n0 + 1));
      phase_ += freq * k_sr_recip;
      phase_ -= (float)(uint32_t)phase_; // wrap to [0,1)

      // Band-limited wavetable index for anti-aliasing
      const float idx = clipmaxf(note_f, 127.f) * (6.f / 127.f);

      float osc;
      if (p.wave < 0.5f)
        osc = osc_bl2_sawf(phase_, idx);
      else
        osc = osc_bl2_sqrf(phase_, idx);

      // --- VCF: envelope+accent pushes the cutoff upward ----------------------
      const float fc = clipminf(kfb, kfb + env_amount * acc_boost * amp_);
      const float k = 1.f - fasterexpf(-M_TWOPI * fc * k_sr_recip);

      // --- 4-stage Moog ladder low-pass filter -------------------------------
      const float u = tanh(osc); // input waveshaper
      s1_ += k * (tanh(u - fb * s4_) - s1_);
      s2_ += k * (tanh(s1_) - s2_);
      s3_ += k * (tanh(s2_) - s3_);
      s4_ += k * (tanh(s3_) - s4_);

      // --- VCA with accent gain boost -----------------------------------------
      const float out_sample = s4_ * amp_ * acc_boost * 0.5f;

      // soft clip to keep things from exploding
      const float s = fx_softclipf(0.15f, out_sample);

      out[0] = s;
      out[1] = s;
    }
  }

  inline void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    switch (phase)
    {
    case k_unit_touch_phase_began:
      gate_ = true;
      step_accent_ = 0.f;
      // Trigger the current step immediately (fall back to root on rests)
      if (k_hit[step_])
      {
        note_target_ = params_.root + k_pitch[step_];
        note_now_ = note_target_;
        step_accent_ = k_accent[step_] ? 1.f : 0.f;
      }
      else
      {
        note_target_ = params_.root;
        note_now_ = params_.root;
      }
      if (env_state_ == ENV_IDLE)
        amp_ = 0.f;
      env_state_ = ENV_ATTACK;
      note_sounding_ = true;
      break;
    case k_unit_touch_phase_moved:
      break;
    case k_unit_touch_phase_ended:
    case k_unit_touch_phase_cancelled:
      gate_ = false; // envelope decays out
      break;
    default:
      break;
    }
  }

private:
  // Samples per 16th-note step at the given BPM
  static inline float k_sr_recip_16th(float bpm)
  {
    return (60.f / bpm) * k_sr * 0.25f;
  }

  float *buffer_; // valid range:  [buffer_, buffer_ + getBufferSize())
  Params params_;

  // sequencer state
  uint8_t step_;
  float clock_accum_;
  float samples_per_step_;

  // voice state
  float phase_;
  float note_target_;
  float note_now_;
  float amp_;
  enum EnvState : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_RELEASE };
  EnvState env_state_;
  bool gate_;
  bool note_sounding_;
  float step_accent_;

  // filter state
  float s1_, s2_, s3_, s4_;
};