#include "display.h"
#include <GxEPD2_BW.h>
#include <array>
#include <cstdint>
#include <epd/GxEPD2_213_B74.h>

static bool displayReady = false;
static DisplayConfig activeConfig{};
static bool dirty = false;
static int updateCountSinceFull = 0;
static constexpr std::array<int8_t, 4> kButtonPins = {4, 5, 6, 7};

using Panel = GxEPD2_213_B74;
using DriverObject = GxEPD2_BW<Panel, Panel::HEIGHT>;

static DriverObject *displayDriver = nullptr;

// if this returns true, thats bad.
static bool isInList(int8_t value, const std::array<int8_t, 4> &list) {
  for (int8_t item : list) {
    if (value == item) {
      return true;
    }
    return false;
  }
}

bool displayValidateConfig(const DisplayConfig &config) {
  // 1) Required pins must be connected (>= 0)
  const std::array<int8_t, 5> requiredPins = {config.pins.cs, config.pins.dc,
                                              config.pins.rst, config.pins.sck,
                                              config.pins.mosi};

  for (int8_t pin : requiredPins) {
    if (pin < 0)
      return false;
  }

  // 2) Required pins must not collide with button pins
  for (int8_t pin : requiredPins) {
    if (isInList(pin, kButtonPins))
      return false;
  }

  // 3) Required pins shouldn’t duplicate each other (catches swapped/accidental
  // reuse)
  for (size_t firstIndex = 0; firstIndex < requiredPins.size(); firstIndex++) {
    for (size_t secondIndex = firstIndex + 1; secondIndex < requiredPins.size();
         secondIndex++) {
      if (requiredPins[firstIndex] == requiredPins[secondIndex])
        return false;
    }
  }

  // Optional pins: you can validate if present
  if (config.pins.busy >= 0 && isInList(config.pins.busy, kButtonPins))
    return false;
  if (config.pins.miso >= 0 && isInList(config.pins.miso, kButtonPins))
    return false;

  return true;
}

bool displayInit(const DisplayConfig &config) {
  // 0) Reset internal state
  displayReady = false;
  dirty = false;
  updateCountSinceFull = 0;

  // 1) Validate config (pins present, no duplicates, no conflicts)
  if (!displayValidateConfig(config)) {
    Serial.println("Display init failed: invalid config");
    return false;
  }

  // 2) Store config (copy it so we can use it later)
  activeConfig = config;

  // Decide whether we are using BUSY (wired) or ignoring it (-1)
  const bool useBusy = (activeConfig.pins.busy >= 0);

  // 3) Configure BUSY pin behavior (optional)
  if (useBusy) {
    pinMode(activeConfig.pins.busy, INPUT_PULLUP);
  }

  // 4) Start SPI with explicit pins
  SPI.begin(activeConfig.pins.sck, activeConfig.pins.miso,
            activeConfig.pins.mosi, activeConfig.pins.cs);

  // 5) Construct / configure the driver (from config pins)
  if (displayDriver != nullptr) {
    delete displayDriver;
    displayDriver = nullptr;
  }

  displayDriver = new DriverObject(
      Panel(activeConfig.pins.cs, activeConfig.pins.dc, activeConfig.pins.rst,
            useBusy ? activeConfig.pins.busy : -1));

  // 6) Initialize the display driver
  // init(baud, initial_reset, reset_duration, pulldown_busy)
  displayDriver->init(115200, true, 2, useBusy);

  // 7) Apply rotation + baseline window mode
  displayDriver->setRotation(toRotationValue(activeConfig.rotation));
  displayDriver->setFullWindow();

  // 8) Clear once on init (fresh boot policy)
  // Using full-window page loop to match GxEPD2's drawing model.
  displayDriver->firstPage();
  do {
    displayDriver->fillScreen(GxEPD_WHITE);
  } while (displayDriver->nextPage());

  dirty = false;
  updateCountSinceFull = 0;

  // 9) Mark ready + log
  displayReady = true;
  Serial.println("Display initialized");
  return true;
}
