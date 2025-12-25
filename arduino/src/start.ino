#include <Adafruit_NeoPixel.h>
#include <esp_sleep.h>

// ─── Configuration ─────────────────────────────────────────────
#define LED_PIN 21                 // GPIO21 (onboard WS2812)
#define LED_COUNT 1                // Only ONE LED on this board
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP 5            // Time ESP32 will go to sleep (in seconds)

// ─── NeoPixel ──────────────────────────────────────────────────
Adafruit_NeoPixel led(
  LED_COUNT,
  LED_PIN,
  NEO_RGB + NEO_KHZ800);

// ─── Application States ────────────────────────────────────────
enum ApplicationState {
  STATE_BOOT,
  STATE_IDLE,
  STATE_UPDATING
};

ApplicationState currentState;
uint32_t stateEnteredAt;

// ─── Helpers ───────────────────────────────────────────────────
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ─── State Entry Functions ─────────────────────────────────────
void enterBoot() {
  setColor(0, 0, 255);  // Blue
  stateEnteredAt = millis();
}

void enterIdle() {
  setColor(0, 255, 0);  // Green
  stateEnteredAt = millis();
}

void enterUpdating() {
  setColor(255, 255, 0);  // Yellow
  stateEnteredAt = millis();
}

// ─── Sleep  ─────────────────────────────────────────────────────

void enterSleep() {
  // Put peripherals in a known state
  led.clear();
  led.show();

  // Declare wake source
  esp_sleep_enable_timer_wakeup(
    TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // End execution
  esp_deep_sleep_start();
}

void setup() {
  led.begin();
  led.clear();

  currentState = STATE_BOOT;
  enterBoot();
}

void loop() {
  uint32_t now = millis();

  switch (currentState) {

    case STATE_BOOT:
      if (now - stateEnteredAt >= 2000) {
        currentState = STATE_IDLE;
        enterIdle();
      }
      break;

    case STATE_IDLE:
      if (now - stateEnteredAt >= 3000) {
        currentState = STATE_UPDATING;
        enterUpdating();
      }

      break;

    case STATE_UPDATING:
      if (now - stateEnteredAt >= 1000) {
        currentState = STATE_IDLE;
        enterIdle();
        // enterSleep();
      }
      break;

    
    default:
      break;
  }
}
