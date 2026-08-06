#include "display.h"
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <epd/GxEPD2_213_B74.h>

static bool displayReady = false;
static DisplayConfig activeConfig{};
static bool dirty = false;
static int updateCountSinceFull = 0;
static constexpr std::array<int8_t, 4> kButtonPins = {4, 5, 6, 7};

using Panel = GxEPD2_213_B74;
using DriverObject = GxEPD2_BW<Panel, Panel::HEIGHT>;

// displayDriver is a pointer.
static DriverObject *displayDriver = nullptr;
static SPIClass hspi(HSPI);

// --- internal helpers ---------------------------------

static uint16_t measureTextWidth(const char *text, uint8_t textSize) {
  if (displayDriver == nullptr || text == nullptr)
    return 0;

  displayDriver->setTextSize(textSize);

  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  displayDriver->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

static void drawWrappedText(const char *text, int16_t left, int16_t top,
                            int16_t maxWidth, int16_t maxHeight,
                            uint8_t textSize) {
  if (displayDriver == nullptr)
    return;

  displayDriver->setTextColor(GxEPD_BLACK);
  displayDriver->setTextSize(textSize);

  const int16_t lineHeight = static_cast<int16_t>(8 * textSize + 2);

  int16_t cursorY = top;
  displayDriver->setCursor(left, cursorY);

  // Buffers for building lines/words without heap allocations
  char lineBuf[160];
  char wordBuf[80];

  lineBuf[0] = '\0';

  const char *cursor = (text == nullptr) ? "" : text;

  auto flushLine = [&]() {
    if (lineBuf[0] == '\0')
      return;
    // Stop if we run out of vertical space
    if (cursorY + lineHeight > top + maxHeight)
      return;
    displayDriver->setCursor(left, cursorY);
    displayDriver->print(lineBuf);
    cursorY = static_cast<int16_t>(cursorY + lineHeight);
    lineBuf[0] = '\0';
  };

  auto appendWordToLine = [&](const char *word) {
    if (word == nullptr || word[0] == '\0')
      return;

    // Build candidate line
    char candidate[160];
    if (lineBuf[0] == '\0') {
      std::snprintf(candidate, sizeof(candidate), "%s", word);
    } else {
      std::snprintf(candidate, sizeof(candidate), "%s %s", lineBuf, word);
    }

    const uint16_t candidateWidth = measureTextWidth(candidate, textSize);

    if (candidateWidth <= static_cast<uint16_t>(maxWidth)) {
      // Fits → accept candidate
      std::snprintf(lineBuf, sizeof(lineBuf), "%s", candidate);
      return;
    }

    // Doesn't fit:
    // 1) flush current line if it has content
    if (lineBuf[0] != '\0') {
      flushLine();
    }

    // 2) if word itself fits on a fresh line, put it there
    const uint16_t wordWidth = measureTextWidth(word, textSize);
    if (wordWidth <= static_cast<uint16_t>(maxWidth)) {
      std::snprintf(lineBuf, sizeof(lineBuf), "%s", word);
      return;
    }

    // 3) word is too long → hard-break it across lines
    const size_t wordLen = std::strlen(word);
    size_t startIndex = 0;

    while (startIndex < wordLen) {
      // If we run out of space, stop
      if (cursorY + lineHeight > top + maxHeight)
        return;

      char segment[160];
      segment[0] = '\0';

      // Grow a segment char-by-char until it no longer fits
      size_t endIndex = startIndex;
      while (endIndex < wordLen) {
        const size_t segLen = endIndex - startIndex + 1;
        if (segLen >= sizeof(segment))
          break;

        std::memcpy(segment, &word[startIndex], segLen);
        segment[segLen] = '\0';

        if (measureTextWidth(segment, textSize) >
            static_cast<uint16_t>(maxWidth)) {
          // segment too wide; back off
          if (segLen == 1) {
            // Can't fit even one char; give up
            return;
          }
          segment[segLen - 1] = '\0';
          break;
        }
        endIndex++;
      }

      // Print segment
      displayDriver->setCursor(left, cursorY);
      displayDriver->print(segment);
      cursorY = static_cast<int16_t>(cursorY + lineHeight);

      // Advance
      startIndex += std::strlen(segment);
    }

    // Clear line buffer since we printed directly
    lineBuf[0] = '\0';
  };

  while (*cursor != '\0') {
    // Handle explicit newlines: force flush
    if (*cursor == '\n') {
      flushLine();
      cursor++;
      continue;
    }

    // Skip spaces
    while (*cursor == ' ')
      cursor++;
    if (*cursor == '\0')
      break;

    // Read next word until space or newline
    size_t wordLen = 0;
    while (cursor[wordLen] != '\0' && cursor[wordLen] != ' ' &&
           cursor[wordLen] != '\n') {
      if (wordLen + 1 >= sizeof(wordBuf))
        break;
      wordLen++;
    }

    std::memcpy(wordBuf, cursor, wordLen);
    wordBuf[wordLen] = '\0';

    appendWordToLine(wordBuf);

    cursor += wordLen;

    // Stop if we're out of vertical space
    if (cursorY + lineHeight > top + maxHeight)
      break;
  }

  // Print any remaining buffered line
  flushLine();
}

static void sanitizeForDisplay(const char *src, char *dst, size_t dstSize) {
  size_t s = 0, d = 0;
  while (src[s] != '\0' && d < dstSize - 1) {
    if ((uint8_t)src[s] == 0xe2 && src[s + 1] != '\0' &&
        (uint8_t)src[s + 1] == 0x80 && src[s + 2] != '\0' &&
        ((uint8_t)src[s + 2] == 0x99 || (uint8_t)src[s + 2] == 0x98)) {
      dst[d++] = '`';
      s += 3;
    } else if (src[s] == '\'') {
      dst[d++] = '`';
      s++;
    } else {
      dst[d++] = src[s++];
    }
  }
  dst[d] = '\0';
}

// if this returns true, thats bad.
static bool isInList(int8_t value, const std::array<int8_t, 4> &list) {
  for (int8_t item : list) {
    if (value == item) {
      return true;
    }
  }
  return false;
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
  if (config.pins.busy >= 0) {
    if (isInList(config.pins.busy, kButtonPins))
      return false;
    for (int8_t pin : requiredPins) {
      if (config.pins.busy == pin)
        return false;
    }
  }
  if (config.pins.miso >= 0) {
    if (isInList(config.pins.miso, kButtonPins))
      return false;
    for (int8_t pin : requiredPins) {
      if (config.pins.miso == pin)
        return false;
    }
    if (config.pins.busy >= 0 && config.pins.miso == config.pins.busy)
      return false;
  }

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
    pinMode(activeConfig.pins.busy, INPUT);
  }

  // 4) Start SPI with explicit pins (HSPI — display wired to GPIO 12/13)
  hspi.begin(activeConfig.pins.sck, activeConfig.pins.miso,
             activeConfig.pins.mosi, activeConfig.pins.cs);

  // 5) Construct / configure the driver (from config pins)
  if (displayDriver != nullptr) {
    delete displayDriver;
    displayDriver = nullptr;
  }

  displayDriver = new DriverObject(
      Panel(activeConfig.pins.cs, activeConfig.pins.dc, activeConfig.pins.rst,
            useBusy ? activeConfig.pins.busy : -1));

  displayDriver->epd2.selectSPI(hspi,
                                SPISettings(4000000, MSBFIRST, SPI_MODE0));

  // 6) Initialize the display driver
  // init(baud, initial_reset, reset_duration, pulldown_rst_mode)
  displayDriver->init(115200, true, 2, activeConfig.pulldownRstMode);

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

bool displayRenderBlankScreen() {
  if (!displayReady || displayDriver == nullptr)
    return false;

  displayDriver->setFullWindow();
  displayDriver->firstPage();
  do {
    displayDriver->fillScreen(GxEPD_WHITE);
  } while (displayDriver->nextPage());

  return true;
}

bool displayRenderInsult(const char *text) {
  if (!displayReady || displayDriver == nullptr)
    return false;
  if (text == nullptr)
    text = "(null)";

  char sanitized[256];
  sanitizeForDisplay(text, sanitized, sizeof(sanitized));
  text = sanitized;

  // Use runtime width/height (respects rotation)
  const int16_t screenW = static_cast<int16_t>(displayDriver->width());
  const int16_t screenH = static_cast<int16_t>(displayDriver->height());

  const int16_t margin = 8;

  // Simple layout:
  // - optional small header line
  // - insult body wrapped below
  const uint8_t headerSize = 1;
  const uint8_t bodySize = 2;

  const int16_t headerY = margin;
  const int16_t headerHeight = static_cast<int16_t>(8 * headerSize + 2);

  const int16_t bodyTop = static_cast<int16_t>(headerY + headerHeight + 6);
  const int16_t bodyHeight = static_cast<int16_t>(screenH - bodyTop - margin);
  const int16_t bodyWidth = static_cast<int16_t>(screenW - 2 * margin);

  displayDriver->setFullWindow();
  displayDriver->firstPage();
  do {
    displayDriver->fillScreen(GxEPD_WHITE);

    // Header
    displayDriver->setTextColor(GxEPD_BLACK);
    displayDriver->setTextSize(headerSize);
    displayDriver->setCursor(margin, headerY + headerHeight);
    // displayDriver->print("BardAssistant");

    // Body
    drawWrappedText(text, margin, bodyTop, bodyWidth, bodyHeight, bodySize);

  } while (displayDriver->nextPage());

  return true;
}

bool displaySleep(DisplaySleepMode mode) {
  // if display isn't initialized, fail.
  if (!displayReady || displayDriver == nullptr)
    return false;

  switch (mode) {
  case DisplaySleepMode::KeepPowered:
    return true;
  case DisplaySleepMode::Hibernate:
    // GxEPD2 panels typically support hibernate(); this is the lowest-power
    // option.
    displayDriver->hibernate();
    return true;
  default:
    return false;
  }
}