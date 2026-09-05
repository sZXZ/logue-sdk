#pragma once
/*
 *  File: effect.h
 *
 *  NTS-3 kaoss pad kit "ACID303" generative acid synthesizer
 *
 *  A TB-303 style acid synthesizer that ignores the audio input and generates
 *  a self-contained, tempo-synced 16-step acid bassline:
 *
 *    - Generative 16-step sequencer (16ths) at host BPM
 *    - "PATTERN" seed deterministically generates pitch / accent / slide data
 *      on a natural-minor scale via a lightweight LCG
 *    - "DENSITY" uses a Bjorklund Euclidean rhythm to spread K active steps
 *      evenly across the 16-step bar
 *    - Band-limited (wavetable) Saw / Square oscillator
 *    - One-pole glide (portamento) on pitch transitions
 *    - 4-stage Moog-ladder resonant low-pass filter with tanh saturation
 *    - Generous linear-attack / decay / release envelope on the VCA and VCF
 *
 *  The clock keeps running whenever tempo is applied, but audio only sounds
 *  while the KAOSS pad is touched. X axis sweeps filter cutoff, Y axis the
 *  resonance. "ACID" is a macro that scales both the glide time and the
 *  intensity of sequenced accents (volume / brightness / decay punch).
 */
#include "processor.h"
#include "unit_genericfx.h"
#include "fx_api.h"
#include "osc_api.h"

namespace
{
constexpr float k_sr = 48000.f;
constexpr float k_sr_recip = 1.f / k_sr;
constexpr uint8_t k_num_steps = 16;
constexpr uint8_t k_step_mask = k_num_steps - 1;

// Natural-minor scale offsets for the generative bass (root cents).
const int8_t k_scale[8] = {0, 2, 3, 5, 7, 8, 10, 12};

// Simple deterministic LCG used only by the (run-once) pattern generator.
static inline uint32_t lcg_next(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return state >> 16; // top 16 bits have the longest period quality
}

// Bjorklund Euclidean rhythm: spread `pulses` onsets as evenly as possible
// across `steps_` positions.
static inline void euclid(uint8_t pulses, uint8_t steps_, uint8_t gates[16])
{
  for (uint8_t i = 0; i < steps_; ++i)
    gates[i] = 0;
  if (pulses == 0)
    return;

  const uint8_t pitch = steps_ / pulses; // base gap
  const uint8_t rem = steps_ % pulses;   // steps to make slightly wider
  uint8_t index = 0;
  for (uint8_t i = 0; i < pulses; ++i)
  {
    gates[index] = 1;
    index += pitch + (i < rem ? 1 : 0);
  }
}
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
    PARAM_PATTERN, // seed for the generative pitch/accent/slide
    PARAM_DENSITY, // Euclidean pulse count (K)
    PARAM_CUTOFF,
    PARAM_RESONANCE,
    PARAM_DECAY,
    PARAM_ACID,     // macro: glide time + accent intensity
    NUM_PARAMS
  };

  struct Params
  {
    float wave;     // 0 = Saw, 1 = Square
    float root;     // MIDI note number 24..84 (bass transposition)
    int32_t pattern; // seed 0..1023
    int32_t density; // 0..1023 -> K pulses 1..16
    float cutoff;   // 0..1
    float resonance;// 0..1
    float decay;    // 0..1 (10ms .. 2000ms)
    float acid;     // 0..1 macro: glide time + accent intensity

    void reset()
    {
      wave = 0.f;
      root = 45.f;          // A2
      pattern = 512;
      density = 768;        // ~12 hits
      cutoff = 256.f / 1023.f;
      resonance = 384.f / 1023.f;
      decay = 384.f / 1023.f;
      acid = 384.f / 1023.f;
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

    case PARAM_PATTERN:
      if (params_.pattern != value)
      {
        params_.pattern = value;
        pattern_dirty_ = true;
      }
      break;

    case PARAM_DENSITY:
      if (params_.density != value)
      {
        params_.density = value;
        density_dirty_ = true;
      }
      break;

    case PARAM_CUTOFF:
      params_.cutoff = param_10bit_to_f32(value);
      break;

    case PARAM_RESONANCE:
      params_.resonance = param_10bit_to_f32(value);
      break;

    case PARAM_DECAY:
      params_.decay = param_10bit_to_f32(value);
      break;

    case PARAM_ACID:
      params_.acid = param_10bit_to_f32(value);
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
    step_accent_ = 0.f;

    step_ = 0;
    clock_accum_ = 0.f;
    samples_per_step_ = k_sr_recip_16th(120.f);

    s1_ = s2_ = s3_ = s4_ = 0.f;

    pattern_dirty_ = true;
    density_dirty_ = true;
    regenPattern();
    regenEuclid();
  }

  void teardown() override final { buffer_ = nullptr; }

  void reset() override final
  {
    phase_ = 0.f;
    note_now_ = params_.root;
    amp_ = 0.f;
    env_state_ = ENV_IDLE;
    gate_ = false;
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
    // Regenerate pattern arrays only when their knobs actually changed.
    if (pattern_dirty_)
      regenPattern();
    if (density_dirty_)
      regenEuclid();

    // Cache current parameter values
    const Params p = params_;

    // Base filter cutoff frequency, 25 Hz .. 13 kHz
    const float kfb = 25.f + 13000.f * clip01f(p.cutoff);

    // Resonance feedback (max ~1.6, gently self-oscillates near the top)
    const float fb = 1.6f * clip01f(p.resonance);

    // ACID macro: maps 0..1 to glide time 0..150 ms.
    const float acid = clip01f(p.acid);
    const float glide_ms = 0.150f * acid + 0.0005f; // avoid a zero time constant

    // Envelope linear increment per sample.
    // Attack ~1.2 ms constant-increment ramp, decay 10 ms .. 2000 ms,
    // release ~25 ms (reduced by ACID for snappier accents).
    const float atk_inc = k_sr_recip / 0.0012f;
    const float dec_inc = k_sr_recip / (0.010f + 1.990f * p.decay);
    const float rel_inc = k_sr_recip / (0.025f * (1.f + 0.5f * acid));

    // Glide (portamento) time, 0 .. 150 ms scaled by ACID.
    const float kg = 1.f - fasterexpf(-k_sr_recip / glide_ms);

    // VCF envelope modulation: moderate base sweep, boosted by ACID on accents.
    const float env_amount = 6000.f * (0.35f + 0.65f * acid);

    for (const float *out_end = out + frames * 2; out != out_end; in += 2, out += 2)
    {
      // --- Sequencer clock (16th notes) --------------------------------------
      clock_accum_ += 1.f;
      if (clock_accum_ >= samples_per_step_)
      {
        clock_accum_ -= samples_per_step_;
        step_ = (step_ + 1) & k_step_mask;

        if (gate_ && hit_[step_])
        {
          const float n = p.root + (float)pitch_[step_];
          if (slide_[step_] && env_state_ != ENV_IDLE)
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
          }
          step_accent_ = accent_[step_] ? 1.f : 0.f;
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
        }
        break;
      case ENV_RELEASE:
        amp_ -= rel_inc;
        if (amp_ <= 0.f)
        {
          amp_ = 0.f;
          env_state_ = ENV_IDLE;
        }
        break;
      case ENV_IDLE:
      default:
        amp_ = 0.f;
        break;
      }

      // Accent: louder + brighter, boosted by ACID intensity.
      const float acc_boost = 1.f + (0.9f * acid) * (0.4f + step_accent_);

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

      // Standard FX pass-through: dry input always passes, the generated acid
      // signal is mixed on top (silent when idle / not touched).
      out[0] = in[0] + s;
      out[1] = in[1] + s;
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
      if (hit_[step_])
      {
        note_target_ = params_.root + pitch_[step_];
        note_now_ = note_target_;
        step_accent_ = accent_[step_] ? 1.f : 0.f;
      }
      else
      {
        note_target_ = params_.root;
        note_now_ = params_.root;
      }
      if (env_state_ == ENV_IDLE)
        amp_ = 0.f;
      env_state_ = ENV_ATTACK;
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

  // Regenerate pitch/accent/slide arrays from the PATTERN seed.
  void regenPattern()
  {
    pattern_dirty_ = false;
    uint32_t state = 0x7F4A7C15u + (uint32_t)(params_.pattern + 1) * 0x9E3779B9u;

    // Bias strongly toward the root & fifth for a driving acid line.
    for (uint8_t i = 0; i < k_num_steps; ++i)
    {
      const uint32_t r = lcg_next(state);
      uint8_t choice;
      switch (r & 3u)
      {
      case 0:
      case 1:
        choice = 0; break;      // root (most common)
      case 2:
        choice = 4; break;      // fifth
      default:
        choice = 1 + (r >> 8) % 7; break; // other scale degrees
      }
      pitch_[i] = k_scale[choice];

      accent_[i] = (lcg_next(state) & 0xFFu) < 112u ? 1 : 0; // ~44% accents
      slide_[i] = (lcg_next(state) & 0xFFu) < 64u ? 1 : 0;   // ~25% slides
    }
    // First step always lands on the root for a solid beat one.
    pitch_[0] = 0;
    accent_[0] = 1;
    slide_[0] = 0;
  }

  // Regenerate the Euclidean hit pattern from DENSITY (K pulses 1..16).
  void regenEuclid()
  {
    density_dirty_ = false;
    const uint8_t k = 1 + (uint8_t)((uint32_t)(params_.density & 1023) * 15u / 1023u);
    euclid(k, k_num_steps, hit_);
  }

  float *buffer_; // valid range:  [buffer_, buffer_ + getBufferSize())
  Params params_;

  // generative pattern (regenerated on knob change)
  int8_t pitch_[k_num_steps];
  uint8_t accent_[k_num_steps];
  uint8_t slide_[k_num_steps];
  uint8_t hit_[k_num_steps];
  bool pattern_dirty_;
  bool density_dirty_;

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
  float step_accent_;

  // filter state
  float s1_, s2_, s3_, s4_;
};
