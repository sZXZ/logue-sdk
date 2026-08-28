---
name: nts3-param-editing
description: Guide and patterns for adding, modifying, or merging parameters in Korg NTS-3 kaoss pad logue-SDK units across headers, DSP logic, and web simulators.
---

# Editing Parameters in NTS-3 logue-SDK Units

## 1. Header Definitions & Sync
Keep `header.c` (hardware) and `sim/header.c` (WASM simulator) identical:
* **Param Count**: Set `.num_params` in `unit_header.common` (max `UNIT_MAX_PARAM_COUNT` = 8).
* **Parameter Descriptors (`params[]`)**:
  `{min, max, center, init, type, frac, frac_mode, reserved, {"NAME"}}`
* **Parameter Types (`runtime.h` `k_unit_param_type_*`)**:
  - `none`: Typeless / raw numerical slider.
  - `strings`: Custom text lookup via `unit_get_param_str_value()`.
  - `enum`: Numerical enum (displays `value + 1` if min is 0).
  - Units (`percent`, `db`, `cents`, `semi`, `oct`, `hertz`, `khertz`, `bpm`, `msec`, `sec`).
  - Controls (`drywet` D/BAL/W, `pan` L/C/R, `spread` </>, `onoff` OFF/ON, `midi_note` C0-G9).
* **Controller Assignments (`default_mappings[]`)**:
  - Assign to `k_genericfx_param_assign_x`, `_y`, `_depth`, or `_none`.

## 2. DSP Implementation (`effect.h`)
* **Index Enum**: `enum { PARAM_0 = 0U, ..., NUM_PARAMS };` matching header order.
* **Storage Types**: Use `uint16_t` for `0–1023` parameter values to prevent `uint8_t` overflow.
* **Accessors**:
  ```cpp
  inline void setParameter(uint8_t index, int32_t value) {
    switch (index) {
    case PARAM_ID:
      params_.val = (uint16_t)std::min(value, (int32_t)1023); // Cast literal for C++11 std::min match
      break;
    }
  }

  inline int32_t getParameterValue(uint8_t index) const {
    switch (index) {
    case PARAM_ID: return (int32_t)params_.val;
    }
    return 0;
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const {
    switch (index) {
    case ENUM_PARAM_ID: {
      static const char *names[] = {"OptionA", "OptionB"};
      if (value >= 0 && value < 2) return names[value];
      break;
    }
    }
    return nullptr; // Sliders return nullptr
  }
  ```

## 3. General Patterns
* **Continuous Morphs (0–1023)**: Divide 0–1023 into equal segments (`t = (v - offset) / seg_len`) and linearly interpolate (`a + t * (b - a)`) between adjacent targets.
* **Parameter Packing**: Merge sub-options into a single menu (`value = optionA * countB + optionB`) to save parameter slots. Decode with `valA = value / countB`, `valB = value % countB`.
* **Legato / Clickless Retrigger**: When retriggering while active, transition smoothly from current amplitude/state rather than resetting to zero.
