# Building NTS-3 Kaoss Pad Plugins

> [!IMPORTANT]
> **Do not run `make` commands from the root directory.** The root directory does not contain a `Makefile`. You must first navigate into a specific project subdirectory (e.g., `cd dummy-genericfx`) before running any `make` commands.

This guide explains how to build your custom effects (such as `dummy-genericfx`) for both the hardware unit and the WebAssembly browser debug simulator.

---

## 🛠️ Building for the Hardware

To compile your code into the `.nts3unit` format that can be loaded onto the physical NTS-3 Kaoss Pad:

1. **Navigate to the plugin directory (CRITICAL):**
   ```bash
   cd dummy-genericfx
   ```

2. **Clean previous builds (Optional but recommended):**
   ```bash
   make clean
   ```

3. **Compile the plugin:**
   ```bash
   make
   ```

4. **Package and export the plugin:**
   ```bash
   make install
   ```
   *This creates the `dummy_genericfx.nts3unit` file in the plugin directory. You can load this file onto the device using the NTS-3 Librarian.*

---

## 🌐 Building for the WebAssembly Simulator

To compile and test the plugin in a browser-based simulator without needing the physical hardware:

1. **Navigate to the plugin directory (CRITICAL):**
   ```bash
   cd dummy-genericfx
   ```

2. **Compile to WebAssembly and start the sandbox server:**
   ```bash
   make wasm
   ```
   *This command will:*
   * Compile the DSP and wrapper code to WebAssembly using the Emscripten Compiler (`emcc`).
   * Copy the required simulator web assets (styles, scripts, images) into a local `sim/` directory.
   * Start a local web server (`emrun`) listening on a local port (e.g., `http://localhost:6931/`).
   * Attempt to automatically open the interactive XY-pad simulation page in Google Chrome.

3. **Interacting with the Simulator:**
   * If Chrome does not open automatically, look at the terminal output for the local URL (e.g., `http://localhost:6931/dummy_genericfx.html` or similar) and open it manually in Chrome.
   * Use the on-screen controls to test your DSP code in real-time.

---

## 🔍 Troubleshooting

### Error: `OSError: [Errno 48] Address already in use`
This error indicates that a previous simulator server process (`emrun`) is still running in the background and keeping port `6931` occupied.

To resolve this, find and terminate the active process:
1. Find the Process ID (PID) using the port:
   ```bash
   lsof -i :6931
   ```
2. Kill the process (replace `<PID>` with the actual PID returned from `lsof`):
   ```bash
   kill <PID>
   ```
   *(Alternatively, you can run `killall Python` if you have no other Python servers active.)*
