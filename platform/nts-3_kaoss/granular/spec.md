# Product Specification: NTS-3 "Clouds" Granular Effect

## 1. Overview
This project aims to port the core granular synthesis algorithms of Mutable Instruments' "Clouds" module to the KORG NTS-3 kaoss pad via the `logue-sdk` custom effect API. The goal is to provide real-time granular audio processing, allowing users to freeze, fragment, and manipulate live incoming audio or internal synthesizer sounds using the NTS-3's X/Y touchpad and encoders.

This port will focus purely on the audio processing engine. Modular-specific features like CV control over individual parameters, firmware update via audio, and save/load state functionality will be omitted to fit the NTS-3 architecture.

## 2. Target Platform
*   **Hardware:** KORG NTS-3 kaoss pad
*   **Software API:** `logue-sdk` (specifically the `platform/nts-3_kaoss` environment).

## 3. Core Capabilities (The "Clouds" Engine)
The effect will continuously record incoming stereo audio into a short circular buffer. It will synthesize a sonic texture by playing back short, overlapping segments ("grains") extracted from this buffer.

### 3.1 Essential Features
*   **Continuous Sampling:** Constantly writes audio input to the buffer.
*   **Granular Playback:** Generates overlapping grains from the buffer.
*   **Freeze:** Stops writing to the buffer, allowing continuous granularization of the captured audio tail.
*   **Grain Parameters:** Control over position (where in the buffer), size (length of grain), pitch (playback speed of grain), and density (how many grains/overlap).
*   **Texture/Envelope:** Control over the shape of the grain envelope and diffusion (smearing).

## 4. User Interface & Mapping (NTS-3)

The NTS-3 provides an X/Y pad, depth control, and parameter encoders. We must map Clouds' extensive control set to this interface.

### 4.1 X/Y Pad Mapping (Primary Performance Controls)
The Kaoss pad is ideal for sweeping through the buffer and controlling grain density.

*   **X-Axis:** `Grain Position` (Clouds parameter D). 
    *   Left = Current input (0 delay).
    *   Right = Furthest back in time in the buffer.
*   **Y-Axis:** `Grain Density` (Clouds parameter H).
    *   Bottom = Constant, sparse grain generation.
    *   Middle = Zero grains (silence).
    *   Top = High overlap, randomly spaced grains.

### 4.2 Parameter Encoders (Secondary/Setup Controls)
The NTS-3 allows defining custom parameters mapped to its encoders.

*   **Param 1:** `Grain Size` (Clouds param E). Sets the length of the grains.
*   **Param 2:** `Pitch/Transposition` (Clouds param F). 
    *   Values should allow for musical intervals (e.g., -1 octave, unison, +1 octave, +1 fifth). Unison should be exactly center.
*   **Param 3:** `Texture` (Clouds param I). Morphs the grain envelope (square -> triangle -> Hann) and introduces diffusion at higher values.
*   **Param 4:** `Reverb/Diffusion Amount` (Clouds BLEND function - Reverb). Adds the characteristic Clouds spatial wash.

### 4.3 Depth Control & Buttons
*   **FX Depth:** Maps to `Dry/Wet Balance` (Clouds BLEND function - Dry/Wet).
*   **FX Hold/Freeze Button:** Dedicate Param 5 as a 0/1 toggle for Freeze.

## 5. Technical Constraints & Omissions

### 5.1 Omitted Clouds Features
*   **CV Inputs:** N/A for NTS-3.
*   **Audio Quality Settings (8-bit, etc.):** To save memory and CPU, stick to a single standard 16-bit/24-bit 48kHz processing rate natively supported by the logue-sdk.
*   **Load/Save Buffers:** Excluded.
*   **Feedback/Stereo Spread (Blend Params):** Omitted to keep the encoder count manageable, or hardcoded to sensible default values.
*   **Trigger Input:** Omitted. Grain generation will be continuous based on the Density parameter.

### 5.2 Memory & CPU (logue-sdk specific)
*   **Buffer Size:** Clouds originally supported up to 8s at low quality. On the NTS-3, RAM is limited. The buffer size must be dynamically allocated based on available external RAM (SDRAM) specified in the `logue-sdk` for NTS-3. A realistic target is 1-2 seconds of high-quality stereo audio.
*   **Polyphony:** Clouds supported 40-60 grains. The ARM Cortex-M4/M7 in the logue synths is powerful, but we must optimize the grain scheduler and interpolation (using CMSIS-DSP if possible) to maintain frame rates without dropping audio. Start with a max of 20-30 grains.

## 6. Development Phases

1.  **Skeleton Setup:** Create standard `logue-sdk` NTS-3 project structure (header, main cpp, build scripts).
2.  **Buffer Management:** Implement the circular recording buffer using SDRAM allocation routines provided by the SDK.
3.  **Grain Scheduler:** Port the logic for spawning grains based on the Density parameter.
4.  **Audio Processing Core:** Implement the grain reading, pitch interpolation (read speed), and envelope application (Hann window).
5.  **Parameter Mapping:** Connect the NTS-3 X/Y coordinates and parameter values to the internal engine variables.
6.  **Optimization:** Profile CPU usage and adjust max grain count or interpolation quality as needed.
