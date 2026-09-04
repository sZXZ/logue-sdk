/*
 *  File: header.c
 *
 *  NTS-3 kaoss pad kit evolving arp unit header definition
 *
 */

#include "unit_genericfx.h"

const __unit_header genericfx_unit_header_t unit_header = {
    .common =
        {
            .header_size = sizeof(genericfx_unit_header_t),
            .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
            .api = UNIT_API_VERSION,
            .dev_id = 0x735A585A,
            .unit_id = 0x03U,
            .version = 0x00010000U,
            .name = "EvolvingArp",
            .num_params = 7,

            .params =
                {// 0: ROOT (X mapping) - root note of the chord
                 {0, 1023, 0, 0, k_unit_param_type_midi_note, 0, 0, 0, {"ROOT"}},

                 // 1: CHORD (Y mapping) - chord type
                 {0, 8, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"CHORD"}},

                 // 2: PATTERN - LFO range over chord notes
                 //    0 = LFO stays on root, 1023 = LFO sweeps full chord span
                 {0, 1023, 512, 512, k_unit_param_type_none, 0, 0, 0, {"PATTERN"}},

                 // 3: MODE - LFO pitch drift depth (0 = no drift, 1023 = +-12 semitones)
                 {0, 1023, 256, 256, k_unit_param_type_none, 0, 0, 0, {"MODE"}},

                 // 4: WAVE - LFO wave morph range
                 //    0 = fixed sine, 1023 = LFO sweeps Sine->Tri->Sq->Saw->Sine
                 {0, 1023, 512, 512, k_unit_param_type_none, 0, 0, 0, {"WAVE"}},

                 // 5: LEVEL - output level
                 {0, 1023, 512, 512, k_unit_param_type_none, 0, 0, 0, {"LEVEL"}},

                 // 6: LFO - master LFO speed (0 = off, 1023 = ~8 Hz)
                 {0, 1023, 200, 200, k_unit_param_type_none, 0, 0, 0, {"LFO"}}},
        },
    .default_mappings = {
        // ROOT mapped to X axis
        {k_genericfx_param_assign_x, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 1023, 0},

        // CHORD mapped to Y axis
        {k_genericfx_param_assign_y, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 8, 0},

        // PATTERN (LFO chord sweep range) - menu
        {k_genericfx_param_assign_none, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 1023, 512},

        // MODE (LFO pitch drift depth) - menu
        {k_genericfx_param_assign_none, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 1023, 256},

        // WAVE (LFO wave morph range) - menu
        {k_genericfx_param_assign_none, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 1023, 512},

        // LEVEL mapped to DEPTH control
        {k_genericfx_param_assign_depth, k_genericfx_curve_exp,
         k_genericfx_curve_unipolar, 0, 1023, 512},

        // LFO master speed - menu
        {k_genericfx_param_assign_none, k_genericfx_curve_linear,
         k_genericfx_curve_unipolar, 0, 1023, 200}}};
