/*
 *  File: nts3_clouds.cpp
 *
 *  NTS-3 "Clouds" granular synthesizer - implementation.
 *
 *  Recreates the mathematical concepts of the Mutable Instruments "Clouds"
 *  module (Hann/raised-cosine grain envelopes, interpolated buffer reads for
 *  transposition, density-driven grain scheduling) as a self-contained effect
 *  for the KORG NTS-3 kaoss pad kit, without pulling in the STM32/HAL-tied
 *  original source.
 *
 *  Each sample of the audio thread:
 *    1. Records the incoming stereo pair into the circular SDRAM buffer
 *       (unless frozen).
 *    2. Advances every active grain: reads an interpolated window from the
 *       buffer at the grain's read position, weights it by an envelope, and
 *       accumulates it into the pan-adjusted output.
 *    3. Rolls the density scheduler to decide whether to spawn a new grain.
 *
 *  Performance notes for the M4/M7 core:
 *    - Interpolation is linear with a power-of-two circular index (bitmask
 *      wrap, no division).
 *    - Envelopes use a cheap raised-cosine/piecewise-linear evaluation.
 *    - Heavy per-sample work is bounded by kMaxGrains voice stealing.
 */

#include "nts3_clouds.h"

#include <cmath>

#include "utils/int_math.h"

// ---------------------------------------------------------------------------
// Minimal unsigned-integer rendering (no stdio on this embedded target).
// Writes the ASCII digits of `v` into `buf` (must hold up to 10 digits) and
// returns the number of characters written. Avoids linking newlib's printf
// machinery (and its weak `_printf_float` hook), which the unit loader cannot
// bind as a PLT symbol.
// ---------------------------------------------------------------------------
static uint32_t render_uint(char *buf, uint32_t v) {
  char  tmp[10];
  uint32_t n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0u);
  for (uint32_t i = 0u; i < n; ++i)
    buf[i] = tmp[n - 1u - i];
  return n;
}

// ---------------------------------------------------------------------------
// Self-contained half-period sine table (sin(pi*x), x in [0,1], 129 entries).
// The grain Hann window is built from this so the unit references no
// firmware-exported wavetables (keeps the loader's symbol resolution trivial).
// ---------------------------------------------------------------------------
static const float s_sin_pi_lut[129] = {
    0.00000000f, 0.02454123f, 0.04906767f, 0.07356456f, 0.09801714f,
    0.12241068f, 0.14673047f, 0.17096189f, 0.19509032f, 0.21910124f,
    0.24298018f, 0.26671276f, 0.29028468f, 0.31368174f, 0.33688985f,
    0.35989504f, 0.38268343f, 0.40524131f, 0.42755509f, 0.44961133f,
    0.47139674f, 0.49289819f, 0.51410274f, 0.53499762f, 0.55557023f,
    0.57580819f, 0.59569930f, 0.61523159f, 0.63439328f, 0.65317284f,
    0.67155895f, 0.68954054f, 0.70710678f, 0.72424708f, 0.74095113f,
    0.75720885f, 0.77301045f, 0.78834643f, 0.80320753f, 0.81758481f,
    0.83146961f, 0.84485357f, 0.85772861f, 0.87008699f, 0.88192126f,
    0.89322430f, 0.90398929f, 0.91420976f, 0.92387953f, 0.93299280f,
    0.94154407f, 0.94952818f, 0.95694034f, 0.96377607f, 0.97003125f,
    0.97570213f, 0.98078528f, 0.98527764f, 0.98917651f, 0.99247953f,
    0.99518473f, 0.99729046f, 0.99879546f, 0.99969882f, 1.00000000f,
    0.99969882f, 0.99879546f, 0.99729046f, 0.99518473f, 0.99247953f,
    0.98917651f, 0.98527764f, 0.98078528f, 0.97570213f, 0.97003125f,
    0.96377607f, 0.95694034f, 0.94952818f, 0.94154407f, 0.93299280f,
    0.92387953f, 0.91420976f, 0.90398929f, 0.89322430f, 0.88192126f,
    0.87008699f, 0.85772861f, 0.84485357f, 0.83146961f, 0.81758481f,
    0.80320753f, 0.78834643f, 0.77301045f, 0.75720885f, 0.74095113f,
    0.72424708f, 0.70710678f, 0.68954054f, 0.67155895f, 0.65317284f,
    0.63439328f, 0.61523159f, 0.59569930f, 0.57580819f, 0.55557023f,
    0.53499762f, 0.51410274f, 0.49289819f, 0.47139674f, 0.44961133f,
    0.42755509f, 0.40524131f, 0.38268343f, 0.35989504f, 0.33688985f,
    0.31368174f, 0.29028468f, 0.26671276f, 0.24298018f, 0.21910124f,
    0.19509032f, 0.17096189f, 0.14673047f, 0.12241068f, 0.09801714f,
    0.07356456f, 0.04906767f, 0.02454123f, 0.00000000f};

// ---------------------------------------------------------------------------
// Buffer size: we target >= 1.5s of stereo audio in SDRAM.
//   per-channel samples = round_up_pow2(seconds * sample_rate)
//   return floats needed for [L][R] = 2 * per-channel.
// ---------------------------------------------------------------------------
uint32_t CloudsEffect::getBufferSize() const {
  const uint32_t desired = (uint32_t)(kBufferSeconds * (float)kSampleRate);
  uint32_t n = 1;
  while (n < desired)
    n <<= 1;
  if (n < (1U << 12))
    n = 1U << 12;            // keep a usable minimum
  return n * 2;              // two channels
}

// ---------------------------------------------------------------------------
void CloudsEffect::init(float *allocated_buffer) {
  buffer_ = allocated_buffer;

  const uint32_t total = getBufferSize();
  buffer_size_ = total >> 1;
  buffer_mask_ = buffer_size_ - 1;

  write_pos_ = 0;
  active_grains_ = 0;
  spawn_acc_ = 0.0f;
  spawn_rate_ = 0.0f;
  grain_amp_ = 0.0f;
  walk_dir_ = 1;
  rng_state_ = 0x9E3779B9u;

  for (uint32_t i = 0; i < kMaxGrains; ++i)
    grains_[i].active = false;

  params_.reset();

  // initialise cached control values
  size_samples_ = params_.size_ms * 0.001f * (float)kSampleRate;
  speed_    = 1.0f;
  texture_  = params_.texture;
  dry_wet_  = params_.dry_wet;
  position_ = params_.position;
  freeze_   = params_.freeze != 0;
}

void CloudsEffect::teardown() {
  buffer_ = nullptr;
  active_grains_ = 0;
}

void CloudsEffect::reset() {
  write_pos_ = 0;
  active_grains_ = 0;
  spawn_acc_ = 0.0f;
  spawn_rate_ = 0.0f;
  grain_amp_ = 0.0f;
  walk_dir_ = 1;
  for (uint32_t i = 0; i < kMaxGrains; ++i)
    grains_[i].active = false;
}

// ---------------------------------------------------------------------------
// Small xorshift PRNG.  Deterministic, cheap, adequate for grain timing.
// ---------------------------------------------------------------------------
inline uint32_t CloudsEffect::next_rng() {
  uint32_t x = rng_state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state_ = x;
  return x;
}

// ---------------------------------------------------------------------------
// Raised-cosine "Hann" window over [0, length-1] -> [0,1].
// T = elapsed/length.  Hann = 0.5 - 0.5*cos(2*pi*T) = 0.5 - 0.5*sin(2*pi*(T+0.25)).
// Uses our own static half-sine LUT (no firmware-exported wavetables).
// ---------------------------------------------------------------------------
inline float CloudsEffect::hann_envelope(uint32_t elapsed, uint32_t length) const {
  const float t = (float)elapsed * (1.0f / (float)length);
  const float phase = t + 0.25f;
  const float p = phase - (float)(uint32_t)phase;   // wrap to [0,1)

  // half-period table: sin(2*pi*p) for p<0.5 indexes [0,128), negate beyond.
  const float x0f = 2.0f * p * 128.0f;
  const uint32_t x0p = (uint32_t)x0f;
  const uint32_t x0 = x0p & 127u;
  const uint32_t x1 = (x0 + 1u) & 127u;
  float syn = s_sin_pi_lut[x0] +
              (s_sin_pi_lut[x1] - s_sin_pi_lut[x0]) * (x0f - (float)x0p);
  if (x0p >= 128u)
    syn = -syn;

  return 0.5f - 0.5f * syn;
}

// ---------------------------------------------------------------------------
// Morphable grain envelope.
//   texture 0.0   -> square (constant, clicky when layered)
//   texture 0.5   -> triangle (linear attack/decay)
//   texture 1.0   -> Hann (smooth raised cosine, Clouds' classic shape)
// ---------------------------------------------------------------------------
inline float CloudsEffect::envelope(uint32_t elapsed, uint32_t length) const {
  const float t = (float)elapsed * (1.0f / (float)length);

  if (texture_ <= 0.02f)
    return 1.0f;                                         // square
  if (texture_ >= 0.98f)
    return hann_envelope(elapsed, length);               // Hann
  if (texture_ <= 0.5f) {                                // square -> triangle
    const float k = texture_ * 2.0f;                     // 0..1
    const float tri = (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));
    return (1.0f - k) + k * tri;
  }
  // triangle -> Hann
  const float k = (texture_ - 0.5f) * 2.0f;              // 0..1
  const float tri = (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));
  const float han = hann_envelope(elapsed, length);
  return (1.0f - k) * tri + k * han;
}

// ---------------------------------------------------------------------------
// Spawn a grain.  Position/size/speed/pan are sampled from the current
// control values (plus a texture-scaled random jitter for "spread").
// ---------------------------------------------------------------------------
inline void CloudsEffect::spawn_grain() {
  // Choose the oldest grain as the steal victim when the pool is full.
  uint32_t slot = kMaxGrains;
  for (uint32_t i = 0; i < kMaxGrains; ++i) {
    if (!grains_[i].active) { slot = i; break; }
  }
  if (slot == kMaxGrains) {
    uint32_t oldest = 0, best = 0;
    for (uint32_t i = 0; i < kMaxGrains; ++i) {
      if (grains_[i].elapsed >= best) { best = grains_[i].elapsed; oldest = i; }
    }
    --active_grains_;
    slot = oldest;
  }

  // --- position ------------------------------------------------------------
  // Age maps to a read offset: position_ = 0 means the newest sample just
  // written (write_pos_ - 1); position_ = 1 means the oldest sample still in
  // the buffer (write_pos_, i.e. one full rotation behind the write head).
  const float pos_samples = position_ * (float)(buffer_size_ - 1);

  // Texture adds random jitter around the position (diffusion/spectral smear).
  uint32_t r = next_rng();
  const float jitter = (float)(int32_t)((r >> 16) & 0xFFFF) / 32768.0f;
  float read_off = pos_samples + jitter * texture_ * 32.0f;
  if (read_off < 0.0f)
    read_off = 0.0f;
  if (read_off > (float)buffer_mask_)
    read_off = (float)buffer_mask_;

  // Absolute circular-buffer index where the grain's read head starts.
  // age 0 (newest) == write_pos_ - 1 ; age buffer_size (oldest) == write_pos_.
  // walk_dir_ only selects the sweep direction afterwards (+1 toward input).
  float read_abs = (float)write_pos_ + (float)buffer_size_ - 1.0f - read_off;
  // Wrap into [0, buffer_size)
  while (read_abs >= (float)buffer_size_)
    read_abs -= (float)buffer_size_;

  Grain &g = grains_[slot];
  g.read_pos = read_abs;
  g.speed    = speed_;
  g.length   = (uint32_t)(size_samples_ + 0.5f);
  if (g.length < 16)
    g.length = 16;                       // avoid degenerate clicks
  g.elapsed  = 0;

  // Randomised pan for a wide stereo image, scaled by overlap-compensating
  // amplitude so that sparse and dense clouds sit at similar loudness.
  const float pane_l = ((r >> 24) & 0xFF) * (1.0f / 255.0f); // 0..1
  const float ang = pane_l * 1.57079632679f;                 // 0..pi/2
  g.pan_l = std::cos(ang) * grain_amp_;
  g.pan_r = std::sin(ang) * grain_amp_;

  g.active  = true;
  ++active_grains_;
}

// ---------------------------------------------------------------------------
// setParameter: convert raw logue-sdk integer values into DSP variables.
// ---------------------------------------------------------------------------
void CloudsEffect::setParameter(uint8_t id, int32_t value) {
  switch (id) {
    case PARAM_POSITION: // X axis 0..1023 -> 0..1
      params_.position = (float)clipminmaxi32(0, value, 1023) * (1.0f / 1023.0f);
      break;
    case PARAM_DENSITY:  // Y axis 0..1023 -> -1..+1 (centre 512 -> silent)
      params_.density = (float)clipminmaxi32(0, value, 1023) * (2.0f / 1023.0f) - 1.0f;
      break;
    case DEPTH:          // FX depth -> dry/wet 0..1
      params_.dry_wet = (float)clipminmaxi32(0, value, 1023) * (1.0f / 1023.0f);
      break;
    case PARAM_SIZE: {   // Encoder 1: logarithmic grain size 0.1ms .. 1000ms
      const float n = (float)clipminmaxi32(0, value, 1023) * (1.0f / 1023.0f);
      params_.size_ms = std::exp(-2.3025851f + n * 9.2102403f); // e^-2.3 .. e^6.9
      break;
    }
    case PARAM_PITCH:    // Encoder 2: musical transpose -12 .. +12 semitones
      params_.pitch_semi = (float)clipminmaxi32(-12, value, 12);
      break;
    case PARAM_TEXTURE:  // Encoder 3: envelope morph 0..1
      params_.texture = (float)clipminmaxi32(0, value, 1023) * (1.0f / 1023.0f);
      break;
    case PARAM_FREEZE:   // Encoder 4: freeze toggle
      params_.freeze = value > 0 ? 1 : 0;
      break;
    default:
      break;
  }
}

const char *CloudsEffect::getParameterStrValue(uint8_t id, int32_t value) const {
  // Strings must remain valid until the next call to this function.
  static char str_buf[24];

  switch (id) {
    case PARAM_SIZE: {  // mirror the log-size mapping used in setParameter()
      const float n = (float)clipminmaxi32(0, value, 1023) * (1.0f / 1023.0f);
      const float ms = std::exp(-2.3025851f + n * 9.2102403f); // e^-2.3 .. e^6.9
      char *p = str_buf;
      if (ms < 10.0f) {
        const int  ims = (int)(ms * 10.0f);
        p += render_uint(p, (uint32_t)(ims / 10));
        *p++ = '.';
        *p++ = (char)('0' + (ims % 10));
      } else {
        p += render_uint(p, (uint32_t)ms);
      }
      p[0] = 'm';
      p[1] = 's';
      p[2] = '\0';
      return str_buf;
    }
    default:
      // PITCH uses k_unit_param_type_semi: the device renders the raw
      // semitone value natively ("+7", "-3"), so no string is needed here.
      return nullptr;
  }
}

void CloudsEffect::touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) {
  (void)id;
  (void)x;
  (void)y;
  // X/Y coordinates are delivered through the genericfx parameter mappings
  // (PARAM_POSITION / PARAM_DENSITY); here we only react to the gesture.
  // On a fresh touch we could seed the scheduler; nothing required otherwise.
}

// ---------------------------------------------------------------------------
// Main DSP loop.
// ---------------------------------------------------------------------------
void CloudsEffect::process(const float *__restrict in, float *__restrict out,
                           uint32_t frames) {
  const float *__restrict in_p = in;
  float *__restrict out_p = out;
  const float *out_e = out_p + (frames << 1);  // stereo

  // --- cache control values for this block (K-rate) -------------------------
  size_samples_ = params_.size_ms * 0.001f * (float)kSampleRate;

  // pitch_semi/12 in [-1,1]; 2^p via exp(p*ln2). We avoid the fx_pow2f LUT:
  // the NTS-3 runtime does not export pow2_lut_f, so referencing it fails to
  // resolve at load time. expf comes from libm (statically linked).
  const float p = params_.pitch_semi * (1.0f / 12.0f);
  speed_ = std::exp(p * 0.6931471805599453f);   // 2^p, range 0.5 .. 2.0

  texture_  = params_.texture;
  dry_wet_  = params_.dry_wet;
  position_ = params_.position;
  freeze_   = params_.freeze != 0;

  // Density -> grains/second. |density| in [0,1]; square for a smooth taper,
  // scaled to a musically useful maximum spawn rate.
  const float dens = std::fabs(params_.density);
  constexpr float kMaxSpawn = 80.0f;                    // grains / second
  spawn_rate_ = dens * dens * kMaxSpawn;
  walk_dir_ = (params_.density < 0.0f) ? -1 : 1;

  // Reciprocal spawn interval in samples (0 when silent -> no spawning).
  const float spawn_inc = (spawn_rate_ > 1.0f)
      ? (spawn_rate_ * (1.0f / (float)kSampleRate))
      : 0.0f;

  // Expected overlap = spawn_rate * grain_duration. Each grain scales to
  // ~1/sqrt(1+overlap) so the summed cloud keeps a stable perceived level.
  const float overlap = spawn_rate_ * (size_samples_ * (1.0f / (float)kSampleRate));
  grain_amp_ = (spawn_rate_ > 0.0f)
      ? (1.0f / std::sqrt(1.0f + overlap))
      : 0.0f;

  for (; out_p != out_e; in_p += 2, out_p += 2) {
    const float in_l = in_p[0];
    const float in_r = in_p[1];

    // --- 1. record ----------------------------------------------------------
    if (!freeze_) {
      buffer_[write_pos_]             = in_l;
      buffer_[buffer_size_ + write_pos_] = in_r;
      write_pos_ = (write_pos_ + 1) & buffer_mask_;
    }

    // --- 2. update grain output --------------------------------------------
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    for (uint32_t i = 0; i < kMaxGrains; ++i) {
      Grain &g = grains_[i];
      if (!g.active)
        continue;

      // envelope weight
      const float env = envelope(g.elapsed, g.length);

      // interpolated buffer read (linear, power-of-two wrap)
      const uint32_t idx = ((uint32_t)g.read_pos) & buffer_mask_;
      const float frac = g.read_pos - (float)(uint32_t)g.read_pos;
      const uint32_t idx1 = (idx + 1) & buffer_mask_;
      const float s_l = buffer_[idx];
      const float s_r = buffer_[buffer_size_ + idx];
      const float read_l = s_l + (buffer_[idx1] - s_l) * frac;
      const float read_r = s_r + (buffer_[buffer_size_ + idx1] - s_r) * frac;

      wet_l += read_l * env * g.pan_l;
      wet_r += read_r * env * g.pan_r;

      // advance read head by speed (reverse if walk_dir_ < 0)
      g.read_pos += (walk_dir_ > 0) ? g.speed : -g.speed;

      // keep read_pos bounded to avoid float drift / wrap bookkeeping
      if (g.read_pos >= (float)buffer_size_)
        g.read_pos -= (float)buffer_size_;
      else if (g.read_pos < 0.0f)
        g.read_pos += (float)buffer_size_;

      ++g.elapsed;
      if (g.elapsed >= g.length) {
        g.active = false;
        --active_grains_;
      }
    }

    // --- 3. scheduler -------------------------------------------------------
    if (spawn_inc > 0.0f && active_grains_ < kMaxGrains) {
      spawn_acc_ += spawn_inc;
      // Poisson-ish: spawn when accumulator exceeds a random threshold.
      if (spawn_acc_ >= 1.0f) {
        const uint32_t r = next_rng();
        spawn_acc_ = ((r >> 16) & 0xFFFF) * (1.0f / 65536.0f); // reset to random
        spawn_grain();
      }
    }

    // --- 4. dry/wet mix (soft-clip the wet bus as a safety net) -------------
    const float dry = (1.0f - dry_wet_);
    out_p[0] = in_l * dry + fx_softclipf(0.3333333f, wet_l) * dry_wet_;
    out_p[1] = in_r * dry + fx_softclipf(0.3333333f, wet_r) * dry_wet_;
  }
}
