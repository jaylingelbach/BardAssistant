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

/**
 * @brief Measures the rendered pixel width of a text string at the given size.
 *
 * @param text Null-terminated string to measure.
 * @param textSize GxEPD2 text size multiplier (1 = 8px tall, 2 = 16px, etc.).
 * @return uint16_t Pixel width of the string, or 0 if the driver is unavailable
 * or text is null.
 */
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

/**
 * @brief Draws word-wrapped text into a bounded region on the display.
 *
 * Words are placed left-to-right and wrapped to the next line when they exceed
 * `maxWidth`. Explicit newlines force a line break. Words wider than `maxWidth`
 * are hard-broken character-by-character. Text that would exceed `maxHeight` is
 * silently clipped.
 *
 * @param text Null-terminated string to render.
 * @param left Left edge of the text area in pixels.
 * @param top Top edge of the text area in pixels.
 * @param maxWidth Width of the text area in pixels.
 * @param maxHeight Height of the text area in pixels.
 * @param textSize GxEPD2 text size multiplier.
 */
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

/**
 * @brief Checks whether text fits within a bounded region without overflowing.
 *
 * Simulates the same word-wrap and hard-break logic as `drawWrappedText`
 * without touching the display, so callers can probe whether a given text size
 * fits before committing to a render.
 *
 * @param text Null-terminated string to test.
 * @param maxWidth Width of the target area in pixels.
 * @param maxHeight Height of the target area in pixels.
 * @param textSize GxEPD2 text size multiplier.
 * @return `true` if the text fits within the area, `false` if it overflows.
 */
static bool textFitsInBounds(const char *text, int16_t maxWidth,
                             int16_t maxHeight, uint8_t textSize) {
  if (displayDriver == nullptr || text == nullptr)
    return false;

  const int16_t lineHeight = static_cast<int16_t>(8 * textSize + 2);
  int16_t cursorY = 0;

  char lineBuf[160];
  char wordBuf[80];
  lineBuf[0] = '\0';

  const char *cursor = text;

  auto linesFull = [&]() -> bool {
    return cursorY + lineHeight > maxHeight;
  };

  bool overflowed = false;

  auto flushLine = [&]() {
    if (lineBuf[0] == '\0')
      return;
    cursorY = static_cast<int16_t>(cursorY + lineHeight);
    lineBuf[0] = '\0';
    if (cursorY > maxHeight)
      overflowed = true;
  };

  auto appendWord = [&](const char *word) {
    if (word == nullptr || word[0] == '\0')
      return;

    char candidate[160];
    if (lineBuf[0] == '\0') {
      std::snprintf(candidate, sizeof(candidate), "%s", word);
    } else {
      std::snprintf(candidate, sizeof(candidate), "%s %s", lineBuf, word);
    }

    if (measureTextWidth(candidate, textSize) <=
        static_cast<uint16_t>(maxWidth)) {
      std::snprintf(lineBuf, sizeof(lineBuf), "%s", candidate);
      return;
    }

    if (lineBuf[0] != '\0')
      flushLine();

    // Word wider than maxWidth: hard-break it across lines, same as
    // drawWrappedText, so the line count matches the actual render.
    if (measureTextWidth(word, textSize) > static_cast<uint16_t>(maxWidth)) {
      const size_t wordLen = std::strlen(word);
      size_t startIndex = 0;
      while (startIndex < wordLen && !overflowed) {
        char segment[160];
        segment[0] = '\0';
        size_t endIndex = startIndex;
        while (endIndex < wordLen) {
          const size_t segLen = endIndex - startIndex + 1;
          if (segLen >= sizeof(segment))
            break;
          std::memcpy(segment, &word[startIndex], segLen);
          segment[segLen] = '\0';
          if (measureTextWidth(segment, textSize) >
              static_cast<uint16_t>(maxWidth)) {
            if (segLen == 1)
              return;
            segment[segLen - 1] = '\0';
            break;
          }
          endIndex++;
        }
        cursorY = static_cast<int16_t>(cursorY + lineHeight);
        if (cursorY > maxHeight)
          overflowed = true;
        startIndex += std::strlen(segment);
      }
      lineBuf[0] = '\0';
      return;
    }

    std::snprintf(lineBuf, sizeof(lineBuf), "%s", word);
  };

  while (*cursor != '\0' && !overflowed) {
    if (*cursor == '\n') {
      flushLine();
      cursor++;
      continue;
    }
    while (*cursor == ' ')
      cursor++;
    if (*cursor == '\0')
      break;

    size_t wordLen = 0;
    while (cursor[wordLen] != '\0' && cursor[wordLen] != ' ' &&
           cursor[wordLen] != '\n') {
      if (wordLen + 1 >= sizeof(wordBuf))
        break;
      wordLen++;
    }
    std::memcpy(wordBuf, cursor, wordLen);
    wordBuf[wordLen] = '\0';

    appendWord(wordBuf);
    cursor += wordLen;
  }

  flushLine();
  return !overflowed;
}

/**
 * @brief Replaces characters unsupported by the display font with safe
 * substitutes.
 *
 * Converts UTF-8 curly apostrophes (U+2018, U+2019) and ASCII single quotes
 * to backticks, which the GxEPD2 default font renders correctly. Other
 * characters are copied unchanged.
 *
 * @param src Null-terminated source string.
 * @param dst Destination buffer for the sanitized output.
 * @param dstSize Size of the destination buffer in bytes, including the null
 * terminator.
 */
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

/**
 * @brief Checks whether a pin number appears in a list of reserved pins.
 *
 * @param value Pin number to search for.
 * @param list Array of reserved pin numbers.
 * @return `true` if `value` is found in `list`, `false` otherwise.
 */
static bool isInList(int8_t value, const std::array<int8_t, 4> &list) {
  for (int8_t item : list) {
    if (value == item) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Validates a DisplayConfig before it is applied to the driver.
 *
 * Checks that all required pins are assigned (>= 0), that no required pin
 * conflicts with the button pins, that required pins do not duplicate each
 * other, and that optional pins (busy, miso) do not collide with any required
 * or button pins.
 *
 * @param config Configuration to validate.
 * @return `true` if the configuration is safe to use, `false` otherwise.
 */
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

/**
 * @brief Initializes the e-ink display driver with the given configuration.
 *
 * Validates the config, configures the BUSY pin if wired, starts HSPI,
 * constructs the GxEPD2 driver, applies rotation, performs an initial full
 * clear, and marks the display ready. Calling this a second time tears down
 * the existing driver before constructing a new one.
 *
 * @param config Pin assignments, rotation, and driver options.
 * @return `true` if initialization succeeds, `false` if config validation fails.
 */
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

/**
 * @brief Fills the display with white, effectively clearing it.
 *
 * @return `true` if the display is initialized and the clear is applied,
 * `false` if the display is unavailable.
 */
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

/**
 * @brief Replaces the display contents with guidance for an empty insult deck.
 *
 * @return `true` when the display is initialized and the empty state is
 * rendered, or `false` when the display is unavailable.
 */
bool displayRenderEmptyState() {
  if (!displayReady || displayDriver == nullptr)
    return false;

  const int16_t screenW = static_cast<int16_t>(displayDriver->width());
  const int16_t screenH = static_cast<int16_t>(displayDriver->height());
  const int16_t margin = 8;
  const int16_t areaW = static_cast<int16_t>(screenW - 2 * margin);
  const int16_t areaH = static_cast<int16_t>(screenH - 2 * margin);

  displayDriver->setFullWindow();
  displayDriver->firstPage();
  do {
    displayDriver->fillScreen(GxEPD_WHITE);
    drawWrappedText("No insults loaded. Add some via the web UI.", margin,
                    margin, areaW, areaH, 1);
  } while (displayDriver->nextPage());

  return true;
}

/**
 * @brief Renders an insult string on the e-ink display.
 *
 * Sanitizes the text, then performs a two-pass size selection: text size 2 is
 * used when the content fits within the body area, falling back to size 1 for
 * longer strings. Null text is rendered as "(null)".
 *
 * @param text Null-terminated insult string to display.
 * @return `true` if the display is initialized and the render completes,
 * `false` if the display is unavailable.
 */
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

  const uint8_t headerSize = 1;
  const int16_t headerY = margin;
  const int16_t headerHeight = static_cast<int16_t>(8 * headerSize + 2);

  const int16_t bodyTop = static_cast<int16_t>(headerY + headerHeight + 6);
  const int16_t bodyHeight = static_cast<int16_t>(screenH - bodyTop - margin);
  const int16_t bodyWidth = static_cast<int16_t>(screenW - 2 * margin);

  // Two-pass: prefer size 2, fall back to size 1 if text overflows.
  const uint8_t bodySize =
      textFitsInBounds(text, bodyWidth, bodyHeight, 2) ? 2 : 1;

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

/**
 * @brief Puts the display into the requested low-power mode.
 *
 * `KeepPowered` is a no-op that returns success. `Hibernate` calls the
 * GxEPD2 hibernate routine, which is the lowest-power option and should be
 * called just before deep sleep.
 *
 * @param mode Desired sleep mode.
 * @return `true` if the mode was applied, `false` if the display is
 * unavailable or the mode is unrecognized.
 */
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