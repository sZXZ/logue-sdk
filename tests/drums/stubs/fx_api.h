/*
 *  File: fx_api.h  (host test stub)
 *
 *  Stand-ins for the firmware DSP helper / soft clip APIs plus the float math
 *  helpers the drum DSP relies on (fasterexpf, clip*). Used only by the
 *  host-side audio regression test (tests/drums).
 */
#pragma once

#include <cmath>

static inline float fasterexpf(float x)
{
  return expf(x);
}

static inline float clip01f(float x)
{
  return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

static inline float clipminmaxf(float mn, float x, float mx)
{
  return x < mn ? mn : (x > mx ? mx : x);
}

static inline int32_t clipminmaxi32(int32_t mn, int32_t x, int32_t mx)
{
  return x < mn ? mn : (x > mx ? mx : x);
}

static inline float fx_softclipf(float c, float x)
{
  if (x > 1.f)
    x = 1.f;
  else if (x < -1.f)
    x = -1.f;
  return x - c * x * x * x;
}