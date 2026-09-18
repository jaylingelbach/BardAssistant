# Bard's Assistant — Developer Setup

## Prerequisites

- VS Code
- PlatformIO IDE extension (`platformio.ide`)
- clangd extension (`llvm-vs-code-extensions.vscode-clangd`)

The Microsoft C/C++ extension (`ms-vscode.cpptools`) is installed but disabled at the workspace level — PlatformIO manages IntelliSense through clangd instead.

---

## Go to Definition

Go to Definition (`Fn+F12` or `Cmd+Click`) is powered by clangd reading `compile_commands.json` at the project root.

### First-time setup

After cloning or after a clean build, generate the file:

```sh
pio run -e esp32-s3-devkitm-1 -t compiledb
```

Then in VS Code: `Cmd+Shift+P` → **PlatformIO: Rebuild IntelliSense Index**.

### Keeping it up to date

`compile_commands.json` goes stale when you add new source files or dependencies. Regenerate it with the same command:

```sh
pio run -e esp32-s3-devkitm-1 -t compiledb
```

Normal builds (`pio run`) should also regenerate it automatically via `generate_compile_commands.py`.

---

## Flashing

```sh
# Flash firmware
pio run -e esp32-s3-devkitm-1 --target upload

# Flash filesystem (LittleFS — web UI, insults.json)
pio run -e esp32-s3-devkitm-1 --target uploadfs
```

Both commands are also in the PlatformIO sidebar under **Project Tasks**.

---

## Serial Monitor

```sh
pio device monitor
```

Baud rate is set to `115200` in `platformio.ini`.

---

## Logging

All serial output goes through `lib/include/log.h`. The log level is set at compile time via `LOG_LEVEL` in `platformio.ini` — output below the active level is stripped entirely from the binary (no string literals in flash, no runtime cost).

### Levels

| Value | Name  | What prints                        |
|-------|-------|------------------------------------|
| `0`   | OFF   | Nothing                            |
| `1`   | ERROR | Hard failures only                 |
| `2`   | WARN  | Errors + recoverable warnings      |
| `3`   | INFO  | Errors + warnings + lifecycle info |
| `4`   | DEBUG | Everything (button taps, WiFi dots, insult title cards) |

### Changing the level

In `platformio.ini`, under `[env:esp32-s3-devkitm-1]`:

```ini
build_flags =
    ...
    -DLOG_LEVEL=4   ; dev — change to 3 for a release build
```

`LOG_LEVEL=4` is the default for development. Set to `3` before shipping to strip all debug output.

### Macros

| Macro | Use for |
|-------|---------|
| `LOG_ERROR(msg)` | Hard failures |
| `LOG_WARN(msg)` | Recoverable edge cases |
| `LOG_INFO(msg)` | Lifecycle events (boot, connect, save) |
| `LOG_DEBUG(msg)` | Verbose dev output (button events, nav steps) |
| `LOG_ERRORF(fmt, ...)` | printf-style with runtime values |
| `LOG_INFO_RAW(val)` / `LOG_DEBUG_RAW(val)` | `Serial.println(val)` for non-string types (IPAddress, int, etc.) |
| `LOG_INFO_PRINT(val)` / `LOG_DEBUG_PRINT(val)` | `Serial.print(val)` without newline |

Use `F()` wrapping is handled inside the macros for string literals — don't add it at the call site.

---

## Test Display Environment

A separate `test-display` env compiles `test_display.cpp` instead of `main.cpp` for isolated display testing:

```sh
pio run -e test-display --target upload
```
