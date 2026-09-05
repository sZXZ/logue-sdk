#pragma once
/*
 *  File: nts3_clouds.h
 *
 *  NTS-3 "Clouds" granular synthesizer - class definition.
 *
 *  A stereo granular engine for the KORG NTS-3 kaoss pad kit (logue-sdk
 *  genericfx API), recreating the core algorithms of the Mutable Instruments
 *  "Clouds" module:
 *    - continuous circular recording buffer in external SDRAM
 *    - density-driven grain scheduler with overlapping grains
 *    - linear-interpolated buffer reads for transposition
 *    - Hann / raised-cosine grain envelopes (morphable toward square)
 *
 *  Buffer layout (allocated once in SDRAM as a single float array):
 *      [0 ... buffer_size_-1]            -> left channel
 *      [buffer_size_ ... 2*size_-1]      -> right channel
 *  Both halves are power-of-two sized so reads wrap via a bitmask.
 */

#include <cstdint>
#include <cstddef>

#include "processor.h"
#include "unit_genericfx.h"

// One active grain (voice) of the granular engine.
struct Grain {
  float    read_pos;  // absolute fractional read index into buffer [samples]
  float    speed;     // playback rate; 1.0f = original pitch, >1 = faster/higher
  uint32_t length;    // grain length in output samples
  uint32_t elapsed;   // output samples produced so far
  float    pan_l;     // left gain (amp * left pan, 0..1)
  float    pan_r;     // right gain (amp * right pan, 0..1)
  bool     active;    // slot in use
};

// Normalised parameter backing store.  setParameter() converts the raw
// integer values coming from the logue-sdk (0..1023, or bipolar ranges) into
// these DSP variables.
//
//  logue-sdk control -> DSP variable mapping
//    X axis  (mapped to PARAM_POSITION) -> position    [0..1]
//    Y axis  (mapped to PARAM_DENSITY)  -> density     [-1..+1, 0 = silence]
//    FX depth (mapped to DEPTH)         -> dry_wet     [0..1]
//    Encoder 1 (PARAM_SIZE)             -> size_ms     [~0.1 .. 1000] ms
//    Encoder 2 (PARAM_PITCH)            -> pitch_semi  [-12 .. +12] semitones
//    Encoder 3 (PARAM_TEXTURE)          -> texture     [0..1]
//    Encoder 4 (PARAM_FREEZE)           -> freeze      [0 | 1]
//    8th param (PARAM_FEEDBACK)         -> feedback    [0..1]
struct CloudParams {
  float position;    // 0 = current input, 1 = furthest back in buffer
  float density;     // signed; magnitude = spawn rate, sign = walk direction
  float dry_wet;     // 0 = fully dry, 1 = fully wet
  float size_ms;     // grain length
  float pitch_semi;  // transposition, musical semitones
  float texture;     // 0 = square ... 0.5 = triangle ... 1 = Hann
  int   freeze;      // 0 = record, 1 = freeze (playback only)
  float feedback;    // wet-signal echo regeneration amount (0..1)

  void reset() {
    position   = 0.0f;
    density    = 0.0f;   // silence by default (vertical centre)
    dry_wet    = 1.0f;   // fully wet on load
    size_ms    = 100.0f;
    pitch_semi = 0.0f;   // unison
    texture    = 0.5f;   // triangle
    freeze     = 0;
    feedback   = 0.0f;   // off
  }
};

class CloudsEffect : public Processor {
public:
  static constexpr uint32_t kMaxGrains = 24;   // hard polyphony ceiling
  static constexpr float    kBufferSeconds = 1.5f; // SDRAM target duration
  static constexpr uint32_t kSampleRate = 48000;
  // Feedback echo line length (per channel): 0.25 s @ 48 kHz = 12000 samples.
  static constexpr uint32_t kFeedbackDelaySamples = 12000u;
  static constexpr uint32_t getFeedbackBufferSize() { return 2 * kFeedbackDelaySamples; }
  void setFeedbackBuffer(float *buf) { fb_delay_ = buf; }

  enum {
    PARAM_POSITION = 0U, // X axis  -> grain position in buffer
    PARAM_DENSITY,       // Y axis  -> grain density
    DEPTH,               // FX depth -> dry/wet
    PARAM_SIZE,          // Enc 1   -> grain size
    PARAM_PITCH,         // Enc 2   -> pitch transpose
    PARAM_TEXTURE,       // Enc 3   -> envelope morph / diffusion
    PARAM_FREEZE,        // Enc 4   -> freeze toggle
    PARAM_FEEDBACK,      // 8th param -> wet-to-buffer feedback
    NUM_PARAMS
  };

  // ---- Processor interface -------------------------------------------------
  uint32_t getBufferSize() const override final;
  void init(float *allocated_buffer) override final;
  void teardown() override final;
  void reset() override final;
  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final;
  void setParameter(uint8_t id, int32_t value) override final;
  const char *getParameterStrValue(uint8_t id, int32_t value) const override final;
  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final;

private:
  // ---- DSP state -----------------------------------------------------------
  float *buffer_;          // SDRAM allocation: [L][R]
  uint32_t buffer_size_;   // samples per channel
  uint32_t buffer_mask_;   // buffer_size_ - 1 (power of two wrap)

  uint32_t write_pos_;     // current record index per channel
  uint32_t active_grains_; // running count of active voices

  // Scheduler state
  float    spawn_acc_;     // Poisson-style spawn accumulator
  float    spawn_rate_;    // grains per second (derived from density)
  float    grain_amp_;     // per-grain amplitude (compensates overlap level)
  int      walk_dir_;      // +1 forward in buffer, -1 backward (density sign)

  // Smoothed / cached control values (recomputed per block)
  float    size_samples_;
  float    speed_;         // 2^(pitch_semi/12)
  float    texture_;
  float    dry_wet_;
  float    position_;
  bool     freeze_;
  float    feedback_;      // wet echo regeneration amount (0..1)

  // Dedicated regenerating echo delay on the wet bus (optional SDRAM line).
  float    *fb_delay_;     // [L][R] stereo blocks, or nullptr if unavailable
  uint32_t fb_size_;       // echo samples per channel
  uint32_t fb_w_;          // current write/read index of the echo line

  // Per-grain pan randomization seed progression
  uint32_t rng_state_;

  Grain    grains_[kMaxGrains];
  CloudParams params_;

  // ---- helpers -------------------------------------------------------------
  inline uint32_t next_rng();
  inline float    hann_envelope(uint32_t elapsed, uint32_t length) const;
  inline float    envelope(uint32_t elapsed, uint32_t length) const;
  inline void     spawn_grain();
};