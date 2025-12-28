#include "button.h"
#include "insults.h"
#include "led.h"
#include <Arduino.h>
#include <esp_sleep.h>

// ───────────────── Configuration ─────────────────

static Button sleepButton;
static Button randomButton;
static Button nextButton;
static Button prevButton;

static constexpr uint8_t PIN_SLEEP_BUTTON = 6;
static constexpr uint8_t PIN_RANDOM_BUTTON = 4;
static constexpr uint8_t PIN_NEXT_BUTTON = 5;
static constexpr uint8_t PIN_PREV_BUTTON = 7;

// Time/Power config
static constexpr uint64_t US_PER_S = 1000000ULL;
static constexpr uint64_t TIME_TO_SLEEP_S = 5ULL;

// Toggle this later when you want boot-insult behavior on screen too.
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// ───────────────── App State ─────────────────────

enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };

static ApplicationState currentState = ApplicationState::Boot;

// Timing
static uint32_t stateEnteredAt = 0;

/**
 * @brief Transition the application into the boot state.
 *
 * Sets the application state to Boot, records the time of entry, and triggers
 * the boot LED indicator.
 */
static void enterBoot() {
  ledShowBoot();
  currentState = ApplicationState::Boot;
  stateEnteredAt = millis();
}

/**
 * @brief Enter the Idle application state.
 *
 * Triggers the idle LED display, sets the current state to Idle, and records
 * the time the state was entered.
 */
static void enterIdle() {
  ledShowIdle();
  currentState = ApplicationState::Idle;
  stateEnteredAt = millis();
}

/**
 * @brief Enter the Updating application state and show the updating LED
 * pattern.
 *
 * Displays the updating LED indication, sets the application state to Updating,
 * and records the time the state was entered.
 */
static void enterUpdating() {
  ledShowUpdating();
  currentState = ApplicationState::Updating;
  stateEnteredAt = millis();
}

/**
 * @brief Powers down the device and enters deep sleep until the configured
 * wakeup time.
 *
 * Turns off LEDs, schedules a timer-based wakeup after TIME_TO_SLEEP_S seconds,
 * and starts deep sleep.
 */
static void enterSleep() {
  ledOff();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * US_PER_S);
  esp_deep_sleep_start();
}

// ───────────────── Work Orchestration ────────────

/**
 * @brief Handle a button event and trigger state-appropriate actions.
 *
 * Processes a single button event: ignores `None` events; always handles the
 * Sleep button (currently only acknowledging hold events); and for
 * Random/Next/Prev buttons, only reacts when the application is in the Idle
 * state — a Tap starts the corresponding operation.
 *
 * @param buttonId Identifier of the button that generated the event.
 * @param event The button event to process.
 * @param now Current time in milliseconds (typically from millis()) used for
 *            timestamping operations that are started.
 */
static void handleButtonEvent(ButtonId buttonId, ButtonEvent event,
                              uint32_t now) {
  if (event == ButtonEvent::None) {
    return;
  }

  // Sleep button is special; it’s allowed in any state.
  if (buttonId == ButtonId::Sleep) {
    if (event == ButtonEvent::HoldStart) {
      Serial.println(F("[Sleep] Hold detected (sleep currently disabled)."));
      // enterSleep();
    }
    return;
  }

  // For Random/Next/Prev we only start work from Idle.
  if (currentState != ApplicationState::Idle) {
    return;
  }

  if (event == ButtonEvent::Tap) {
    switch (buttonId) {
    case ButtonId::Random:
      Serial.println(F("[Random] Tap"));
      if (insultsStartOperation(PendingAction::Random, now)) {
        enterUpdating();
      }
      break;
    case ButtonId::Next:
      Serial.println(F("[Next] Tap"));
      if (insultsStartOperation(PendingAction::Next, now)) {
        enterUpdating();
      }
      break;
    case ButtonId::Prev:
      Serial.println(F("[Prev] Tap"));
      if (insultsStartOperation(PendingAction::Prev, now)) {
        enterUpdating();
      }
      break;
    default:
      break;
    }
  }
}

/**
 * @brief Initialize hardware, state, and startup display for the application.
 *
 * Performs global initialization: configures serial output, seeds the random
 * number generator, initializes LEDs, the insult deck, and all button inputs;
 * resets history tracking; enters the boot state and renders the title screen.
 *
 * If configured to show an insult on boot and insults are available, it draws
 * an initial insult, appends it to history, renders it with a boot reason,
 * and then transitions the application to the idle state.
 */
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("Booting Bard's Assistant..."));

  // Seed RNG for deck shuffling
  randomSeed(esp_random());

  ledInit();

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  enterBoot();

  insultsInit(PRINT_INSULT_ON_BOOT);
}

/**
 * @brief Main application loop that polls inputs and advances the application
 * state.
 *
 * Polls all four buttons, dispatches their events to the unified button
 * handler, and advances the high-level state machine. In Boot state,
 * transitions to Idle after two seconds. In Updating state, progresses the
 * ongoing operation by calling insultsPoll. Idle state has no time-driven
 * behavior.
 */
void loop() {
  const uint32_t now = millis();

  // Poll buttons
  const ButtonEvent sleepEvent = updateButton(sleepButton, now);
  const ButtonEvent randomEvent = updateButton(randomButton, now);
  const ButtonEvent nextEvent = updateButton(nextButton, now);
  const ButtonEvent prevEvent = updateButton(prevButton, now);

  handleButtonEvent(ButtonId::Sleep, sleepEvent, now);
  handleButtonEvent(ButtonId::Random, randomEvent, now);
  handleButtonEvent(ButtonId::Next, nextEvent, now);
  handleButtonEvent(ButtonId::Prev, prevEvent, now);

  // High-level app state machine
  switch (currentState) {
  case ApplicationState::Boot:
    // If we *didn't* print an insult on boot, fall into Idle after a short
    // delay.
    if (now - stateEnteredAt >= 2000) {
      enterIdle();
    }
    break;

  case ApplicationState::Idle:
    // Nothing time-based; we only move because of button events.
    break;

  case ApplicationState::Updating:
    if (insultsPoll(now)) {
      enterIdle();
    }
    break;
  }
}
