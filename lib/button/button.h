#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

// ─── Physical / Debounced States ────────────────────────────────
enum class ButtonState { Idle, Pressed };

// ─── Button Events (Intent) ─────────────────────────────────────
enum class ButtonEvent { None = 0, Tap, HoldStart, HoldEnd };

// ─── Button State Container ─────────────────────────────────────
struct Button {
  uint8_t pin;

  // raw input
  int lastReading; // Raw pin comparison

  // Debounce
  ButtonState state;         // Stable debounced state
  uint32_t lastDebounceTime; // Debounce window

  // Timing
  uint32_t pressedAt; // Measure press duration

  // Intent
  bool holdFired; // Prevent tap + hold
};

// ─── API ────────────────────────────────────────────────────────
void buttonInit(Button &button, uint8_t pin);
ButtonEvent updateButton(Button &button, uint32_t now);

#endif // BUTTON_H
