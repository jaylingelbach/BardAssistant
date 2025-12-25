#include <Arduino.h>
#include "led.h"
#include <esp_sleep.h>

// ─── Configuration ─────────────────────────────────────────────
#define uS_TO_S_FACTOR 1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP 5            // Time ESP32 will go to sleep (in seconds)

// ─── Application States ────────────────────────────────────────
enum ApplicationState {
  STATE_BOOT,
  STATE_IDLE,
  STATE_UPDATING
};

ApplicationState currentState;
uint32_t stateEnteredAt;


// ─── State Entry Functions ─────────────────────────────────────
void enterBoot() {
  ledShowBoot();  // Blue
  stateEnteredAt = millis();
}

void enterIdle() {
  ledShowIdle();  // Green
  stateEnteredAt = millis();
}

void enterUpdating() {
  ledShowUpdating();  // Yellow
  stateEnteredAt = millis();
}

// ─── Sleep  ─────────────────────────────────────────────────────

void enterSleep() {
  // Put peripherals in a known state
  ledOff();

  // Declare wake source
  esp_sleep_enable_timer_wakeup(
    TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // End execution
  esp_deep_sleep_start();
}

void setup() {
  ledInit();

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
