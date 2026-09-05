---
name: create-module
description: Scaffold and implement a new Korg NTS-3 logue-sdk genericfx unit from a spec file, following loader-safe conventions (8-slot headers, zero unfillable PLT symbols, verifiable build/wasm).
---

# Create NTS-3 Module From Spec

Turn `spec.md` (params, UI map, DSP intent) into a working genericfx unit under `platform/nts-3_kaoss/<name>/`.

## 1. Scaffold
Copy `granular/` as base (not the raw template — its Makefile wasm include is broken). Keep: `header.c`, `unit.cc`, `wasm.cc`, `Makefile` (already sysroot-fixed), `config.mk`. Rename `nts3_clouds.{h,cpp}` → `<name>.{h,cpp}`; set `PROJECT:=<name>`, `ULIBS=-lm`, `UCXXSRC=unit.cc <name>.cpp`, `UCSRC=header.c`.

## 2. Header (`header.c`)
- Set `.num_params` to the actual parameter count (fewer than 8 is fine — `UNIT_GENERICFX_MAX_PARAM_COUNT` is just the cap).
- Prefer native types over `strings` (strings rendered oddly on device): pitch → `k_unit_param_type_semi` range `{-12,12,0,0}`; toggle → `onoff {0,1}`; levels → `drywet`/`none`. No string callback for native types.
- `default_mappings`: linear/unipolar (arpeggiator-proven). X→assign_x, Y→assign_y, depth knob→assign_depth; encoders→assign_none with min/max matching the descriptor range.

## 3. Callbacks (`unit.cc`)
Pluck pattern only: no `<climits>`/`INT_MIN` guards (invalid sentinels break device param sync); getters return `cached_values[id]`. Keep `unit_init` validation (target, API, 48 kHz, 2ch, sdram_alloc) + buffer zero-fill.

## 4. DSP — loader-safe only (hard rules)
- The ONLY truly unbindable symbol is `_printf_float`: linking stdio (`snprintf`/`printf`) pulls it as a PLT JUMP_SLOT the loader can't resolve ("Resolve Symbol"). Fix: define it yourself, e.g. in `unit.cc`: `extern "C" int _printf_float(struct _reent *, const char *, ...) { return 0; }`. Never called for integer-only formats. Without the stub, omit stdio.
- `fx_pow2f`/`fx_sinf`/`fx_cosf` are SAFE: they use firmware-exported LUTs (`pow2_lut_f`, `wt_sine_lut_f`) that resolve at load (arpeggiator proves the pattern). `fx_pow2f` only covers `[0,3]`; mirror negatives with `1.0f / fx_pow2f(-p)`.
- libm (`std::exp/sqrt/sin/cos`) also works — links statically into the unit, resolves internally. Sizes smallest with `fx_*`.

## 5. Verify (mandatory)
1. `make clean && make` — no warnings.
2. `arm-none-eabi-readelf --dyn-syms build/<name>.elf | grep ' UND '` must contain ONLY: firmware exports (`wt_*`, `pow2_lut_f`, `midi_to_hz_lut_f`, `osc_rand`, ...) and weak `__sf_fake_*` data refs (auto_tune precedent). `_printf_float` must be DEFINED (a `GLOBAL DEFAULT` FUNC), never UND.
3. `make install` → `<name>.nts3unit`.
4. wasm: `make install` does not build it — run emcc manually (see granular shell history): `-s AUDIO_WORKLET=1 -s WASM_WORKERS=1 -s WASM_ASYNC_COMPILATION=0 -lembind --shell-file websim/xypad.html -O2 -g -fdebug-compilation-dir='..'` with `-I<name> -Icommon -I tools/emsdk/upstream/emscripten/cache/sysroot/include`, sources `wasm.cc header.c unit.cc <name>.cpp websim/dsp/*.c websim/dsp/*.cpp`. Expect `WASM_OK`.

## 6. Diagnosing loader errors
- "Resolve Symbol": read device log's PLT list; line `(lazy bind PLT stub) -> unresolvable!!` names the offender. If it's `_printf_float` → add the stub (rule 4); anything else → it's not a firmware export, remove/replace it.
