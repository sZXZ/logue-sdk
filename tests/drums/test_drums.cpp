/*
 *  File: test_drums.cpp
 *
 *  Host-side audio regression test for the NTS-3 "DRUMS" generic FX unit
 *  (platform/nts-3_kaoss/drums).
 *
 *  Compiles the real module DSP (effect.h) against stub firmware headers and
 *  renders audio off-device, so we can measure objectively:
 *
 *    - "flat-top" saturation count  -> catches the hard-clipping (buzzing /
 *      clicky) failure mode that turns drum hits into flat square blocks.
 *    - per-voice loudness (kick / snare / hat) via single-voice render runs,
 *      so the kick can be kept punchy rather than a "gentle tap".
 *    - overall RMS / peak level, so the mix stays healthy (not tiny).
 *    - saturation still happening at DRIVE=1023 (intended per spec).
 *
 *  Build (from the repo root):  ./tests/drums/build.sh
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Stub firmware headers first (via -I tests/drums/stubs).
#include "processor.h"
#include "unit_genericfx.h"
#include "fx_api.h"
#include "osc_api.h"

// The module DSP, unmodified. Its includes resolve to the stubs above because
// the module directory has no processor.h / fx_api.h / etc.
#include "effect.h"

namespace
{
constexpr int k_test_sr = 48000;
constexpr int k_seconds = 2;
constexpr int k_total = k_test_sr * k_seconds;

struct Stats
{
  float max_out;
  float rms;
  float max_step;
  int flats;      // samples pinned to the softclip ceiling (flat-topped)
  int flat_run;   // longest consecutive run of ceiling-pinned samples
  float low_peak; // peak of ~100 Hz low-passed signal (kick body energy)
};

// One-pole low-pass at ~110 Hz for extracting the kick body band.
inline float lowpass_k(float hz)
{
  return 1.f - expf(-6.28318f * hz / (float)k_test_sr);
}

void render(Effect &fx, float *out)
{
  std::memset(out, 0, sizeof(float) * k_total * 2);
  fx.touchEvent(0, 1 /* began */, 0, 0);
  const int chunk = 480;
  for (int f = 0; f < k_total; f += chunk)
  {
    const int n = std::min(chunk, k_total - f);
    static float in[k_total * 2];
    fx.process(in + f * 2, out + f * 2, n);
  }
}

Stats analyze(const float *out, float drive_norm, float softclip_c)
{
  Stats s;
  s.max_out = 0.f;
  s.rms = 0.f;
  s.max_step = 0.f;
  s.flats = 0;
  s.flat_run = 0;
  s.low_peak = 0.f;

  const float ceiling = (1.f - softclip_c) / (1.f + 0.6f * drive_norm);
  const float k_lp = lowpass_k(110.f);
  float lp = 0.f;
  int run = 0;

  for (int i = 0; i < k_total * 2; i += 2)
  {
    const float v = out[i];
    if (std::fabs(v) > s.max_out)
      s.max_out = std::fabs(v);
    s.rms += v * v;
    if (i >= 2)
    {
      const float step = std::fabs(v - out[i - 2]);
      if (step > s.max_step)
        s.max_step = step;
    }
    if (std::fabs(v) - (ceiling - 1e-4f) >= 0.f)
    {
      s.flats++;
      if (++run > s.flat_run)
        s.flat_run = run;
    }
    else
    {
      run = 0;
    }
    lp += k_lp * (v - lp);
    if (std::fabs(lp) > s.low_peak)
      s.low_peak = std::fabs(lp);
  }
  s.rms = std::sqrt(s.rms / k_total);
  return s;
}
} // namespace

int main(int argc, char **argv)
{
  const bool verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

  struct RunSpec
  {
    const char *label;
    int kick, snare, hihat, decay, tone, drive;
  };

  const RunSpec runs[] = {
      {"kick-only",   1023, 0,   0,   400, 512, 256},
      {"snare-only",  0,    1023, 0,   400, 512, 256},
      {"hat-only",    0,    0,    1023, 400, 512, 256},
      {"default",     768,  512,  384, 400, 512, 256},
      {"maxed",       1023, 1023, 1023, 2000, 1023, 1023},
  };

  float out[k_total * 2];
  Stats st[5];
  for (int r = 0; r < 5; ++r)
  {
    Effect fx;
    fx.init(nullptr);
    fx.setParameter(0, 512); // PATTERN
    fx.setParameter(1, 8);   // DENSITY
    fx.setParameter(2, runs[r].kick);
    fx.setParameter(3, runs[r].snare);
    fx.setParameter(4, runs[r].hihat);
    fx.setParameter(5, runs[r].decay);
    fx.setParameter(6, runs[r].tone);
    fx.setParameter(7, runs[r].drive);
    fx.setTempo(120.f);

    render(fx, out);
    st[r] = analyze(out, runs[r].drive / 1023.f, 0.25f);

    if (verbose && r == 3) // print first 50ms of default run
    {
      std::printf("--- default waveform (L, every 2ms) ---\n");
      for (int ms = 0; ms < 60; ms += 2)
        std::printf("%3dms: %+.5f\n", ms, out[ms * k_test_sr / 1000 * 2]);
    }

    std::printf("%-12s max=%.3f rms=%.3f step=%.3f flats=%d(/%d) low_peak=%.3f\n",
                runs[r].label, st[r].max_out, st[r].rms, st[r].max_step,
                st[r].flats, st[r].flat_run, st[r].low_peak);
  }

  // --- DECAY sweep: kick-only, density=1 (one hit per bar). The note length
  // (time the kick stays above -40 dB of its own peak) must clearly grow with
  // DECAY. This is the regression for "decay doesn't do anything".
  // density=1 activates only step 0, and the sequencer first fires step 0 after
  // a full bar (~2s), so we render 4s and measure the first hit's tail within
  // the clean gap before the bar's second hit (~4s).
  const int k_decay_total = k_test_sr * 4;
  std::vector<float> decay_out(k_decay_total * 2);
  float tails[3];
  const int dvals[] = {0, 400, 2000};
  for (int k = 0; k < 3; ++k)
  {
    Effect fx;
    fx.init(nullptr);
    fx.setParameter(0, 512);
    fx.setParameter(1, 1); // one hit per bar -> clean isolated hit
    fx.setParameter(2, 1023);
    fx.setParameter(3, 0);
    fx.setParameter(4, 0);
    fx.setParameter(5, dvals[k]);
    fx.setParameter(6, 512);
    fx.setParameter(7, 256);
    fx.setTempo(120.f);
    std::memset(decay_out.data(), 0, sizeof(float) * k_decay_total * 2);
    fx.touchEvent(0, 1 /* began */, 0, 0);
    const int chunk = 480;
    for (int f = 0; f < k_decay_total; f += chunk)
    {
      const int c = std::min(chunk, k_decay_total - f);
      static float in[k_decay_total * 2];
      fx.process(in + f * 2, decay_out.data() + f * 2, c);
    }

    float peak = 0.f;
    for (int i = 0; i < k_decay_total * 2; i += 2)
    {
      const float a = std::fabs(decay_out[i]);
      if (a > peak)
        peak = a;
    }

    // Separate hits with the strong threshold (attack zones), then measure how
    // long the first hit's output stays above -40 dB of its own peak. The hit
    // region ends where amplitude stays below 0.5*peak for >= 50 ms (a clean
    // gap before the next hit), so pre-hit attack ramps can't corrupt it.
    int onset1 = -1;
    for (int i = 0; i < k_decay_total * 2; i += 2)
    {
      if (std::fabs(decay_out[i]) > 0.5f * peak)
      {
        onset1 = i / 2;
        break;
      }
    }

    int region_end = -1, below = 0;
    for (int i = onset1 * 2; i < k_decay_total * 2; i += 2)
    {
      if (std::fabs(decay_out[i]) > 0.5f * peak)
        below = 0;
      else
        below += 2;
      if (below >= (k_test_sr / 20) * 2)
      {
        region_end = (i - below) / 2;
        break;
      }
    }

    int last = -1;
    for (int i = onset1 * 2; i < region_end * 2; i += 2)
    {
      if (std::fabs(decay_out[i]) > 0.01f * peak)
        last = i / 2;
    }
    tails[k] = (region_end > 0 && last > onset1) ? (float)(last - onset1) / k_test_sr : -1.f;
    std::printf("decay=%4dms kick tail=%5.2fs\n", dvals[k], tails[k]);
  }

  // --- Regression assertions ------------------------------------------------
  int failures = 0;
#define CHECK(cond, msg) \
  do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else std::printf("ok:   %s\n", msg); \
  } while (0)

  // Default settings must NOT flat-top (that was the "clicky/buzzing" bug),
  // and must stay punchy (not tiny / a gentle tap).
  CHECK(st[3].flats < 10, "default does not hit the softclip ceiling");
  CHECK(st[3].rms > 0.05f, "default mix RMS is audible (not tiny)");
  CHECK(st[3].max_out > 0.35f, "default mix peaks healthy");
  CHECK(st[3].low_peak > 0.10f, "default has low-end kick body energy");
  CHECK(st[0].max_out > 0.45f, "kick is punchy, not a tap");
  CHECK(st[0].low_peak > 0.30f, "kick carries sub/low body");

  // DRIVE at maximum must still saturate (intended per spec).
  CHECK(st[4].flats > 100, "DRIVE=1023 heavily saturates the bus (spec)");

  // Voice isolation sanity: snare/hat must not dominate a knockout kick.
  CHECK(st[1].max_out < st[0].max_out, "snare sits below kick level");
  CHECK(st[2].max_out < st[0].max_out, "hat sits below kick level");

  // DECAY must audibly change the drum note length.
  CHECK(tails[0] > 0.f, "decay=0 kick tail is finite");
  CHECK(tails[1] > tails[0] * 1.5f, "DECAY clearly lengthens the note (short->mid)");
  CHECK(tails[2] > tails[1] * 1.5f, "DECAY clearly lengthens the note (mid->long)");

#undef CHECK

  std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
  return failures == 0 ? 0 : 1;
}