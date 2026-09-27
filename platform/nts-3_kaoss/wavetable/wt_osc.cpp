/*
 *  File: wt_osc.cpp
 *
 *  NTS-3 kaoss pad kit "WT" wavetable oscillator DSP
 *
 */
#include "wt_osc.h"
#include "fx_api.h"
#include "osc_api.h"

void WavetableOsc::init(float *allocated_buffer)
{
  buffer_ = allocated_buffer;
  params_.reset();
  reset();
  setTempo(120.f);
}

void WavetableOsc::reset()
{
  phase_a_ = phase_b_ = phase_sub_ = 0.f;
  lfo_phase_ = 0.f;
  note_target_ = params_.note;
  note_now_ = params_.note;
  amp_ = 0.f;
  env_phase_ = 0.f;
  env_state_ = ENV_IDLE;
  step_accent_ = 1.f;
  gate_ = false;
  step_ = 0;
  clock_accum_ = 0.f;
  host_counter_ = 0;
  host_sync_valid_ = false;
  pattern_dirty_ = true;
  for (uint8_t i = 0; i < k_num_steps; ++i)
  {
    pitch_[i] = 0;
    hit_[i] = 1;
    slide_[i] = 0;
    accent_[i] = 0;
  }
}

const int8_t *WavetableOsc::scale()
{
  // Minor pentatonic: the acid unit's scale, kept so both units agree on what
  // a generated line does.
  static const int8_t k_scale[8] = {0, 2, 3, 5, 7, 8, 10, 12};
  return k_scale;
}

void WavetableOsc::regenPattern()
{
  pattern_dirty_ = false;
  const int8_t *sc = scale();
  uint32_t state = 0x7F4A7C15u + (uint32_t)(params_.pattern + 1) * 0x9E3779B9u;

  for (uint8_t i = 0; i < k_num_steps; ++i)
  {
    const uint32_t r = lcgNext(state);
    uint8_t choice;
    switch (r & 3u)
    {
    case 0:
    case 1:
      choice = 0; // root, most common
      break;
    case 2:
      choice = 4; // fifth
      break;
    default:
      choice = (uint8_t)(1u + (r >> 8) % 7u);
      break;
    }
    pitch_[i] = sc[choice];

    accent_[i] = (lcgNext(state) & 0xFFu) < 112u ? 1 : 0; // ~44 % accents
    slide_[i] = (lcgNext(state) & 0xFFu) < 64u ? 1 : 0;   // ~25 % slides
  }

  // No DENSITY parameter: the seed picks its own hit density (5..12 of 16),
  // so turning PATTERN changes the rhythm as well as the notes.
  euclid((uint8_t)(5u + (state >> 24) % 8u), k_num_steps, hit_);

  // A solid downbeat: step 0 is always the root, accented, no slide.
  pitch_[0] = 0;
  accent_[0] = 1;
  slide_[0] = 0;
  hit_[0] = 1;
}

void WavetableOsc::setParameter(uint8_t index, int32_t value)
{
  switch (index)
  {
  case PARAM_PITCH:
    params_.note = value * 127.f / 1023.f; // 0..127
    if (params_.pattern == 0)
      note_target_ = params_.note; // sequencer off: PITCH is the played note
    break;

  case PARAM_POSITION:
    params_.position = param_10bit_to_f32(value);
    break;

  case PARAM_LFO_RATE:
    params_.lfo_rate = Params::rate_from_param(value);
    break;

  case PARAM_LFO_DEPTH:
    params_.lfo_depth = param_10bit_to_f32(value);
    break;

  case PARAM_ADSR:
    params_.adsr = (uint16_t)value;
    break;

  case PARAM_PATTERN:
    if (params_.pattern != value)
    {
      params_.pattern = value;
      pattern_dirty_ = true;
      if (value == 0)
      {
        // Sequencer off: PITCH becomes the played note again.
        note_target_ = params_.note;
      }
      else if (gate_ && !droneMode())
      {
        regenPattern(); // rebuild first, so this step is from the new seed
        step_ = 0;
        clock_accum_ = 0.f;
        triggerStep(); // hear the new pattern straight away
      }
    }
    break;

  case PARAM_SUB:
    params_.sub = param_10bit_to_f32(value);
    break;

  case PARAM_DETUNE:
    params_.detune = (float)value; // cents
    break;

  default:
    break;
  }
}

const char *WavetableOsc::getParameterStrValue(uint8_t index, int32_t value) const
{
  if (index == PARAM_ADSR)
  {
    // The morph is continuous, so name the zone it is crossing.
    static const char *kNames[4] = {"Pluck", "Pad", "Perc", "Swell"};
    if (value < 0 || value > 1023)
      return nullptr;
    if (value >= (int32_t)k_drone_threshold)
      return "Drone";
    const uint8_t zone = (uint8_t)((float)value / k_adsr_zone);
    return kNames[zone < 4 ? zone : 3];
  }
  if (index == PARAM_PATTERN)
    return value == 0 ? "Off" : nullptr;
  return nullptr;
}

void WavetableOsc::touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{
  (void)id;
  (void)x;
  (void)y;

  if (droneMode())
    return; // a drone ignores the pad: it is already sounding

  switch (phase)
  {
  case k_unit_touch_phase_began:
    gate_ = true;
    if (params_.pattern != 0)
    {
      if (pattern_dirty_)
        regenPattern();
      triggerStep(); // play the step under the finger now, not at the next tick
    }
    else
    {
      triggerRoot();
    }
    break;

  case k_unit_touch_phase_moved:
    break;

  case k_unit_touch_phase_ended:
  case k_unit_touch_phase_cancelled:
    gate_ = false; // envelope releases
    break;

  default:
    break;
  }
}

void WavetableOsc::setTempo(float bpm)
{
  if (bpm < 1.f)
    bpm = 1.f;
  samples_per_step_ = (60.f / bpm) * k_sr * 0.25f; // one 16th note
}

void WavetableOsc::tempo4ppqnTick(uint32_t counter)
{
  host_counter_ = counter;
  host_sync_valid_ = true;
}

void WavetableOsc::triggerStep()
{
  const uint8_t s = step_;
  if (!hit_[s])
    return;

  const float n = params_.note + (float)pitch_[s];
  note_target_ = n;

  if (slide_[s] && env_state_ != ENV_IDLE)
  {
    // Legato slide: glide to the note without re-attacking.
  }
  else
  {
    // Retrigger, but resume the attack from the current level so a fast
    // sequence never clicks.
    note_now_ = n;
    if (env_state_ == ENV_IDLE)
      amp_ = 0.f;
    env_state_ = ENV_ATTACK;
  }
  step_accent_ = accent_[s] ? 1.f : 0.55f;
}

void WavetableOsc::triggerRoot()
{
  note_target_ = params_.note;
  note_now_ = params_.note;
  if (env_state_ == ENV_IDLE)
    amp_ = 0.f;
  env_state_ = ENV_ATTACK;
  step_accent_ = 1.f;
}

void WavetableOsc::process(const float *__restrict in, float *__restrict out, uint32_t frames)
{
  (void)in; // pure generator: the pad input is not used

  const Params p = params_;
  const bool drone = droneMode();
  const bool seq_on = p.pattern != 0;

  if (seq_on && pattern_dirty_)
    regenPattern();

  // ---- Envelope ------------------------------------------------------------
  const AdsrPreset env = currentAdsr();
  const float atk_inc = env.attack > 0.f ? k_sr_recip / env.attack : 1.f;
  const float dec_inc = env.decay > 0.f ? k_sr_recip / env.decay : 1.f;
  const float rel_inc = env.release > 0.f ? k_sr_recip / env.release : 1.f;
  const float sus_lvl = drone ? 1.f : env.sustain;

  // ---- Pitch smoothing (8 ms), so encoder and step jumps do not click ------
  const float kg = 1.f - fasterexpf(-k_sr_recip / 0.008f);

  // ---- LFO -----------------------------------------------------------------
  const float lfo_inc = p.lfo_rate * k_sr_recip;

  // ---- Unison --------------------------------------------------------------
  const float w = 0.25f * clip01f(si_fabsf(p.detune) * 0.02f);
  const float al = fastercosfullf((1.f - w) * 0.25f * M_PI);
  const float ar = fastersinfullf((1.f - w) * 0.25f * M_PI);
  const float bl = fastercosfullf((1.f + w) * 0.25f * M_PI);
  const float br = fastersinfullf((1.f + w) * 0.25f * M_PI);

  const float oct = p.detune * (0.5f / 1200.f);
  const float ratio = oct >= 0.f ? fx_pow2f(oct) : 1.f / fx_pow2f(-oct);
  const float inc_a = ratio;
  const float inc_b = 1.f / ratio;
  const float sub_gain = p.sub * 0.5f;

  for (uint32_t i = 0; i < frames; ++i, out += 2)
  {
    // --- Drone: always sounding, no envelope, no sequencer -------------------
    if (drone)
    {
      amp_ = 1.f;
    }
    else
    {
      // --- Sequencer clock, 16th notes --------------------------------------
      if (seq_on && gate_)
      {
        clock_accum_ += 1.f;

        if (host_sync_valid_)
        {
          // The host transport owns the step grid while it is sending 4PPQN
          // ticks, so follow its counter and run no second clock. Advancing
          // on both would trigger every step twice, a few samples apart, and
          // the second trigger would cut the note short.
          const uint8_t host_step = (uint8_t)(host_counter_ & k_step_mask);
          if (host_step != step_)
          {
            step_ = host_step;
            clock_accum_ = 0.f;
            triggerStep();
          }
        }
        else if (clock_accum_ >= samples_per_step_)
        {
          clock_accum_ -= samples_per_step_;
          step_ = (uint8_t)((step_ + 1) & k_step_mask);
          triggerStep();
        }
      }

      // --- Envelope state machine --------------------------------------------
      if (!gate_ && env_state_ != ENV_IDLE && env_state_ != ENV_RELEASE)
      {
        env_state_ = ENV_RELEASE;
        env_phase_ = amp_; // linear release starts from the held level
      }

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
        amp_ -= dec_inc * (1.f - sus_lvl);
        if (amp_ <= sus_lvl)
        {
          amp_ = sus_lvl;
          env_state_ = sus_lvl > 0.f ? ENV_SUSTAIN : ENV_IDLE;
        }
        break;
      case ENV_SUSTAIN:
        amp_ = sus_lvl; // held until the pad is released
        break;
      case ENV_RELEASE:
        amp_ -= rel_inc * env_phase_; // linear, like the arpeggiator unit
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
    }

    // --- Pitch ---------------------------------------------------------------
    note_now_ += kg * (note_target_ - note_now_);
    const float note_f = clipminmaxf(0.f, note_now_, 150.f);
    const uint8_t n0 = (uint8_t)note_f;
    const float freq = linintf(note_f - (float)n0, osc_notehzf(n0), osc_notehzf(n0 + 1));

    // --- LFO -> POSITION ------------------------------------------------------
    lfo_phase_ += lfo_inc;
    lfo_phase_ -= (float)(uint32_t)lfo_phase_;
    const float pos = clip01f(p.position + osc_sinf(lfo_phase_) * p.lfo_depth);

    // --- Band-limited mip selection -------------------------------------------
    // l_safe is the darkest level that still reads with enough points per
    // sample; POSITION crossfades from there up to level 0, the untouched
    // cycle, so the bright end of the sweep sounds the same at every pitch.
    const float l_safe = clipminmaxf(0.f, fastlog2f(freq * k_wt_lsafe_gain), k_lmax);
    const float li = l_safe * (1.f - pos);
    const uint8_t l0 = (uint8_t)li;
    const uint8_t l1 = l0 >= (uint8_t)k_lmax ? l0 : (uint8_t)(l0 + 1);
    const float lfrac = l0 == l1 ? 0.f : li - (float)l0;

    const int npts0 = (int)wt_level_offset[l0 + 1] - (int)wt_level_offset[l0];
    const int npts1 = (int)wt_level_offset[l1 + 1] - (int)wt_level_offset[l1];
    const int16_t *tbl0 = &wt_table[wt_level_offset[l0]];
    const int16_t *tbl1 = &wt_table[wt_level_offset[l1]];

    // --- Oscillators -----------------------------------------------------------
    float a, b;
    if (freq > k_wt_sine_crossover)
    {
      // Past the smallest mip level the bake rate is out of harmonics: keep
      // the pitch with the firmware's band-limited sine instead of folding.
      a = osc_sinf(phase_a_);
      b = osc_sinf(phase_b_);
    }
    else
    {
      a = linintf(lfrac, tap(tbl0, npts0, phase_a_ * (float)npts0), tap(tbl1, npts1, phase_a_ * (float)npts1));
      b = linintf(lfrac, tap(tbl0, npts0, phase_b_ * (float)npts0), tap(tbl1, npts1, phase_b_ * (float)npts1));
    }

    phase_a_ += freq * k_sr_recip * inc_a;
    phase_a_ -= (float)(uint32_t)phase_a_;
    phase_b_ += freq * k_sr_recip * inc_b;
    phase_b_ -= (float)(uint32_t)phase_b_;

    // Sub oscillator one octave down, band-limited square.
    phase_sub_ += freq * 0.5f * k_sr_recip;
    phase_sub_ -= (float)(uint32_t)phase_sub_;
    const float sq = osc_bl2_sqrf(phase_sub_, clipminmaxf(0.f, note_f - 12.f, 115.f) * (6.f / 127.f));

    // --- Sum, VCA, soft clip ---------------------------------------------------
    const float lvl = amp_ * step_accent_ * 0.5f;
    const float sig_l = (a * al + b * bl + sub_gain * sq) * lvl;
    const float sig_r = (a * ar + b * br + sub_gain * sq) * lvl;

    out[0] = fx_softclipf(0.15f, sig_l);
    out[1] = fx_softclipf(0.15f, sig_r);
  }
}
