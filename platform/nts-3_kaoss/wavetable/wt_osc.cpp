/*
 *  File: wt_osc.cpp
 *
 *  NTS-3 kaoss pad kit "WT" wavetable oscillator DSP
 *
 */
#include "wt_osc.h"
#include "fx_api.h"
#include "osc_api.h"
#include "utils/float_math.h"

void WavetableOsc::init(float *allocated_buffer)
{
  buffer_ = allocated_buffer;
  params_.reset();
  reset();
}

void WavetableOsc::reset()
{
  phase_a_ = phase_b_ = phase_sub_ = 0.f;
  note_target_ = params_.note;
  note_now_ = params_.note;
  amp_ = 0.f;
  env_state_ = ENV_IDLE;
  gate_ = false;
  s1l_ = s2l_ = s3l_ = s4l_ = 0.f;
  s1r_ = s2r_ = s3r_ = s4r_ = 0.f;
}

void WavetableOsc::setParameter(uint8_t index, int32_t value)
{
  switch (index)
  {
  case PARAM_PITCH:
    params_.note = value * 127.f / 1023.f; // 0..127
    note_target_ = params_.note;
    break;

  case PARAM_MORPH:
    params_.morph = param_10bit_to_f32(value);
    break;

  case PARAM_DETUNE:
    params_.detune = (float)value; // cents
    break;

  case PARAM_SUB:
    params_.sub = param_10bit_to_f32(value);
    break;

  case PARAM_CUTOFF:
    params_.cutoff = param_10bit_to_f32(value);
    break;

  case PARAM_RESON:
    params_.reson = param_10bit_to_f32(value);
    break;

  case PARAM_DECAY:
    params_.decay = value * 0.001f; // 0..4000 ms
    break;

  case PARAM_MIX:
    params_.mix = value * (1.f / 1024.f); // -1..1
    break;

  default:
    break;
  }
}

void WavetableOsc::touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{
  (void)id;
  (void)x;
  (void)y;

  switch (phase)
  {
  case k_unit_touch_phase_began:
    gate_ = true;
    if (env_state_ == ENV_IDLE)
      amp_ = 0.f; // start from silence, otherwise ramp from the current level
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

void WavetableOsc::process(const float *__restrict in, float *__restrict out, uint32_t frames)
{
  const Params p = params_;

  // Base cutoff, exponential scale 25 Hz .. ~12.5 kHz.
  const float kfb = 25.f * fasterexpf(6.215f * clip01f(p.cutoff));
  const float fb = 3.2f * clip01f(p.reson);

  // The envelope opens the filter by a factor rather than by a fixed number of
  // Hz, so CUTOFF keeps its whole range: a low cutoff still sounds dark at the
  // top of the attack, and a high one is only nudged down again on release.
  // Higher resonance opens further, so the Y axis squelches as well as colours.
  const float env_open = 1.f + 6.f * (0.35f + 0.65f * clip01f(p.reson));

  // Envelope rates: 2 ms attack, DECAY fall, 30 ms release.
  const float atk_inc = k_sr_recip / 0.002f;
  const float dec_inc = k_sr_recip / clipminf(0.001f, p.decay);
  const float rel_inc = k_sr_recip / 0.030f;

  // Pitch smoothing, so encoder steps do not click (8 ms time constant).
  const float kg = 1.f - fasterexpf(-k_sr_recip / 0.008f);

  // Unison spread: 0 = both oscillators centred, 50 cents and up = hard panned.
  const float w = 0.25f * clip01f(si_fabsf(p.detune) * 0.02f);
  const float al = fastercosfullf((1.f - w) * 0.25f * M_PI);
  const float ar = fastersinfullf((1.f - w) * 0.25f * M_PI);
  const float bl = fastercosfullf((1.f + w) * 0.25f * M_PI);
  const float br = fastersinfullf((1.f + w) * 0.25f * M_PI);

  // Detune ratio of the two oscillators, +-DETUNE/2 cents.
  const float oct = p.detune * (0.5f / 1200.f);
  const float ratio = oct >= 0.f ? fx_pow2f(oct) : 1.f / fx_pow2f(-oct);
  const float inc_a = ratio;
  const float inc_b = 1.f / ratio;
  const float sub_gain = p.sub * 0.5f;

  // Constant power dry/wet: MIX < 0 favours the input, MIX > 0 the oscillator.
  const float m = clipminmaxf(-1.f, p.mix, 1.f);
  const float gw = sqrtf(0.5f * (1.f + m));
  const float gd = sqrtf(0.5f * (1.f - m));

  for (uint32_t i = 0; i < frames; ++i, in += 2, out += 2)
  {
    // --- Envelope state machine ---------------------------------------------
    if (!gate_ && env_state_ != ENV_IDLE)
      env_state_ = ENV_RELEASE;

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

    // --- Pitch ---------------------------------------------------------------
    note_now_ += kg * (note_target_ - note_now_);
    const float note_f = clipminmaxf(0.f, note_now_, 150.f);
    const uint8_t n0 = (uint8_t)note_f;
    const float freq = linintf(note_f - (float)n0, osc_notehzf(n0), osc_notehzf(n0 + 1));

    // --- Band-limited mip selection -----------------------------------------
    // l_safe is the darkest level that still reads with enough points per
    // sample; MORPH crossfades from there up to level 0, the untouched cycle,
    // so the bright end of the sweep sounds the same at every pitch.  Both
    // levels of the crossfade are band limited, so the sweep cannot alias.
    const float l_safe = clipminmaxf(0.f, fastlog2f(freq * k_wt_lsafe_gain), k_lmax);
    const float li = l_safe * (1.f - p.morph);
    const uint8_t l0 = (uint8_t)li;
    const uint8_t l1 = l0 >= (uint8_t)k_lmax ? l0 : (uint8_t)(l0 + 1);
    const float lfrac = l0 == l1 ? 0.f : li - (float)l0;

    const int npts0 = (int)wt_level_offset[l0 + 1] - (int)wt_level_offset[l0];
    const int npts1 = (int)wt_level_offset[l1 + 1] - (int)wt_level_offset[l1];
    const int16_t *tbl0 = &wt_table[wt_level_offset[l0]];
    const int16_t *tbl1 = &wt_table[wt_level_offset[l1]];

    // --- Oscillators ---------------------------------------------------------
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

    // --- Stereo mix ----------------------------------------------------------
    float sig_l = a * al + b * bl + sub_gain * sq;
    float sig_r = a * ar + b * br + sub_gain * sq;

    // --- 4-stage Moog ladder low-pass, one per channel -----------------------
    const float fc = clipminmaxf(100.f, kfb * env_open * amp_, 18000.f);
    const float k = 1.f - fasterexpf(-M_TWOPI * fc * k_sr_recip);

    const float ul = tanh(sig_l);
    s1l_ += k * (tanh(ul - fb * s4l_) - s1l_);
    s2l_ += k * (tanh(s1l_) - s2l_);
    s3l_ += k * (tanh(s2l_) - s3l_);
    s4l_ += k * (tanh(s3l_) - s4l_);

    const float ur = tanh(sig_r);
    s1r_ += k * (tanh(ur - fb * s4r_) - s1r_);
    s2r_ += k * (tanh(s1r_) - s2r_);
    s3r_ += k * (tanh(s2r_) - s3r_);
    s4r_ += k * (tanh(s3r_) - s4r_);

    // --- VCA, soft clip, dry/wet ---------------------------------------------
    const float wl = fx_softclipf(0.15f, s4l_ * amp_ * 0.5f);
    const float wr = fx_softclipf(0.15f, s4r_ * amp_ * 0.5f);

    out[0] = gd * in[0] + gw * wl;
    out[1] = gd * in[1] + gw * wr;
  }
}
