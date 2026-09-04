#pragma once
/*
 *  File: effect.h
 *
 *  EvolvingArp - continuous LFO-driven drone for NTS-3 kaoss pad kit
 *
 *  Design:
 *    There are NO discrete arpeggiator steps. Instead, a single free-running
 *    LFO phasor drives three dimensions simultaneously:
 *
 *    - PATTERN (range): LFO [0..1] maps to a float position across chord notes.
 *      Frequency is interpolated smoothly between adjacent chord tones.
 *      Result: the drone slowly slides between notes in the chord.
 *
 *    - MODE (depth): A cosine-phase LFO (90-deg offset from main) adds a
 *      continuous pitch drift of 0..+/-12 semitones. MODE sets the depth.
 *      Result: slow vibrato/wobble layered on top of the chord sweep.
 *
 *    - WAVE (range): The same unipolar LFO sweeps the oscillator wave morph
 *      from 0 up to the WAVE value (0=fixed sine, 1023=full Sine->Saw sweep).
 *      Result: the timbre evolves in sync with the pitch motion.
 *
 *    - LFO param: master speed for all three LFOs above (0=off, 1023=~8Hz).
 *
 *    Envelope: simple ASR (no gate, no re-trigger).
 *      Touch down  -> attack (40 ms)
 *      Held        -> sustain at full level
 *      Touch up    -> release (300 ms)
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "osc_api.h"
#include "unit_genericfx.h"
#include "utils/float_math.h"

class EvolvingArp {
public:
  // Parameter indices (must match header.c order)
  enum { ROOT = 0U, CHORD, PATTERN, MODE, WAVE, LEVEL, LFO_RATE, NUM_PARAMS };

  // ---------------------------------------------------------------------------
  // Chord offsets table (semitones from root)
  // ---------------------------------------------------------------------------
  static const int8_t kChordOffsets[9][4];
  static const int8_t kChordSizes[9];

  // ---------------------------------------------------------------------------
  // Parameter storage
  // ---------------------------------------------------------------------------
  struct Params {
    uint32_t root{60};       // MIDI note (0-127, derived from 0-1023 X value)
    uint8_t  chord{0};       // Chord index 0-8
    uint16_t pattern{512};   // LFO chord-sweep range 0-1023
    uint16_t mode{256};      // LFO pitch-drift depth 0-1023
    uint16_t wave{512};      // LFO wave-morph range 0-1023
    float    level{0.5f};    // Output level 0-1
    uint16_t lfo_rate{200};  // Master LFO speed 0-1023

    void reset() {
      root     = 60;
      chord    = 0;
      pattern  = 512;
      mode     = 256;
      wave     = 512;
      level    = 0.5f;
      lfo_rate = 200;
    }
  };

  // ---------------------------------------------------------------------------
  EvolvingArp() {}
  ~EvolvingArp() {}

  // ---------------------------------------------------------------------------
  inline int8_t Init(const unit_runtime_desc_t *desc) {
    if (!desc)
      return k_unit_err_undef;
    if (desc->target != (UNIT_TARGET_PLATFORM | k_unit_module_genericfx))
      return k_unit_err_target;
    if (!UNIT_API_IS_COMPAT(desc->api))
      return k_unit_err_api_version;
    if (desc->samplerate != 48000)
      return k_unit_err_samplerate;

    samplerate_ = desc->samplerate;
    params_.reset();
    resetState();
    updateActiveNotes();

    return k_unit_err_none;
  }

  inline void Teardown() {}
  inline void Reset()   { resetState(); }
  inline void Resume()  { resetState(); }
  inline void Suspend() { is_touched_ = false; amp_ = 0.f; env_state_ = ENV_IDLE; }

  // setTempo kept for SDK callback compatibility - not used in LFO-only design
  inline void setTempo(uint32_t tempo) { (void)tempo; }
  inline void tempoTick(uint32_t counter) { (void)counter; }

  // ---------------------------------------------------------------------------
  // Touch events
  // ---------------------------------------------------------------------------
  inline void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) {
    (void)id; (void)x; (void)y;
    if (phase == k_unit_touch_phase_began) {
      is_touched_ = true;
      env_state_  = ENV_ATTACK;
    } else if (phase == k_unit_touch_phase_ended ||
               phase == k_unit_touch_phase_cancelled) {
      is_touched_ = false;
      if (env_state_ != ENV_IDLE) {
        env_phase_  = amp_;
        env_state_  = ENV_RELEASE;
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Audio render
  // ---------------------------------------------------------------------------
  fast_inline void Process(const float *in, float *out, size_t frames) {
    const float *__restrict in_p  = in;
    float       *__restrict out_p = out;
    const float *out_e = out_p + (frames << 1);

    const float sr_inv = 1.f / (float)samplerate_;

    // Fixed ASR envelope times
    const float atk_inc = sr_inv / 0.040f;  // 40 ms attack
    const float rel_inc = sr_inv / 0.300f;  // 300 ms release

    // Master LFO increment per sample
    const float lfo_hz  = lfoRateHz();
    const float lfo_inc = lfo_hz * sr_inv;

    // Pre-normalise params once per block
    const float pattern_norm = (float)params_.pattern * (1.f / 1023.f); // 0..1
    const float mode_norm    = (float)params_.mode    * (1.f / 1023.f); // 0..1
    const float wave_range   = (float)params_.wave;                      // 0..1023

    for (; out_p != out_e; in_p += 2, out_p += 2) {

      // -----------------------------------------------------------------------
      // LFO advance
      // -----------------------------------------------------------------------
      lfo_phase_ += lfo_inc;
      if (lfo_phase_ >= 1.f) lfo_phase_ -= 1.f;

      // Main LFO: unipolar sine [0..1]
      const float lfo_uni  = osc_sinf(lfo_phase_) * 0.5f + 0.5f;
      // Quadrature LFO (90 deg offset): bipolar sine [-1..+1] for pitch drift
      // lfo_phase_ + 0.25 wraps naturally via osc_sinf's internal phase handling
      const float lfo_quad = osc_sinf(lfo_phase_ + 0.25f);

      // -----------------------------------------------------------------------
      // PATTERN: LFO sweeps a float position across active chord notes
      // -----------------------------------------------------------------------
      float target_hz = base_hz_; // fallback: root note
      if (active_notes_count_ > 1) {
        // Sweep from note[0] to note[n-1] * pattern_norm
        // At pattern_norm=0 → always note[0]
        // At pattern_norm=1 → LFO sweeps from note[0] to note[n-1]
        const float max_pos  = (float)(active_notes_count_ - 1) * pattern_norm;
        const float note_pos = lfo_uni * max_pos;                 // [0 .. max_pos]
        const int   lo_idx   = (int)note_pos;
        const int   hi_idx   = lo_idx + 1;
        const float frac     = note_pos - (float)lo_idx;

        const float hz_lo = noteHz_[lo_idx];
        const float hz_hi = (hi_idx < active_notes_count_) ? noteHz_[hi_idx] : noteHz_[lo_idx];
        target_hz = hz_lo + frac * (hz_hi - hz_lo);
      }

      // -----------------------------------------------------------------------
      // MODE: quadrature LFO adds pitch drift (0 to +-12 semitones)
      // 2^(x/12) approximation: ratio = exp(x * ln2/12) ≈ 1 + x*0.05776 for |x|<1
      // For up to 12 semitones we use the accurate expf path.
      // -----------------------------------------------------------------------
      const float drift_semitones = lfo_quad * 12.f * mode_norm;
      const float drift_ratio     = expf(drift_semitones * 0.057762f); // ln(2)/12

      const float modulated_hz = target_hz * drift_ratio;

      // -----------------------------------------------------------------------
      // Smooth frequency (slow glide coefficient for drone slides)
      // At 48kHz, coeff 0.0008 gives ~60 ms half-glide time → smooth chord slide
      // -----------------------------------------------------------------------
      smooth_hz_ += (modulated_hz - smooth_hz_) * 0.0008f;
      const float w0 = smooth_hz_ * sr_inv;

      // -----------------------------------------------------------------------
      // WAVE: LFO sweeps oscillator morph from 0 to wave_range
      // -----------------------------------------------------------------------
      const float w = lfo_uni * wave_range; // 0..wave_range

      const float sine_sig   = osc_sinf(phase_);
      const float tri_sig    = (phase_ < 0.5f) ? (-1.0f + 4.0f * phase_) : (3.0f - 4.0f * phase_);
      const float square_sig = osc_sqrf(phase_);
      const float saw_sig    = osc_sawf(phase_);

      float sig = 0.f;
      if (w < 256.f) {
        sig = sine_sig + (w * (1.f / 256.f)) * (tri_sig - sine_sig);
      } else if (w < 512.f) {
        sig = tri_sig  + ((w - 256.f) * (1.f / 256.f)) * (square_sig - tri_sig);
      } else if (w < 768.f) {
        sig = square_sig + ((w - 512.f) * (1.f / 256.f)) * (saw_sig - square_sig);
      } else {
        sig = saw_sig + ((w - 768.f) * (1.f / 255.f)) * (sine_sig - saw_sig);
      }

      // -----------------------------------------------------------------------
      // Phase advance
      // -----------------------------------------------------------------------
      phase_ += w0;
      if (phase_ >= 1.f) phase_ -= 1.f;

      // -----------------------------------------------------------------------
      // Envelope (ASR)
      // -----------------------------------------------------------------------
      switch (env_state_) {
      case ENV_ATTACK:
        amp_ += atk_inc;
        if (amp_ >= 1.f) { amp_ = 1.f; env_state_ = ENV_SUSTAIN; }
        break;
      case ENV_SUSTAIN:
        amp_ = 1.f;
        break;
      case ENV_RELEASE:
        amp_ -= rel_inc * env_phase_; // scale release by start level
        if (amp_ <= 0.f) { amp_ = 0.f; env_state_ = ENV_IDLE; }
        break;
      case ENV_IDLE:
      default:
        amp_ = 0.f;
        break;
      }

      // -----------------------------------------------------------------------
      // Mix: -16.5 dBFS headroom scaling (logue-sdk convention)
      // -----------------------------------------------------------------------
      const float out_val = sig * amp_ * params_.level * 0.15f;
      out_p[0] = in_p[0] + out_val;
      out_p[1] = in_p[1] + out_val;
    }
  }

  // ---------------------------------------------------------------------------
  // Parameter accessors
  // ---------------------------------------------------------------------------
  inline void setParameter(uint8_t index, int32_t value) {
    switch (index) {
    case ROOT:
      params_.root = (uint32_t)(value * 127 / 1023);
      updateActiveNotes();
      break;
    case CHORD:
      params_.chord = (uint8_t)std::min(value, (int32_t)8);
      updateActiveNotes();
      break;
    case PATTERN:
      params_.pattern = (uint16_t)std::min(value, (int32_t)1023);
      break;
    case MODE:
      params_.mode = (uint16_t)std::min(value, (int32_t)1023);
      break;
    case WAVE:
      params_.wave = (uint16_t)std::min(value, (int32_t)1023);
      break;
    case LEVEL:
      params_.level = (float)value / 1023.f;
      break;
    case LFO_RATE:
      params_.lfo_rate = (uint16_t)std::min(value, (int32_t)1023);
      break;
    }
  }

  inline int32_t getParameterValue(uint8_t index) const {
    switch (index) {
    case ROOT:    return (int32_t)(params_.root * 1023 / 127);
    case CHORD:   return (int32_t)params_.chord;
    case PATTERN: return (int32_t)params_.pattern;
    case MODE:    return (int32_t)params_.mode;
    case WAVE:    return (int32_t)params_.wave;
    case LEVEL:   return (int32_t)(params_.level * 1023.f);
    case LFO_RATE:return (int32_t)params_.lfo_rate;
    }
    return 0;
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const {
    if (index == CHORD) {
      static const char *names[] = {
        "Single", "Major", "Minor", "Dom 7", "Maj 7", "Min 7", "Sus 4", "Dim", "Aug"
      };
      if (value >= 0 && value < 9) return names[value];
    }
    return nullptr;
  }

private:
  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  // Map LFO_RATE (0-1023) to Hz exponentially: 0=off, 1-1023 → ~0.02..8 Hz
  inline float lfoRateHz() const {
    if (params_.lfo_rate == 0) return 0.f;
    // 0.02 * exp(t * ln(400)) where t = rate/1023
    // ln(400) ≈ 5.9915
    const float t = (float)params_.lfo_rate * (1.f / 1023.f);
    return 0.02f * expf(t * 5.9915f);
  }

  void resetState() {
    phase_     = 0.f;
    amp_       = 0.f;
    env_phase_ = 0.f;
    env_state_ = ENV_IDLE;
    smooth_hz_ = 0.f;
    lfo_phase_ = 0.f;
    is_touched_= false;
    base_hz_   = osc_notehzf(60);
  }

  void updateActiveNotes() {
    static const int8_t offsets[9][4] = {
      {0,  0,  0,  0},  // 0: Single
      {0,  4,  7,  0},  // 1: Major
      {0,  3,  7,  0},  // 2: Minor
      {0,  4,  7, 10},  // 3: Dom 7
      {0,  4,  7, 11},  // 4: Maj 7
      {0,  3,  7, 10},  // 5: Min 7
      {0,  5,  7,  0},  // 6: Sus 4
      {0,  3,  6,  0},  // 7: Dim
      {0,  4,  8,  0},  // 8: Aug
    };
    static const int8_t sizes[9] = {1, 3, 3, 4, 4, 4, 3, 3, 3};

    const int ci = std::min((int)params_.chord, 8);
    const int n  = sizes[ci];
    active_notes_count_ = n;

    for (int i = 0; i < n; ++i) {
      const uint8_t note = (uint8_t)std::min((int)params_.root + offsets[ci][i], 127);
      active_notes_[i] = note;
      noteHz_[i] = osc_notehzf(note);
    }
    base_hz_ = noteHz_[0];

    // Pre-fill safety slot above last note (same as last)
    noteHz_[n] = noteHz_[n - 1];
  }

  // ---------------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------------
  Params   params_;
  uint32_t samplerate_{48000};

  // Chord note cache
  uint8_t  active_notes_[12];
  float    noteHz_[13]; // +1 safety slot for lerp upper bound
  int      active_notes_count_{1};
  float    base_hz_{261.63f}; // C4

  // LFO
  float lfo_phase_{0.f}; // 0..1 phasor

  // Oscillator
  float phase_{0.f};

  // Frequency glide
  float smooth_hz_{0.f};

  // Envelope (ASR)
  enum EnvState : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_SUSTAIN, ENV_RELEASE };
  EnvState env_state_{ENV_IDLE};
  float amp_{0.f};
  float env_phase_{0.f}; // amp at moment of release

  bool is_touched_{false};
};
