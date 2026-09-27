/*
 *  File: header.c
 *
 *  NTS-3 kaoss pad kit "WT" wavetable oscillator unit header definition
 *
 *  The unit name and id come from the baked wavetable (wt_data.h is generated
 *  per wav by tools/wav2table.py, and the Makefile builds one variant per wav
 *  in wt/), so every wav in the folder ends up as its own .nts3unit.
 */

#include "unit_genericfx.h" // Note: Include base definitions for genericfx units
#include "wt_data.h"         // Generated: WT_DISPLAY_NAME, WT_UNIT_ID

// ---- Unit header definition  --------------------------------------------------------------------

const __unit_header genericfx_unit_header_t unit_header = {
  .common = {
    .header_size = sizeof(genericfx_unit_header_t),          // Size of this header. Leave as is.
    .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx, // Target platform and module pair for this unit
    .api = UNIT_API_VERSION,                                 // API version for which unit was built. See runtime.h
    .dev_id = 0x735A585A,
    .unit_id = WT_UNIT_ID,                                   // Scoped within dev_id, one per wav
    .version = 0x00010000U,
    .name = WT_DISPLAY_NAME,                                 // Name shown on device, e.g. "WT BASS"
    .num_params = 8,                                         // Number of valid parameter descriptors. (max. 8)

    .params = {
      // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name

      // PITCH: base pitch, displayed as a musical pitch (the DSP scales 0..1023 to MIDI 0..127)
      {0, 1023, 0, 360, k_unit_param_type_midi_note, 0, 0, 0, {"PITCH"}},

      // POSITION: wavetable position, 0 = darkest level the pitch allows,
      // 1023 = the full baked cycle
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"POSITION"}},

      // LFO RATE: free running LFO, exponential 0.05 Hz .. 20 Hz
      {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"LFO RATE"}},

      // LFO DEPTH: how far the LFO sweeps POSITION
      {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"LFO DEPTH"}},

      // ADSR: envelope morph, the arpeggiator unit's preset chain
      // (Pluck / Pad / Perc / Swell / Long release). 820 and above is DRONE:
      // full sustain, no envelope, sounds without touch.
      {0, 1023, 0, 256, k_unit_param_type_none, 0, 0, 0, {"ADSR"}},

      // PATTERN: seed for the generative 16 step line (tempo synced).
      // 0 = off, the unit is a plain touch played oscillator.
      {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"PATTERN"}},

      // SUB: sub oscillator (square, one octave down) level
      {0, 1023, 0, 384, k_unit_param_type_none, 0, 0, 0, {"SUB"}},

      // DETUNE: unison spread in cents
      {-1200, 1200, 0, 8, k_unit_param_type_cents, 0, 0, 0, {"DETUNE"}},
    },
  },
  .default_mappings = {
    // By default, the parameters described above will be mapped to controls as described below.
    // These assignments can be overriden by the user.

    // Format: assign, curve, curve polarity, min, max, default value

    // PITCH, LFO RATE, ADSR, SUB and DETUNE are not mapped to the pad
    // (edited via the menu)
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 360},

#ifdef WTPAD_PITCH
    // Build option -DWTPAD_PITCH: X plays the wavetable across the pad
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 360},
#else
    // POSITION mapped to the X axis of the control pad: sweeping the pad
    // across X sweeps the wavetable
    {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
#endif

    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},

#ifdef WTPAD_PITCH
    // ... and Y sweeps its position instead
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
#else
    // LFO DEPTH mapped to the Y axis
    {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},
#endif

    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 256},

    // PATTERN mapped to the DEPTH knob: turning it writes a new pattern
    {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},

    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 384},
    {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, -1200, 1200, 8},
  }
};
