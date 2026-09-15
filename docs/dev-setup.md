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

## Test Display Environment

A separate `test-display` env compiles `test_display.cpp` instead of `main.cpp` for isolated display testing:

```sh
pio run -e test-display --target upload
```
