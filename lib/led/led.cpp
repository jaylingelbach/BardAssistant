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

// ─── Internal helper ───────────────────────────────────────────
static void setColor(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ─── Public API ────────────────────────────────────────────────
void ledInit() {
  led.begin();
  led.clear();
  led.show();
}

void ledShowBoot() {
  setColor(0, 0, 255);   // Blue
}

void ledShowIdle() {
  setColor(0, 255, 0);   // Green
}

void ledShowUpdating() {
  setColor(255, 255, 0); // Yellow
}

void ledOff() {
  led.clear();
  led.show();
}
