### The 8-Parameter "Acid" Spec

To nail the 303 sound, we've swapped out the Overdrive (since you can stack it with NTS-3's other effects) for **Glide (Slide)**, which is absolutely essential for those slurred, liquid acid basslines.

| # | Parameter | Range | Description |
| :--- | :--- | :--- | :--- |
| **1** | **Waveform** | `0` (Saw), `1` (Square) | The raw oscillator shape. Saw for classic bite, Square for a hollow bounce. |
| **2** | **Tuning (Base)** | `0 - 127` (MIDI) | Sets the base pitch of the oscillator. |
| **3** | **Cutoff** | `0.0 - 1.0` | Base frequency of the low-pass filter. (KAOSS X-axis offsets this). |
| **4** | **Resonance** | `0.0 - 1.0` | Pushes the filter into self-oscillation for the classic squelch. (KAOSS Y-axis offsets this). |
| **5** | **Env Mod** | `0.0 - 1.0` | Determines how much the envelope pushes the filter cutoff upward. |
| **6** | **Decay** | `0.0 - 1.0` | The fall time of the envelope. Short for plucks, long for sustained wails. |
| **7** | **Accent** | `0.0 - 1.0` | Simulates the 303 accent (increases volume, shortens decay, boosts resonance/env mod). |
| **8** | **Glide (Slide)** | `0.0 - 1.0` | Portamento time. Smooths the pitch transition between consecutive notes. |

---

### The AI Agent Prompt

Copy and paste the text below into your AI coding assistant.

```text
Act as an expert DSP C++ audio developer familiar with the Korg logue-sdk. I need you to write a custom synthesizer/effect plugin for the Korg NTS-3 KAOSS pad. 

Target SDK Reference: [https://github.com/korginc/logue-sdk/tree/main/platform/nts-3_kaoss](https://github.com/korginc/logue-sdk/tree/main/platform/nts-3_kaoss)

Project: "NTS-303" - A TB-303 style Acid generator and filter.
Since the NTS-3 is an effects unit, this plugin will ignore audio input and instead act as a self-contained acid synth that outputs audio into the NTS-3 buffers.

Please implement the DSP C++ code (`main.cpp` and `header` if necessary) with the following specifications:

1. CORE DSP COMPONENTS:
- Oscillator: Anti-aliased (PolyBLEP) oscillator supporting Sawtooth and Pulse/Square waves.
- Glide (Portamento): A one-pole low-pass filter on the pitch frequency to allow notes to slide into each other.
- Filter: A 24dB or 18dB resonant Low-Pass filter model (preferably a diode-ladder approximation) capable of high resonance "squelch" without blowing up.
- Envelope: A simple Decay envelope mapped to both the VCA (volume) and the VCF (filter cutoff). 

2. PARAMETERS (Exactly 8, using standard logue-sdk parameter mapping):
- Param 1: Waveform (0 = Saw, 1 = Square)
- Param 2: Base Note (MIDI note 24 to 84)
- Param 3: Cutoff Base (0.0 to 1.0)
- Param 4: Resonance Base (0.0 to 1.0)
- Param 5: Envelope Mod Depth (0.0 to 1.0)
- Param 6: Decay Time (10ms to 2000ms)
- Param 7: Accent Level (0.0 to 1.0) - should dynamically scale volume and Env Mod.
- Param 8: Glide Time (0.0 to 1.0) - controls the slide time between pitch changes.

3. NTS-3 KAOSS PAD MAPPING:
- Use the `x` and `y` inputs provided in the NTS-3 `FX_PROCESS` context.
- X-Axis (`x`): Should act as a positive/negative offset to the Base Cutoff.
- Y-Axis (`y`): Should act as a positive/negative offset to the Resonance AND trigger the envelope when pressed (gate on when touch is detected, gate off/decay when released). If the pad is held and X/Y changes, slide the pitch based on the Glide parameter.

4. REQUIREMENTS:
- Use standard Logue SDK NTS-3 structs and signatures (`FX_INIT`, `FX_PROCESS`, etc.).
- Ensure all DSP processing is done per-sample inside the frame loop.
- Use efficient math (avoid heavy `pow` or `exp` in the audio rate loop if possible, use lookup tables or fast approximations provided by the SDK).
- Provide the complete code for `nts303.cpp`.

Please output only the code and brief instructions on how to compile it via the Logue SDK makefile.