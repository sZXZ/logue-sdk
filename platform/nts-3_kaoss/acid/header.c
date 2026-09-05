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
    .dev_id = 0x0,                                            // Developer ID. See https://github.com/korginc/logue-sdk/blob/master/developer_ids.md
    .unit_id = 0x0U,                                          // ID for this unit. Scoped within the context of a given dev_id.
    .version = 0x00010000U,                                   // This unit's version: major.minor.patch (major<<16 minor<<8 patch).
    .name = "ACID303",                                        // Name for this unit, will be displayed on device
    .num_params = 8,                                          // Number of valid parameter descriptors. (max. 8)

    .params = {
      // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name

      // WAVE: 0 = Saw, 1 = Square
      {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"WAVE"}},

      // ROOT: pattern root note, displayed as musical pitch (C0..G9)
      {24, 84, 54, 45, k_unit_param_type_midi_note, 0, 0, 0, {"ROOT"}},

      // CUTOFF: filter base frequency
      {0, 1023, 0, 256, k_unit_param_type_none, 0, 0, 0, {"CUTOFF"}},

      // RESONANCE: filter emphasis
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"RESON"}},

      // ENV MOD: how much the envelope pushes the cutoff
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"ENV MOD"}},

      // DECAY: envelope fall time, 10ms .. 2000ms
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"DECAY"}},

      // ACCENT: boosts volume and env mod on accented steps
      {0, 1023, 0, 256, k_unit_param_type_none, 0, 0, 0, {"ACCENT"}},

      // GLIDE: slide time, smooths pitch transitions between notes
      {0, 1023, 0, 192, k_unit_param_type_none, 0, 0, 0, {"GLIDE"}},
    },
  },
  .default_mappings = {
    // By default, the parameters described above will be mapped to controls as described below.
    // These assignments can be overriden by the user.

    // Format: assign, curve, curve polarity, min, max, default value

    // WAVE, ROOT, ENV MOD, DECAY, ACCENT, GLIDE not mapped to pad (edited via menu)
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1, 0},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 24, 84, 45},

    // CUTOFF mapped to X axis of the control pad
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 256},

    // RESONANCE mapped to Y axis of the control pad
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 384},

    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 384},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 256},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 192}
  }
};