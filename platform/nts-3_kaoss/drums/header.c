/*
 *  File: header.c
 *
 *  NTS-3 kaoss pad kit "DRUMS" drum machine unit header definition
 *
 */

#include "unit_genericfx.h"

// ---- Unit header definition -------------------------------------------------

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
    .api = UNIT_API_VERSION,
    .dev_id = 0x735A585A,
    .unit_id = 0x03U,
    .version = 0x00010000U,
    .name = "DRUMS",
    .num_params = 8,

    .params = {
      // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name

      // 1. PATTERN: seed that generates the drum sequence
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"PATTERN"}},

      // 2. DENSITY: Euclidean pulse count (1..16 active steps)
      {1, 16, 8, 8, k_unit_param_type_enum, 0, 0, 0, {"DENSITY"}},

      // 3. KICK: level & punch character (mapped to KAOSS X)
      {0, 1023, 0, 768, k_unit_param_type_none, 0, 0, 0, {"KICK"}},

      // 4. SNARE: level & noise snap
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"SNARE"}},

      // 5. HIHAT: level & closed (0..511) / open (512..1023) character (mapped to KAOSS Y)
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"HIHAT"}},

      // 6. DECAY: master envelope fall time (0..2000 ms)
      {0, 2000, 0, 400, k_unit_param_type_msec, 0, 0, 0, {"DECAY"}},

      // 7. TONE: overall brightness & snare body tuning
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"TONE"}},

      // 8. DRIVE: analog saturation on the drum bus (mapped to Depth)
      {0, 1023, 0, 256, k_unit_param_type_none, 0, 0, 0, {"DRIVE"}},
    },
  },
  .default_mappings = {
    // Format: assign, curve, curve polarity, min, max, default value

    // 1. PATTERN: menu / encoder
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},

    // 2. DENSITY: menu / encoder
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 1, 16, 8},

    // 3. KICK: mapped to X axis of the KAOSS pad
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 768},

    // 4. SNARE: menu / encoder
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},

    // 5. HIHAT: mapped to Y axis of the KAOSS pad
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 384},

    // 6. DECAY: menu / encoder
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 2000, 400},

    // 7. TONE: menu / encoder
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},

    // 8. DRIVE: mapped to depth knob
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 256},
  }
};
