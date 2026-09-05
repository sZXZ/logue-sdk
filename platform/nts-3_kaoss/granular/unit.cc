/*
    BSD 3-Clause License

    Copyright (c) 2026, KORG INC.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

/*
 *  File: unit.cc
 *
 *  NTS-3 kaoss pad kit generic effect unit interface
 *  for the "Clouds" granular synthesizer.
 */

#include "unit_genericfx.h"
#include "utils/int_math.h"
#include <algorithm>
#include <climits>

#include "nts3_clouds.h"

static CloudsEffect s_effect_instance;

// Cached parameter values passed from hardware.
static int32_t cached_values[UNIT_GENERICFX_MAX_PARAM_COUNT];

// ---- Callbacks exposed to runtime ----------------------------------------

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc)
    return k_unit_err_undef;

  // Correct platform/module target.
  if (desc->target != unit_header.common.target)
    return k_unit_err_target;

  // API compatibility.
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  // Sample rate must be 48kHz.
  if (desc->samplerate != (uint32_t)CloudsEffect::kSampleRate)
    return k_unit_err_samplerate;

  // Stereo in/out geometry.
  if (desc->input_channels != 2 || desc->output_channels != 2)
    return k_unit_err_geometry;

  // SDRAM buffer allocation must be available (granular buffer lives here).
  if (!desc->hooks.sdram_alloc)
    return k_unit_err_memory;

  {
    const uint32_t bytes =
        s_effect_instance.getBufferSize() * (uint32_t)sizeof(float);
    float *buf = (float *)desc->hooks.sdram_alloc(bytes);
    if (!buf)
      return k_unit_err_memory;

    // Clear the buffer (avoids clicks/blasts on first playback).
    std::fill(buf, buf + s_effect_instance.getBufferSize(), 0.0f);
    s_effect_instance.init(buf);
  }

  // Initialise cached parameter values to defaults from the header.
  for (uint32_t id = 0; id < UNIT_GENERICFX_MAX_PARAM_COUNT; ++id)
    cached_values[id] = unit_header.common.params[id].init;

  return k_unit_err_none;
}

__unit_callback void unit_teardown() {
  s_effect_instance.teardown();
}

__unit_callback void unit_reset() {
  s_effect_instance.reset();
}

__unit_callback void unit_resume() {}

__unit_callback void unit_suspend() {}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames) {
  s_effect_instance.process(in, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value) {
  if (id >= CloudsEffect::NUM_PARAMS)
    return;
  value = clipminmaxi32(unit_header.common.params[id].min, value,
                        unit_header.common.params[id].max);
  cached_values[id] = value;
  s_effect_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id) {
  if (id >= CloudsEffect::NUM_PARAMS)
    return INT_MIN;
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  if (id >= CloudsEffect::NUM_PARAMS)
    return nullptr;
  value = clipminmaxi32(unit_header.common.params[id].min, value,
                        unit_header.common.params[id].max);
  return s_effect_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_set_tempo(uint32_t tempo) {
  (void)tempo;
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter) {
  (void)counter;
}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x,
                                      uint32_t y) {
  s_effect_instance.touchEvent(id, phase, x, y);
}