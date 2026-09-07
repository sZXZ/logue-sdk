/*
 *  File: processor.h  (host test stub)
 *
 *  Minimal stand-in for platform/nts-3_kaoss/common/processor.h used only by
 *  the host-side audio regression test (tests/drums). Real hardware builds use
 *  the actual SDK header.
 */
#pragma once

#include <cstdint>

class Processor
{
public:
  virtual ~Processor() {}

  virtual uint32_t getBufferSize() const = 0;
  virtual void init(float *buffer) = 0;
  virtual void teardown() = 0;
  virtual void reset() = 0;
  virtual void resume() {}
  virtual void suspend() {}
  virtual void process(const float *in, float *out, uint32_t frames) = 0;
  virtual void setParameter(uint8_t index, int32_t value) = 0;

  virtual const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  virtual void setTempo(float bpm) { (void)bpm; }

  virtual void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
  {
    (void)id;
    (void)phase;
    (void)x;
    (void)y;
  }
};