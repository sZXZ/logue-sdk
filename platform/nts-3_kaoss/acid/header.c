/*
 *  File: header.c
 *
 *  NTS-3 kaoss pad kit "NTS-303" acid generator unit header definition
 *
 */

#include "unit_genericfx.h"   // Note: Include base definitions for genericfx units

// ---- Unit header definition  --------------------------------------------------------------------

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),           // Size of this header. Leave as is.
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx, // Target platform and module pair for this unit
    .api = UNIT_API_VERSION,                                  // API version for which unit was built. See runtime.h
    .dev_id = 0x735A585A,
    .unit_id = 0x02U,
    .version = 0x00010000U,
#ifdef AUTODRIFT
    .name = "ACID Bass Evo",                              // Evo variant: evolving pattern
#else
    .name = "ACID Bass",                                  // Name for this unit, will be displayed on device
#endif
    .num_params = 8,                                      // Number of valid parameter descriptors. (max. 8)

    .params = {
      // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name

      // WAVE: 0 = Saw, 1023 = Square
      {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"WAVE"}},

      // ROOT: bass transposition, displayed as musical pitch (C0..G9)
      {0, 1023, 0, 237, k_unit_param_type_midi_note, 0, 0, 0, {"ROOT"}},

      // PATTERN: seed that generates the pitch/accent/slide sequence
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"PATTERN"}},

      // DENSITY: Euclidean pulse count (1..16 active steps)
      {1, 16, 8, 12, k_unit_param_type_enum, 0, 0, 0, {"DENSITY"}},

      // CUTOFF: filter base frequency
      {0, 1023, 0, 256, k_unit_param_type_none, 0, 0, 0, {"CUTOFF"}},

      // RESONANCE: filter emphasis
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"RESON"}},

      // DECAY: envelope fall time, 10ms .. 2000ms
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"DECAY"}},

      // ACID: macro scaling glide time + accent intensity
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"ACID"}},
    },
  },
  .default_mappings = {
    // By default, the parameters described above will be mapped to controls as described below.
    // These assignments can be overriden by the user.

    // Format: assign, curve, curve polarity, min, max, default value

    // WAVE, ROOT, PATTERN, DENSITY, DECAY, ACID not mapped to pad (edited via menu)
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 198, 442, 237},
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 1, 16, 12},

    // CUTOFF mapped to Y axis of the control pad
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 256},

    // RESONANCE mapped to X axis of the control pad
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 400, 1023, 384},

    {k_genericfx_param_assign_x, k_genericfx_curve_exp, k_genericfx_curve_unipolar, 0, 2000, 500},
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 384}
  }
};