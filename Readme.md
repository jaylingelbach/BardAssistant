# BardAssistant – The Bard’s Assistant (ESP32-S3)

A tiny “Vicious Mockery” companion built on an ESP32-S3 DevKitM-1.

Right now it:

- Uses **buttons** to request insults (`Random`, `Next`, `Prev`, `Sleep (hold)`).
- Uses a **NeoPixel** as a simple state indicator:
  - **Blue** – Booting
  - **Green** – Idle (ready for button presses)
  - **Yellow** – Updating (pretending to “do work”)
- Prints insults and status info over **Serial** (`115200` baud).

Planned:

- Move insult / deck / history logic into its own module.
- Add a **Waveshare 2.13" E-Ink** screen and render insults + header there.
- Power stuff / sleep / wake on long-press.

---

## Hardware

Current setup:

- **Board**: ESP32-S3 DevKitM-1
- **LED**:
  - 1x NeoPixel RGB LED
  - Data pin: `GPIO 21`
- **Buttons** (tactile switches to GND, using `INPUT_PULLUP` in code):
  - Sleep: `GPIO 6`
  - Random: `GPIO 4`
  - Next: `GPIO 5`
  - Prev: `GPIO 7`

Planned display (not wired/used yet):

- Waveshare 2.13" E-Ink Display HAT V4
  - 250x122, SPI, partial refresh
  - Will eventually show:
    - Title screen / logo
    - Current insult text
    - Basic “source” info (`Random / Next / Prev`)

---

## Firmware Overview

All of this lives in `src/main.cpp` for now, plus small modules:

- `lib/button/` – debounced button input
- `lib/led/` – NeoPixel status LED

### Application States

```cpp
enum class ApplicationState { Boot, Idle, Updating };
```

- **Boot**
  - LED: Blue (`ledShowBoot()`)
  - After ~2 seconds, transitions to **Idle**

- **Idle**
  - LED: Green (`ledShowIdle()`)
  - Listens for button taps:
    - Random → start a “Random insult” operation
    - Next → move forward through insult history (or draw new)
    - Prev → move backward through insult history

- **Updating**
  - LED: Yellow (`ledShowUpdating()`)
  - Simulates “work” with a small delay (`MOCK_WORK_MS`)
  - When “done”, applies the insult index and goes back to Idle

### Buttons & Events

Buttons are debounced and produce high-level events (from `button` module):

```cpp
enum class ButtonEvent { None, Tap, HoldStart, HoldEnd };
```

- Tap → triggers Random / Next / Prev operations _while in Idle_
- Sleep:
  - `HoldStart` is wired to eventually trigger deep sleep (`enterSleep()`),
    currently just logs.

### Insults, Deck, and History (Serial Only for Now)

- `insults[]` – array of `const char*` insult strings.
- **Deck**:
  - Shuffled list of indices, ensures “Random” doesn’t repeat until all have been used.

- **History**:
  - Remembers which insults have been shown.
  - `Next` / `Prev` navigate the history when possible.
  - `Next` at the end of history draws a new insult from the deck.

Rendering is currently:

- `renderTitleScreen()` – ASCII art + project name on boot (Serial only).
- `renderInsultAtIndex(...)` – logs:
  - Which insult index is active
  - The insult text itself
  - Optional context (`Random` vs `Next` vs `Prev`)

Later, these will be redirected to the E-Ink screen instead of (or in addition to) Serial.

---

## PlatformIO – Commands I Keep Forgetting

> All commands assume you’re in the project root (where `platformio.ini` lives).

### Build

**Generic:**

```bash
pio run
```

**Specific environment (ESP32-S3 DevKitM-1):**

```bash
pio run -e esp32-s3-devkitm-1
```

---

### Upload Firmware

**Generic upload for default env:**

```bash
pio run -t upload
```

**Upload to ESP32-S3 DevKitM-1 explicitly:**

```bash
pio run -e esp32-s3-devkitm-1 -t upload
```

---

### Erase Flash

Useful when things get weird or after changing flash layout / partitions:

```bash
pio run -e esp32-s3-devkitm-1 -t erase
```

Then re-upload your firmware.

---

### Clean Build Artifacts

If builds start acting cursed:

```bash
pio run -t clean
```

---

### Serial Monitor

**On my Mac / current setup:**

```bash
pio device monitor -p /dev/cu.usbmodem1101 -b 115200
```

Generic form:

```bash
pio device monitor -p <PORT> -b 115200
```

Hit `Ctrl + C` to exit.

---

## Development Notes

- Buttons are wired **to GND** and use `INPUT_PULLUP`:
  - Released → HIGH
  - Pressed → LOW

- Long-press on Sleep is **debounced and recognized**, but deep sleep is not
  enabled yet (call to `enterSleep()` is still commented).
- State machine and insult logic are conceptually separate:
  - `main.cpp` handles:
    - Board setup
    - State transitions
    - Calling “work engine” functions

  - Insult/deck/history logic is being prepped to move into a dedicated module
    (e.g., `lib/insults`).

---

## Setup (Fresh Clone)

```bash
# Clone
git clone https://github.com/jaylingelbach/BardAssistant.git
cd BardAssistant

# Build (default env)
pio run

# Or build for the ESP32-S3 env explicitly
pio run -e esp32-s3-devkitm-1

# Upload
pio run -e esp32-s3-devkitm-1 -t upload

# Monitor
pio device monitor -p /dev/cu.usbmodem1101 -b 115200
```
