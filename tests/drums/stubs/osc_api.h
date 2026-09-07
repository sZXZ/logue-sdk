/*
 *  File: osc_api.h  (host test stub)
 *
 *  Stand-ins for the firmware oscillator / random APIs used by the drum DSP:
 *    - osc_sinf(phase)   -> sin(2*pi*x) as in the real firmware LUT version
 *    - osc_rand()        -> deterministic 32-bit LCG (matches loader-test needs)
 *  Used only by the host-side audio regression test (tests/drums).
 */
#pragma once

#include <cstdint>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_TWOPI
#define M_TWOPI (2.0 * M_PI)
#endif

static inline float osc_sinf(float x)
{
  return sinf(x * 2.0f * 3.14159265f) * 0.999f;
}

static inline uint32_t osc_rand()
{
  static uint32_t s = 0x12345678u;
  s = s * 1664525u + 1013904223u;
  return s;
}