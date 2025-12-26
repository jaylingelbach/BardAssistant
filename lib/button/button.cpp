#include "button.h"
#include <Arduino.h>

#define DEBOUNCE_TIME_MS 30
#define HOLD_THRESHOLD_MS 800

// ─── Constructor style initializer ──────────────────────────────
void buttonInit(Button &button, uint8_t pin) {
  button.pin = pin;

  // If you're wiring the button to GND when pressed:
  pinMode(pin, INPUT_PULLUP);

  // Establish a known baseline so the first update is predictable.
  button.lastReading = digitalRead(pin);
  button.lastDebounceTime = millis();

  // Choose a consistent boot behavior:
  // start "released" semantically, then let updateButton observe presses
  // normally.
  button.state = ButtonState::Idle;

  button.pressedAt = 0;
  button.holdFired = false;
}

ButtonEvent updateButton(Button &button, uint32_t now) {
  ButtonEvent event = ButtonEvent::None;

  // “Raw reading changed → reset debounce window”
  int raw = digitalRead(
      button
          .pin); // returns HIGH or LOW. LOW → circuit closed (button physically
                 // pressed) HIGH → circuit open (button physically released)

  if (raw != button.lastReading) {
    // Reset the debounce timer if the state is unstable
    button.lastDebounceTime = now;
    button.lastReading = raw;
    return ButtonEvent::None;
  }

  if ((now - button.lastDebounceTime) < DEBOUNCE_TIME_MS) {
    return ButtonEvent::None;
  }

  // From here on, raw is "trusted" (stable).
  const bool pressedNow = (raw == LOW);

  // Handle debounced transitions FIRST.

  // Idle -> Pressed
  if (pressedNow && button.state == ButtonState::Idle) {
    button.state = ButtonState::Pressed;
    button.pressedAt = now;
    button.holdFired = false;
    return ButtonEvent::None; // Tap vs Hold is decided later on release
  }

  // Pressed -> Idle (release)
  if (!pressedNow && button.state == ButtonState::Pressed) {
    button.state = ButtonState::Idle;
    return button.holdFired ? ButtonEvent::HoldEnd : ButtonEvent::Tap;
  }

  // No transition happened this call. If still pressed, check hold threshold.
  if (button.state == ButtonState::Pressed && !button.holdFired) {
    if ((now - button.pressedAt) >= HOLD_THRESHOLD_MS) {
      button.holdFired = true;
      return ButtonEvent::HoldStart;
    }
  }
  return event;
}
