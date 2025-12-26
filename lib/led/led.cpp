#include "led.h"

#include <Adafruit_NeoPixel.h>

// ─── Hardware configuration (private to this module) ───────────
#define LED_PIN   21
#define LED_COUNT 1

static Adafruit_NeoPixel led(
  LED_COUNT,
  LED_PIN,
  NEO_RGB + NEO_KHZ800
);

/**
 * @brief Set the single NeoPixel to the specified RGB color and apply the change.
 *
 * Sets the LED color using the provided red, green, and blue components and updates
 * the strip so the new color is visible.
 *
 * @param r Red component (0–255).
 * @param g Green component (0–255).
 * @param b Blue component (0–255).
 */
static void setColor(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

/**
 * @brief Initialize the NeoPixel LED and clear its color state.
 *
 * Configures the NeoPixel driver, clears any previous color data, and updates the LED
 * so it is off after initialization.
 */
void ledInit() {
  led.begin();
  led.clear();
  led.show();
}

/**
 * @brief Set the indicator LED to blue to indicate the boot state.
 *
 * Updates the LED color to RGB(0, 0, 255) and applies the change.
 */
void ledShowBoot() {
  setColor(0, 0, 255);     // Blue
}

/**
 * @brief Set the status LED to green to indicate the system is idle.
 */
void ledShowIdle() {
  setColor(0, 255, 0);     // Green
}

/**
 * @brief Indicate an ongoing update by setting the LED to yellow.
 *
 * Sets the single RGB LED to yellow to represent the "updating" state.
 */
void ledShowUpdating() {
  setColor(255, 255, 0);   // Yellow
}

/**
 * @brief Turns the LED off and applies the change.
 *
 * Clears all pixels and updates the LED hardware so the LED is set to off.
 */
void ledOff() {
  led.clear();
  led.show();
}