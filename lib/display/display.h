#ifndef DISPLAY_H
#define DISPLAY_H

#include <cstdint>

enum class DisplayMode : uint8_t { FullRefresh, PartialRefresh };

enum class DisplaySleepMode : uint8_t { KeepPowered, Hibernate };

enum class DisplayRotation : uint8_t { R0 = 0, R1 = 1, R2 = 2, R3 = 3 };

struct DisplayPins {
  int8_t rst = 8;
  int8_t dc = 9;
  int8_t cs = 10;
  int8_t busy = 11; // -1 means not connected / ignored

  int8_t mosi = 12;
  int8_t sck = 13;
  int8_t miso = -1; // often unused
};

struct DisplayConfig {
  DisplayPins pins{};
  DisplayRotation rotation = DisplayRotation::R1;
  DisplayMode modeDefault = DisplayMode::FullRefresh;
};
// ─── API ────────────────────────────────────────────────────────
bool displayValidateConfig(const DisplayConfig &config);
bool displayInit(const DisplayConfig &config);

// ─── Helpers ────────────────────────────────────────────────────
constexpr uint8_t toRotationValue(DisplayRotation rotation) {
  return static_cast<uint8_t>(rotation);
}

#endif // DISPLAY_H