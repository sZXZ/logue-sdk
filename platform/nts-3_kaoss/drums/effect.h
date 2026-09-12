#pragma once
/*
 *  File: effect.h
 *
 *  NTS-3 kaoss pad kit "DRUMS" generative drum machine
 *
 *  A generative drum machine implemented as an NTS-3 kaoss pad generic FX unit:
 *    - 16-step tempo-synced drum sequencer (16ths) at host BPM
 *    - "PATTERN" seed deterministically generates kick / snare / hi-hat patterns via LCG
 *    - "DENSITY" uses a Bjorklund Euclidean rhythm to control active steps (1..16)
 *
 *  Build option -DAUTODRIFT (the "<name>_evo" variants) turns the static
 *  pattern into an evolving one: once per bar a handful of steps are softly
 *  mutated so the groove drifts over time.
 *    - Kick: tuned sine sweep (~60 -> 30 Hz), beater transient, analog saturation
 *    - Snare: tuned sine body (150..280 Hz) + high-passed noise burst (~800 Hz)
 *    - Hi-Hat: band-pass filtered metallic noise (8..10 kHz), closed / open modes
 *    - "DECAY" sets an audible note length for every voice (0..2000 ms);
 *      envelopes fall linearly to silence over that length so short decay =
 *      tight pluck, long decay = ringing tail.
 *    - "TONE" shared brightness & snare body tuning
 *    - "DRIVE" soft-clip bus saturation with input gain scaling
 *
 *  Audio passes through dry (`out = in + drums`) and drums only sound while
 *  the KAOSS pad is touched. X axis controls KICK punch/level, Y axis controls
 *  HIHAT level and closed/open character.
 */

#include "processor.h"
#include "unit_genericfx.h"
#include "fx_api.h"
#include "osc_api.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float k_sr = 48000.f;
constexpr float k_sr_recip = 1.f / k_sr;
constexpr uint8_t k_num_steps = 16;
constexpr uint8_t k_step_mask = k_num_steps - 1;

// Deterministic LCG used by pattern generator
static inline uint32_t lcg_next(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return state >> 16;
}

// Bjorklund Euclidean rhythm: spread `pulses` onsets evenly across `steps_`
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
    index += pitch + (i < rem ? 1 : 0);
  }
}
}

class Effect : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0U; }

  enum
  {
    PARAM_PATTERN = 0U,
    PARAM_DENSITY,
    PARAM_KICK,
    PARAM_SNARE,
    PARAM_HIHAT,
    PARAM_DECAY,
    PARAM_TONE,
    PARAM_DRIVE,
    NUM_PARAMS
  };

  struct Params
  {
    int32_t pattern; // seed 0..1023
    int32_t density; // 1..16
    int32_t kick;    // 0..1023 (mapped to X)
    int32_t snare;   // 0..1023
    int32_t hihat;   // 0..1023 (mapped to Y)
    int32_t decay;   // 0..2000 ms
    int32_t tone;    // 0..1023
    int32_t drive;   // 0..1023 (mapped to Depth)

    void reset()
    {
      pattern = 512;
      density = 8;
      kick = 768;
      snare = 512;
      hihat = 384;
      decay = 400;
      tone = 512;
      drive = 256;
    }

    Params() { reset(); }
  };

  inline void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
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

    case PARAM_KICK:
      params_.kick = value;
      break;

    case PARAM_SNARE:
      params_.snare = value;
      break;

    case PARAM_HIHAT:
      params_.hihat = value;
      break;

    case PARAM_DECAY:
      params_.decay = value;
      break;

    case PARAM_TONE:
      params_.tone = value;
      break;

    case PARAM_DRIVE:
      params_.drive = value;
      break;

    default:
      break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    (void)allocated_buffer;
    params_.reset();

    step_ = 0;
    clock_accum_ = 0.f;
    samples_per_step_ = k_sr_recip_16th(120.f);
    host_counter_ = 0;
    host_sync_valid_ = false;
    gate_ = false;
    touch_gain_ = 0.f;

    kick_amp_ = 0.f;
    kick_pitch_env_ = 0.f;
    kick_phase_ = 0.f;
    kick_attack_ = 1.f;
    kick_amp_dec_ = 0.f;
    kick_pitch_decay_coeff_ = 0.999f;

    snare_body_amp_ = 0.f;
    snare_noise_amp_ = 0.f;
    snare_pitch_env_ = 0.f;
    snare_phase_ = 0.f;
    snare_attack_ = 1.f;
    snare_noise_hp_state_ = 0.f;
    snare_body_dec_ = 0.f;
    snare_noise_dec_ = 0.f;
    snare_pitch_decay_coeff_ = 0.999f;

    hat_amp_ = 0.f;
    hat_attack_ = 1.f;
    hat_bpf_s1_ = 0.f;
    hat_bpf_s2_ = 0.f;
    hat_dec_ = 0.f;

    tone_lp_state_ = 0.f;

    pattern_dirty_ = true;
    density_dirty_ = true;
    regenPattern();
    regenEuclid();
  }

  void teardown() override final {}

  void reset() override final
  {
    step_ = 0;
    clock_accum_ = 0.f;
    host_counter_ = 0;
    host_sync_valid_ = false;
    gate_ = false;
    touch_gain_ = 0.f;

    kick_amp_ = 0.f;
    kick_pitch_env_ = 0.f;
    kick_phase_ = 0.f;
    kick_attack_ = 1.f;

    snare_body_amp_ = 0.f;
    snare_noise_amp_ = 0.f;
    snare_pitch_env_ = 0.f;
    snare_phase_ = 0.f;
    snare_attack_ = 1.f;
    snare_noise_hp_state_ = 0.f;

    hat_amp_ = 0.f;
    hat_attack_ = 1.f;
    hat_bpf_s1_ = 0.f;
    hat_bpf_s2_ = 0.f;

    tone_lp_state_ = 0.f;
  }

  inline void setTempo(float bpm) override final
  {
    if (bpm < 1.f)
      bpm = 1.f;
    samples_per_step_ = k_sr_recip_16th(bpm);
  }

  // Host 4PPQN sync callback: one tick per 16th note on the global clock.
  inline void tempo4ppqnTick(uint32_t counter) override final
  {
    host_counter_ = counter;
    host_sync_valid_ = true;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    if (pattern_dirty_)
      regenPattern();
    if (density_dirty_)
      regenEuclid();

    const Params p = params_;
    const float kick_norm = clip01f(p.kick / 1023.f);
    const float snare_norm = clip01f(p.snare / 1023.f);
    const float tone_norm = clip01f(p.tone / 1023.f);
    const float drive_norm = clip01f(p.drive / 1023.f);

    // DECAY multiplier: at default 400ms = 1.0x, at 0ms = 0.5x, at 2000ms = 3.0x
    const float decay_mult = 0.5f + 2.5f * (clipminmaxf(0.f, (float)p.decay, 2000.f) / 2000.f);

    // Hi-hat mode & level
    const bool hat_is_open = (p.hihat >= 512);
    const float hat_level = hat_is_open ? (0.35f + 0.65f * ((p.hihat - 512.f) / 511.f)) : (p.hihat / 511.f);

    // Snare parameters
    const float snare_base_hz = 150.f + 130.f * tone_norm;
    const float snare_noise_mix = 0.35f + 0.45f * snare_norm;
    const float snare_body_mix = 1.0f - 0.4f * snare_norm;
    const float snare_level = snare_norm * 0.34f;

    // Kick parameters
    const float kick_base_hz = 40.f;
    const float kick_sweep_depth = 95.f + 45.f * kick_norm;
    const float kick_gain = 0.22f + 0.50f * kick_norm;

    // Master Tone Shelf filter coeff & tilt
    const float tone_k = 1.f - fasterexpf(-M_TWOPI * 3000.f * k_sr_recip);
    const float tone_tilt = (tone_norm - 0.5f) * 0.6f;

    // Master Drive
    // DRIVE saturation is deliberately soft at the default so the mix stays
    // open (no flat-top clipping); cranking it pushes into heavy saturation.
    const float drive_gain = 0.75f + 1.75f * drive_norm;
    const float drive_comp = 1.f / (1.f + 0.6f * drive_norm);

    // Hat filter parameters (SVF BPF around 7.5k - 10.5k Hz)
    const float hat_fc = 7500.f + 3000.f * tone_norm;
    const float hat_f = 2.f * sinf((float)M_PI * hat_fc * k_sr_recip);
    const float hat_q = 0.35f;

    // Snare HPF coeff (~800 Hz)
    const float snare_hp_k = 1.f - fasterexpf(-M_TWOPI * 800.f * k_sr_recip);

    // Ramp rates for pad touch fade-in / fade-out (zero clicks)
    const float ramp_up = k_sr_recip / 0.002f;   // 2 ms ramp
    const float ramp_down = k_sr_recip / 0.025f; // 25 ms fade out

    // Fast attack increments (1 ms) to round off drum onsets (no click transients)
    const float attack_inc = k_sr_recip / 0.001f;

    for (const float *out_end = out + frames * 2; out != out_end; in += 2, out += 2)
    {
      // --- Sequencer clock (16th notes) --------------------------------------
      clock_accum_ += 1.f;

      // Phase-lock to the host's global 4PPQN grid: once ticks arrive, any
      // boundary that differs from our free-running step snaps us back onto
      // the transport clock so we never drift from the global sync time.
      if (host_sync_valid_)
      {
        const uint8_t host_step = (uint8_t)(host_counter_ % k_num_steps);
        if (host_step != step_)
        {
          step_ = host_step;
          clock_accum_ = 0.f;

#ifdef AUTODRIFT
          if (host_step == 0)
            driftPattern();
#endif

          if (gate_ && euclid_mask_[step_])
          {
            if (kick_hit_[step_])
              triggerKick(decay_mult, kick_norm);
            if (snare_hit_[step_])
              triggerSnare(decay_mult, snare_norm);
            if (hihat_hit_[step_])
              triggerHihat(decay_mult, hat_is_open);
          }
        }
      }

      if (clock_accum_ >= samples_per_step_)
      {
        clock_accum_ -= samples_per_step_;
        step_ = (step_ + 1) & k_step_mask;

#ifdef AUTODRIFT
        if (step_ == 0)
          driftPattern();
#endif

        if (gate_ && euclid_mask_[step_])
        {
          if (kick_hit_[step_])
            triggerKick(decay_mult, kick_norm);
          if (snare_hit_[step_])
            triggerSnare(decay_mult, snare_norm);
          if (hihat_hit_[step_])
            triggerHihat(decay_mult, hat_is_open);
        }
      }

      // --- Pad Touch Smoothing -----------------------------------------------
      if (gate_)
      {
        touch_gain_ += ramp_up;
        if (touch_gain_ > 1.f)
          touch_gain_ = 1.f;
      }
      else
      {
        touch_gain_ -= ramp_down;
        if (touch_gain_ < 0.f)
          touch_gain_ = 0.f;
      }

      // If silent and pad not touched, pass dry audio directly
      if (touch_gain_ <= 0.f && kick_amp_ <= 0.0001f && snare_body_amp_ <= 0.0001f &&
          snare_noise_amp_ <= 0.0001f && hat_amp_ <= 0.0001f)
      {
        out[0] = in[0];
        out[1] = in[1];
        continue;
      }

      // --- 1. Kick Voice -----------------------------------------------------
      float kick_out = 0.f;
      if (kick_amp_ > 0.0001f)
      {
        if (kick_attack_ < 1.f)
        {
          kick_attack_ += attack_inc;
          if (kick_attack_ > 1.f)
            kick_attack_ = 1.f;
        }
        kick_pitch_env_ *= kick_pitch_decay_coeff_;
        kick_amp_ -= kick_amp_dec_;
        if (kick_amp_ < 0.f)
          kick_amp_ = 0.f;

        const float kick_hz = kick_base_hz + kick_sweep_depth * kick_pitch_env_;
        kick_phase_ += kick_hz * k_sr_recip;
        kick_phase_ -= (float)(int32_t)kick_phase_;

        const float kick_sine = osc_sinf(kick_phase_);
        const float kick_click = (kick_pitch_env_ > 0.85f) ? (kick_pitch_env_ * 0.35f * kick_norm) : 0.f;
        const float kick_raw = (kick_sine + kick_click) * kick_amp_;
        kick_out = tanh(kick_raw * 1.5f) * kick_gain * kick_attack_;
      }

      // --- 2. Snare Voice ----------------------------------------------------
      float snare_out = 0.f;
      if (snare_body_amp_ > 0.0001f || snare_noise_amp_ > 0.0001f)
      {
        if (snare_attack_ < 1.f)
        {
          snare_attack_ += attack_inc;
          if (snare_attack_ > 1.f)
            snare_attack_ = 1.f;
        }
        snare_body_amp_ -= snare_body_dec_;
        if (snare_body_amp_ < 0.f)
          snare_body_amp_ = 0.f;
        snare_noise_amp_ -= snare_noise_dec_;
        if (snare_noise_amp_ < 0.f)
          snare_noise_amp_ = 0.f;
        snare_pitch_env_ *= snare_pitch_decay_coeff_;

        const float snare_hz = snare_base_hz + 50.f * snare_pitch_env_;
        snare_phase_ += snare_hz * k_sr_recip;
        snare_phase_ -= (float)(int32_t)snare_phase_;
        const float snare_body = osc_sinf(snare_phase_) * snare_body_amp_;

        const float white = (float)(int32_t)osc_rand() * 4.6566129e-10f;
        const float hp_in = white - snare_noise_hp_state_;
        snare_noise_hp_state_ += snare_hp_k * hp_in;
        const float snare_noise = hp_in * snare_noise_amp_;

        snare_out = (snare_body * snare_body_mix + snare_noise * snare_noise_mix) * snare_level * snare_attack_;
      }

      // --- 3. Hi-Hat Voice ---------------------------------------------------
      float hat_out = 0.f;
      if (hat_amp_ > 0.0001f)
      {
        if (hat_attack_ < 1.f)
        {
          hat_attack_ += attack_inc;
          if (hat_attack_ > 1.f)
            hat_attack_ = 1.f;
        }
        hat_amp_ -= hat_dec_;
        if (hat_amp_ < 0.f)
          hat_amp_ = 0.f;

        const float white = (float)(int32_t)osc_rand() * 4.6566129e-10f;
        const float hp = white - hat_bpf_s1_ * hat_q - hat_bpf_s2_;
        const float bp = hat_f * hp + hat_bpf_s1_;
        hat_bpf_s1_ = bp;
        const float lp = hat_f * bp + hat_bpf_s2_;
        hat_bpf_s2_ = lp;

        const float hat_filtered = bp * 0.85f + hp * 0.15f;
        hat_out = hat_filtered * hat_amp_ * hat_level * 0.14f * hat_attack_;
      }

      // --- Drum Sum & Master Tone Tilt ---------------------------------------
      float drum_sum = kick_out + snare_out + hat_out;

      tone_lp_state_ += tone_k * (drum_sum - tone_lp_state_);
      const float drum_hp = drum_sum - tone_lp_state_;
      drum_sum += tone_tilt * drum_hp;

      // --- DRIVE Saturation --------------------------------------------------
      const float driven = drum_sum * drive_gain;
      const float sat = fx_softclipf(0.25f, driven) * drive_comp;

      // --- Master Out (Dry + Gated Drum Signal) -------------------------------
      const float drum_final = sat * touch_gain_;
      out[0] = in[0] + drum_final;
      out[1] = in[1] + drum_final;
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
      if (euclid_mask_[step_])
      {
        const float decay_mult = 0.5f + 2.5f * (clipminmaxf(0.f, (float)params_.decay, 2000.f) / 2000.f);
        const float kick_norm = clip01f(params_.kick / 1023.f);
        const float snare_norm = clip01f(params_.snare / 1023.f);
        const bool hat_is_open = (params_.hihat >= 512);

        if (kick_hit_[step_])
          triggerKick(decay_mult, kick_norm);
        if (snare_hit_[step_])
          triggerSnare(decay_mult, snare_norm);
        if (hihat_hit_[step_])
          triggerHihat(decay_mult, hat_is_open);
      }
      break;

    case k_unit_touch_phase_moved:
      break;

    case k_unit_touch_phase_ended:
    case k_unit_touch_phase_cancelled:
      gate_ = false;
      break;

    default:
      break;
    }
  }

private:
  static inline float k_sr_recip_16th(float bpm)
  {
    return (60.f / bpm) * k_sr * 0.25f;
  }

  void triggerKick(float decay_mult, float kick_norm)
  {
    kick_amp_ = 1.0f;
    kick_pitch_env_ = 1.0f;
    kick_phase_ = 0.0f;
    kick_attack_ = 0.0f;

    // Amplitude falls linearly to silence over the note length, so DECAY maps
    // directly to an audible, defined note duration (not a quiet exponential
    // dust tail).
    const float kick_note_len = (0.060f + 0.340f * kick_norm) * decay_mult;
    kick_amp_dec_ = (1.0f - 0.0001f) / (kick_note_len * k_sr);

    const float pitch_dec_time = (0.015f + 0.035f * kick_norm) * decay_mult;
    kick_pitch_decay_coeff_ = fasterexpf(-k_sr_recip / pitch_dec_time);
  }

  void triggerSnare(float decay_mult, float snare_norm)
  {
    snare_body_amp_ = 1.0f;
    snare_noise_amp_ = 1.0f;
    snare_pitch_env_ = 1.0f;
    snare_phase_ = 0.0f;
    snare_attack_ = 0.0f;

    const float body_note_len = (0.020f + 0.045f * snare_norm) * decay_mult;
    snare_body_dec_ = (1.0f - 0.0001f) / (body_note_len * k_sr);

    const float noise_note_len = (0.015f + 0.050f * snare_norm) * decay_mult;
    snare_noise_dec_ = (1.0f - 0.0001f) / (noise_note_len * k_sr);

    snare_pitch_decay_coeff_ = fasterexpf(-k_sr_recip / 0.008f);
  }

  void triggerHihat(float decay_mult, bool is_open)
  {
    hat_amp_ = 1.0f;
    hat_attack_ = 0.0f;

    const float hat_floor = is_open ? 0.180f : 0.040f;
    const float hat_note_len = hat_floor * decay_mult;
    hat_dec_ = (1.0f - 0.0001f) / (hat_note_len * k_sr);
  }

  void regenPattern()
  {
    pattern_dirty_ = false;
    uint32_t state = 0x7F4A7C15u + (uint32_t)(params_.pattern + 1) * 0x9E3779B9u;

    for (uint8_t i = 0; i < k_num_steps; ++i)
    {
      uint32_t r = lcg_next(state);

      // Kick:
      if (i == 0)
      {
        kick_hit_[i] = 1; // Beat 1 always hits
      }
      else if (i == 4 || i == 8 || i == 12)
      {
        // Quarter note beats 2, 3, 4: ~85% probability
        kick_hit_[i] = ((r & 0xFFu) < 218u) ? 1 : 0;
      }
      else if (i == 2 || i == 6 || i == 10 || i == 14)
      {
        // 8th note syncopations: ~25% probability
        kick_hit_[i] = ((r & 0xFFu) < 64u) ? 1 : 0;
      }
      else
      {
        // 16th note fills: ~12% probability
        kick_hit_[i] = ((r & 0xFFu) < 30u) ? 1 : 0;
      }

      // Snare:
      r = lcg_next(state);
      if (i == 4 || i == 12)
      {
        // Beats 2 & 4: ~88% probability
        snare_hit_[i] = ((r & 0xFFu) < 225u) ? 1 : 0;
      }
      else if (i == 7 || i == 11 || i == 14 || i == 15)
      {
        // Ghost hits / end-of-bar fills: ~20% probability
        snare_hit_[i] = ((r & 0xFFu) < 51u) ? 1 : 0;
      }
      else
      {
        snare_hit_[i] = 0;
      }

      // Hi-hat:
      r = lcg_next(state);
      if (i == 2 || i == 6 || i == 10 || i == 14)
      {
        // Off-beat 8ths (driving open/closed hat): ~95% probability
        hihat_hit_[i] = ((r & 0xFFu) < 242u) ? 1 : 0;
      }
      else if (i == 0 || i == 4 || i == 8 || i == 12)
      {
        // On-beats: ~65% probability
        hihat_hit_[i] = ((r & 0xFFu) < 166u) ? 1 : 0;
      }
      else
      {
        // 16th note offbeats: ~45% probability
        hihat_hit_[i] = ((r & 0xFFu) < 115u) ? 1 : 0;
      }
    }

    kick_hit_[0] = 1;
#ifdef AUTODRIFT
    drift_state_ = 0x9E3779B9u ^ (uint32_t)(params_.pattern + 1) * 0x85EBCA6Bu;
#endif
  }

  void regenEuclid()
  {
    density_dirty_ = false;
    uint8_t k = (uint8_t)clipminmaxi32(1, params_.density, k_num_steps);
    euclid(k, k_num_steps, euclid_mask_);

    // Guarantee that every active Euclidean step sounds at least one voice
    for (uint8_t i = 0; i < k_num_steps; ++i)
    {
      if (euclid_mask_[i] && !kick_hit_[i] && !snare_hit_[i] && !hihat_hit_[i])
      {
        hihat_hit_[i] = 1;
      }
    }
  }

#ifdef AUTODRIFT
  // Evolving pattern: once per bar, softly mutate a few hits so the groove
  // drifts over time. The downbeat kick and the "every active step sounds"
  // guarantee are re-applied to keep the rhythm solid.
  void driftPattern()
  {
    drift_state_ = drift_state_ * 1664525u + 1013904223u;
    const uint32_t r0 = drift_state_ >> 16;
    const uint8_t mutations = 1u + (uint8_t)(r0 & 3u);

    for (uint8_t m = 0; m < mutations; ++m)
    {
      drift_state_ = drift_state_ * 1664525u + 1013904223u;
      const uint32_t r1 = drift_state_ >> 16;
      const uint8_t i = 1u + (uint8_t)(r1 & 0x0Fu); // skip the downbeat

      drift_state_ = drift_state_ * 1664525u + 1013904223u;
      const uint32_t r2 = drift_state_ >> 16;

      switch (r2 & 3u)
      {
      case 0:
        kick_hit_[i] ^= 1;
        break;
      case 1:
        snare_hit_[i] ^= 1;
        break;
      case 2:
        hihat_hit_[i] ^= 1;
        break;
      default:
        // Keep a voice firing on active Euclidean steps; drift hats elsewhere.
        if (euclid_mask_[i])
          hihat_hit_[i] = 1;
        else
          hihat_hit_[i] ^= 1;
        break;
      }
    }

    kick_hit_[0] = 1;

    for (uint8_t i = 0; i < k_num_steps; ++i)
    {
      if (euclid_mask_[i] && !kick_hit_[i] && !snare_hit_[i] && !hihat_hit_[i])
        hihat_hit_[i] = 1;
    }
  }
#endif

  Params params_;

  // Generative patterns
  uint8_t kick_hit_[k_num_steps];
  uint8_t snare_hit_[k_num_steps];
  uint8_t hihat_hit_[k_num_steps];
  uint8_t euclid_mask_[k_num_steps];
  bool pattern_dirty_;
  bool density_dirty_;
#ifdef AUTODRIFT
  uint32_t drift_state_; // evolving-pattern mutation RNG
#endif

  // Sequencer state
  uint8_t step_;
  float clock_accum_;
  float samples_per_step_;
  bool gate_;
  float touch_gain_;

  // Host 4PPQN sync: the host fires one tick per 16th note on its global
  // transport clock. When ticks have been received we re-anchor step_ onto
  // that grid so the sequencer never drifts from the global sync time.
  uint32_t host_counter_;
  bool host_sync_valid_;

  // Kick voice state
  float kick_phase_;
  float kick_amp_;
  float kick_pitch_env_;
  float kick_attack_;
  float kick_amp_dec_; // per-sample amplitude decrement (linear decay)
  float kick_pitch_decay_coeff_;

  // Snare voice state
  float snare_phase_;
  float snare_body_amp_;
  float snare_noise_amp_;
  float snare_pitch_env_;
  float snare_attack_;
  float snare_noise_hp_state_;
  float snare_body_dec_;   // per-sample amplitude decrement
  float snare_noise_dec_;  // per-sample amplitude decrement
  float snare_pitch_decay_coeff_;

  // Hi-hat voice state
  float hat_amp_;
  float hat_attack_;
  float hat_bpf_s1_;
  float hat_bpf_s2_;
  float hat_dec_; // per-sample amplitude decrement (linear decay)

  // Master bus filter state
  float tone_lp_state_;
};
