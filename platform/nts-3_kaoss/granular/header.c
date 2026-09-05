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
 *  File: header.c
 *
 *  NTS-3 kaoss pad kit generic effect unit header definition
 *  for the "Clouds" granular synthesizer.
 */

#include "unit_genericfx.h"

// ---- Unit header definition ------------------------------------------------

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),           // size of this header
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx, // NTS-3 genericfx
    .api = UNIT_API_VERSION,                                  // API version
    .dev_id = 0x735A585A,                                     // Developer ID
    .unit_id = 0x10U,                                         // unit ID
    .version = 0x00010000U,                                   // 1.0.0
    .name = "NTS3 Clouds",                                    // shown on device
    .num_params = 8,                                          // number of params

    .params = {
      // Format: min, max, center (unused), default, type, frac bits,
      //         frac mode, <reserved>, name

      // X axis: grain position in the recording buffer.
      {0, 1023, 0, 0,    k_unit_param_type_none, 0, 0, 0, {"POSITION"}},

      // Y axis: grain density (centre = silence, edges = dense).
      {0, 1023, 0, 512,  k_unit_param_type_none, 0, 0, 0, {"DENSITY"}},

      // FX depth knob: dry/wet balance.
      {0, 1023, 0, 1023, k_unit_param_type_drywet, 0, 0, 0, {"DEPTH"}},

      // Encoder 1: grain size in ms.
      {0, 1023, 0, 470,  k_unit_param_type_strings, 0, 0, 0, {"SIZE"}},

      // Encoder 2: pitch transpose (string display: "Unison"/"-2 st"...).
      {0, 1023, 0, 512,  k_unit_param_type_strings, 0, 0, 0, {"PITCH"}},

      // Encoder 3: envelope texture (square -> triangle -> Hann).
      {0, 1023, 0, 512,  k_unit_param_type_none, 0, 0, 0, {"TEXTURE"}},

      // Encoder 4: freeze (toggle: 0 = record, 1 = playback-only).
      {0, 1,    0, 0,    k_unit_param_type_onoff, 0, 0, 0, {"FREEZE"}},

      // Unused parameter slot (kept so the descriptor array is fully defined).
      {0, 0, 0, 0,       k_unit_param_type_none, 0, 0, 0, {""}},
    },
  },
  .default_mappings = {
    // Format: assign, curve, polarity, min, max, default
    {k_genericfx_param_assign_x,     k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},    // POSITION
    {k_genericfx_param_assign_y,     k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},  // DENSITY
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 1023}, // DEPTH
    {k_genericfx_param_assign_none,  k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 470},  // SIZE
    {k_genericfx_param_assign_none,  k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},  // PITCH
    {k_genericfx_param_assign_none,  k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},  // TEXTURE
    {k_genericfx_param_assign_none,  k_genericfx_curve_toggle, k_genericfx_curve_unipolar, 0, 1, 0},       // FREEZE
    {k_genericfx_param_assign_none,  k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},       // unused
  }
};