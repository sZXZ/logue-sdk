#pragma once
/*
 *  File: effect.h
 *
 *  Arpeggiator logic for NTS-3 kaoss pad kit
 *
 */

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "osc_api.h"
#include "unit_genericfx.h"
#include "utils/float_math.h"

class Arpeggiator {
public:
  enum { ROOT = 0U, CHORD, GATE, PATTERN, MODE, WAVE, LEVEL, ADSR, NUM_PARAMS };
  // MODE encodes both direction and octave range: value = direction*4 + (octaves-1)
  // Directions: 0=Up 1=Down 2=UpDown 3=Random 4=Seq   Octaves: 1-4
  // WAVE is a continuous 0-1023 morph: 0=Sine, 256=Triangle, 512=Square, 768=Saw, 1023=Sine
  // ADSR is a 0-1023 morph across 5 envelope presets (Pluck/Pad/Perc/Swell/LongRel)
  enum { UP = 0, DOWN, UPDOWN, RANDOM, SEQ };

  // Rhythmic divisions (in ticks, where 1 quarter note = 480 logical ticks for
  // high resolution) Higher resolution allows for perfect triplets (480 is
  // divisible by 2, 3, 4, 5)
  enum Pattern {
    P_1_4 = 0, // 1/4 note
    P_1_4T,    // 1/4 triplet
    P_1_8,     // 1/8 note
    P_1_8T,    // 1/8 triplet
    P_1_16,    // 1/16 note
    P_1_16T,   // 1/16 triplet
    P_1_32,    // 1/32 note
    P_1_32T    // 1/32 triplet
  };

  struct Params {
    uint32_t root{60};      // MIDI note
    uint8_t chord{0};       // Chord index
    float gate{0.5f};       // Gate length (0-1)
    uint8_t pattern{P_1_8}; // Rhythmic pattern
    uint8_t mode{UP};       // Playback direction (UP/DOWN/UPDOWN/RANDOM/SEQ)
    uint8_t range{1};       // Octave span 1-4 (derived from combined MODE param)
    uint16_t wave{0};       // Wave morph 0-1023 (0=Sine,256=Triangle,512=Square,768=Saw,1023=Sine)
    float level{0.5f};      // Output level (0-1)
    uint16_t adsr{0};       // ADSR morph 0-1023 (Pluck->Pad->Perc->Swell->LongRel)

    void reset() {
      root = 60;
      chord = 0;
      gate = 0.5f;
      pattern = P_1_8;
      mode = UP;
      range = 1;
      wave = 0;
      level = 0.5f;
      adsr = 0;
    }
  };

  // ---------------------------------------------------------------------------
  // ADSR envelope preset curves
  // All time values are in seconds; sustain is a 0-1 level.
  // ---------------------------------------------------------------------------
  struct AdsrPreset {
    float attack;   // seconds
    float decay;    // seconds
    float sustain;  // 0-1 level
    float release;  // seconds
  };

  // Linearly interpolate between two adjacent presets (passed by value, C++11 safe).
  inline AdsrPreset morphAdsr(float t, AdsrPreset a, AdsrPreset b) const {
    AdsrPreset r;
    r.attack  = a.attack  + t * (b.attack  - a.attack);
    r.decay   = a.decay   + t * (b.decay   - a.decay);
    r.sustain = a.sustain + t * (b.sustain - a.sustain);
    r.release = a.release + t * (b.release - a.release);
    return r;
  }

  // Compute the current morphed ADSR from params_.adsr (0-1023).
  // Zones: 0-255 -> presets[0..1], 256-511 -> [1..2],
  //        512-767 -> [2..3], 768-1023 -> [3..4]
  inline AdsrPreset currentAdsr() const {
    // Local static avoids C++11 ODR issues with constexpr class members.
    static const AdsrPreset kPresets[5] = {
      { 0.002f, 0.06f,  0.0f,  0.04f  }, // 0: Pluck
      { 0.4f,   0.3f,   0.8f,  0.6f   }, // 1: Pad
      { 0.001f, 0.12f,  0.0f,  0.015f }, // 2: Percussive
      { 0.25f,  0.4f,   0.9f,  0.35f  }, // 3: Swell
      { 0.01f,  0.2f,   0.6f,  1.2f   }, // 4: Long Release
    };
    const float v = (float)params_.adsr;
    if (v < 256.f)
      return morphAdsr(v * (1.f / 256.f), kPresets[0], kPresets[1]);
    else if (v < 512.f)
      return morphAdsr((v - 256.f) * (1.f / 256.f), kPresets[1], kPresets[2]);
    else if (v < 768.f)
      return morphAdsr((v - 512.f) * (1.f / 256.f), kPresets[2], kPresets[3]);
    else
      return morphAdsr((v - 768.f) * (1.f / 255.f), kPresets[3], kPresets[4]);
  }

  Arpeggiator(void) { active_notes_count_ = 0; }

  ~Arpeggiator(void) {}

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
    reset_state();
    setTempo(120 << 16); // 120 BPM in UQ16.16
    updateActiveNotes();

    return k_unit_err_none;
  }

  inline void Teardown() {}

  inline void Reset() { reset_state(); }
  inline void Resume() { reset_state(); }
  inline void Suspend() { 
    is_active_ = false; 
    is_touched_ = false; 
    target_amp_ = 0.f; 
  }

  inline void reset_state() {
    is_active_ = true;
    is_touched_ = false;
    phase_ = 0.f;
    amp_ = 0.f;
    target_amp_ = 0.f;
    env_state_ = ENV_IDLE;
    env_phase_ = 0.f;
    current_note_hz_ = 0.f;
    smooth_hz_ = 0.f;

    seq_index_ = 0;
    seq_dir_ = 1;

    samples_per_tick_accum_ = 0.f;
    setTempo(last_tempo_);
  }

  // BPM is in UQ16.16 format (integer part in upper 16 bits, fractional in lower 16)
  inline void setTempo(uint32_t tempo) {
    last_tempo_ = tempo;
    float bpm = (tempo >> 16) + (tempo & 0xFFFF) / 65536.0f;
    if (bpm < 1.0f)
      bpm = 1.0f;

    // Calculate samples per 1/4 note
    float samples_per_beat = (60.0f / bpm) * samplerate_;

    // We use a "logical tick" resolution of 1/480 per quarter note.
    // However, for simplicity and stability, we can just calculate
    // the period for each pattern division directly.
    updatePatternPeriod(samples_per_beat);
  }

  inline void updatePatternPeriod(float samples_per_beat) {
    static const float divisions[] = {
        1.0f,        // 1/4
        2.0f / 3.0f, // 1/4T
        0.5f,        // 1/8
        1.0f / 3.0f, // 1/8T
        0.25f,       // 1/16
        1.0f / 6.0f, // 1/16T
        0.125f,      // 1/32
        1.0f / 12.0f // 1/32T
    };

    samples_per_step_ = samples_per_beat * divisions[params_.pattern & 7];
  }

  inline void tempoTick(uint32_t counter) {
    // Not strictly needed if we use sample-accurate timing,
    // but can be used for sync reset if desired.
    (void)counter;
  }

  fast_inline void Process(const float *in, float *out, size_t frames) {
    const float *__restrict in_p = in;
    float *__restrict out_p = out;
    const float *out_e = out_p + (frames << 1);

    // Pre-compute morphed ADSR coefficients once per block.
    const AdsrPreset env = currentAdsr();
    const float sr_inv = 1.f / (float)samplerate_;
    // Convert time->per-sample increment (0..1 over the time window).
    const float atk_inc = (env.attack  > 0.f) ? sr_inv / env.attack  : 1.f;
    const float dec_inc = (env.decay   > 0.f) ? sr_inv / env.decay   : 1.f;
    const float rel_inc = (env.release > 0.f) ? sr_inv / env.release : 1.f;
    const float sus_lvl = env.sustain;

    for (; out_p != out_e; in_p += 2, out_p += 2) {
      if (is_touched_) {
        // --- Arpeggiator step clock ---
        samples_per_tick_accum_ += 1.0f;
        if (samples_per_tick_accum_ >= samples_per_step_) {
          samples_per_tick_accum_ -= samples_per_step_;
          advanceSequence();
          triggerNote();
        }

        // Gate close: when accumulated time exceeds gate fraction, begin release.
        const float gate_samples = samples_per_step_ * params_.gate;
        if (samples_per_tick_accum_ > gate_samples &&
            env_state_ != ENV_RELEASE && env_state_ != ENV_IDLE) {
          env_state_ = ENV_RELEASE;
          env_phase_ = amp_; // release from current level
        }
      } else {
        // Touch ended: force release if not already idle.
        if (env_state_ != ENV_IDLE) {
          env_state_ = ENV_RELEASE;
          env_phase_ = amp_;
        }
      }

      // --- ADSR envelope stepper ---
      switch (env_state_) {
      case ENV_ATTACK:
        amp_ += atk_inc;
        if (amp_ >= 1.f) {
          amp_ = 1.f;
          env_state_ = ENV_DECAY;
        }
        break;
      case ENV_DECAY:
        amp_ -= dec_inc * (1.f - sus_lvl);
        if (amp_ <= sus_lvl) {
          amp_ = sus_lvl;
          env_state_ = (sus_lvl > 0.f) ? ENV_SUSTAIN : ENV_IDLE;
        }
        break;
      case ENV_SUSTAIN:
        amp_ = sus_lvl; // held until gate closes
        break;
      case ENV_RELEASE:
        amp_ -= rel_inc * env_phase_; // scale by start-of-release level
        if (amp_ <= 0.f) {
          amp_ = 0.f;
          env_state_ = ENV_IDLE;
        }
        break;
      case ENV_IDLE:
      default:
        amp_ = 0.f;
        break;
      }

      // Smooth frequency transitions
      smooth_hz_ += (current_note_hz_ - smooth_hz_) * 0.01f;
      float w0 = smooth_hz_ * (1.0f / 48000.0f);

      // Calculate Oscillator - morph across wave slider (0-1023)
      // Segments: 0-255 Sine->Triangle, 256-511 Triangle->Square, 512-767 Square->Saw, 768-1023 Saw->Sine
      float sig = 0.f;
      {
        const float w = (float)params_.wave;
        const float sine_sig   = osc_sinf(phase_);
        const float tri_sig    = (phase_ < 0.5f) ? (-1.0f + 4.0f * phase_) : (3.0f - 4.0f * phase_);
        const float square_sig = osc_sqrf(phase_);
        const float saw_sig    = osc_sawf(phase_);
        if (w < 256.f) {
          float t = w * (1.f / 256.f);
          sig = sine_sig + t * (tri_sig - sine_sig);
        } else if (w < 512.f) {
          float t = (w - 256.f) * (1.f / 256.f);
          sig = tri_sig + t * (square_sig - tri_sig);
        } else if (w < 768.f) {
          float t = (w - 512.f) * (1.f / 256.f);
          sig = square_sig + t * (saw_sig - square_sig);
        } else {
          float t = (w - 768.f) * (1.f / 255.f);
          sig = saw_sig + t * (sine_sig - saw_sig);
        }
      }

      phase_ += w0;
      if (phase_ >= 1.f)
        phase_ -= 1.f;

      // Scale output by 0.15f to match internal logue-sdk headroom (-16.5dBFS)
      float out_val = sig * amp_ * params_.level * 0.15f;

      out_p[0] = in_p[0] + out_val;
      out_p[1] = in_p[1] + out_val;
    }
  }

  inline void setParameter(uint8_t index, int32_t value) {
    switch (index) {
    case ROOT:
      params_.root = value * 127 / 1023; // C-1 to G9
      updateActiveNotes();
      break;
    case CHORD:
      params_.chord = value;
      updateActiveNotes();
      break;
    case GATE:
      params_.gate = value / 1023.f;
      break;
    case PATTERN:
      params_.pattern = value;
      setTempo(last_tempo_); // refresh timing
      break;
    case MODE:
      // Combined mode+range: value = direction*4 + (octaves-1), range 0-19
      params_.mode  = (uint8_t)std::min(value / 4, (int32_t)4);
      params_.range = (uint8_t)(value % 4) + 1;
      updateActiveNotes();
      break;
    case WAVE:
      params_.wave = (uint16_t)std::min(value, (int32_t)1023);
      break;
    case LEVEL:
      params_.level = value / 1023.f;
      break;
    case ADSR:
      params_.adsr = (uint16_t)std::min(value, (int32_t)1023);
      break;
    }
  }

  inline int32_t getParameterValue(uint8_t index) const {
    switch (index) {
    case ROOT:
      return params_.root * 1023 / 127;
    case CHORD:
      return params_.chord;
    case GATE:
      return (int32_t)(params_.gate * 1023.f);
    case PATTERN:
      return params_.pattern;
    case MODE:
      return (int32_t)(params_.mode * 4 + (params_.range - 1));
    case WAVE:
      return (int32_t)params_.wave;
    case LEVEL:
      return (int32_t)(params_.level * 1023.f);
    case ADSR:
      return (int32_t)params_.adsr;
    }
    return 0;
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const {
    switch (index) {
    case CHORD: {
      static const char *names[] = {"Single", "Major", "Minor",
                                    "Dom 7",  "Maj 7", "Min 7",
                                    "Sus 4",  "Dim",   "Aug"};
      if (value >= 0 && value < 9)
        return names[value];
      break;
    }
    case PATTERN: {
      static const char *names[] = {"1/4",  "1/4T",  "1/8",  "1/8T",
                                    "1/16", "1/16T", "1/32", "1/32T"};
      if (value >= 0 && value < 8)
        return names[value];
      break;
    }
    case MODE: {
      static const char *names[] = {
        "Up 1",   "Up 2",   "Up 3",   "Up 4",
        "Down 1", "Down 2", "Down 3", "Down 4",
        "UpDn 1", "UpDn 2", "UpDn 3", "UpDn 4",
        "Rnd 1",  "Rnd 2",  "Rnd 3",  "Rnd 4",
        "Seq 1",  "Seq 2",  "Seq 3",  "Seq 4",
      };
      if (value >= 0 && value < 20)
        return names[value];
      break;
    }
    case WAVE:
      // Continuous slider - no string value needed
      return nullptr;
    }
    return nullptr;
  }

  inline void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) {
    (void)id;
    if (phase == k_unit_touch_phase_began) {
      is_touched_ = true;
      samples_per_tick_accum_ = samples_per_step_; // trigger immediately
      seq_index_ = (params_.mode == DOWN) ? (active_notes_count_ - 1) : 0;
      seq_dir_ = 1;
    } else if (phase == k_unit_touch_phase_moved) {
      // Update Root and Chord via X/Y if desired,
      // but they are already mapped via parameters in header.c
      // So no need to do anything here unless we want to bypass parameter
      // smoothing.
    } else if (phase == k_unit_touch_phase_ended ||
               phase == k_unit_touch_phase_cancelled) {
      is_touched_ = false;
      target_amp_ = 0.f;
    }
  }

private:
  inline void updateActiveNotes() {
    static const int8_t chord_offsets[][4] = {
        {0, 0, 0, 0},  // Single
        {0, 4, 7, 0},  // Major
        {0, 3, 7, 0},  // Minor
        {0, 4, 7, 10}, // Dom 7
        {0, 4, 7, 11}, // Maj 7
        {0, 3, 7, 10}, // Min 7
        {0, 5, 7, 0},  // Sus 4
        {0, 3, 6, 0},  // Dim
        {0, 4, 8, 0}   // Aug
    };

    int chord_idx = std::min((uint8_t)params_.chord, (uint8_t)8);
    int notes_in_chord = 0;
    if (chord_idx == 0)
      notes_in_chord = 1;
    else if (chord_idx == 3 || chord_idx == 4 || chord_idx == 5)
      notes_in_chord = 4;
    else
      notes_in_chord = 3;

    active_notes_count_ = 0;
    for (int oct = 0; oct < params_.range; ++oct) {
      for (int i = 0; i < notes_in_chord; ++i) {
        if (active_notes_count_ < 32) {
          active_notes_[active_notes_count_++] =
              params_.root + chord_offsets[chord_idx][i] + (oct * 12);
        }
      }
    }
  }

  inline void advanceSequence() {
    if (active_notes_count_ == 0)
      return;

    switch (params_.mode) {
    case UP:
      seq_index_ = (seq_index_ + 1) % active_notes_count_;
      break;
    case DOWN:
      if (seq_index_ == 0)
        seq_index_ = active_notes_count_ - 1;
      else
        seq_index_--;
      break;
    case UPDOWN:
      seq_index_ += seq_dir_;
      if (seq_index_ >= (int)active_notes_count_ - 1) {
        seq_index_ = active_notes_count_ - 1;
        seq_dir_ = -1;
      } else if (seq_index_ <= 0) {
        seq_index_ = 0;
        seq_dir_ = 1;
      }
      break;
    case RANDOM:
      seq_index_ = osc_rand() % active_notes_count_;
      break;
    case SEQ:
      // Same as UP for now, or could implement a specific pattern
      seq_index_ = (seq_index_ + 1) % active_notes_count_;
      break;
    }
  }

  inline void triggerNote() {
    if (active_notes_count_ == 0)
      return;
    uint8_t note = active_notes_[seq_index_ % 32];
    current_note_hz_ = osc_notehzf(note);

    // Legato: if the envelope is active (not idle), skip re-attack from zero
    // to avoid a click — simply restart the attack phase from the current
    // amplitude so the transition is smooth.
    if (env_state_ != ENV_IDLE) {
      // Stay at current amp_, re-enter attack from here.
      env_state_ = ENV_ATTACK;
      // amp_ is already at its current level; the attack ramp will continue
      // upward from it, which is glitch-free.
    } else {
      amp_ = 0.f;
      env_state_ = ENV_ATTACK;
    }
  }

  Params params_;
  uint32_t samplerate_{48000};
  uint32_t last_tempo_{120 << 16};

  // Arp State
  uint8_t active_notes_[32];
  uint8_t active_notes_count_;
  int seq_index_;
  int seq_dir_;

  float samples_per_step_;
  float samples_per_tick_accum_;

  // ADSR envelope state
  enum EnvState : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
  EnvState env_state_{ENV_IDLE};
  float env_phase_{0.f}; // amplitude at start of release (for rel shape scaling)

  // DSP State
  float phase_;
  float amp_;
  float target_amp_; // kept for Suspend()/legacy compat, not used in envelope path
  float current_note_hz_;
  float smooth_hz_;

  bool is_active_;
  bool is_touched_;
};
