#include "button.h"
#include "driver/rtc_io.h"
#include "insults.h"
#include "led.h"
#include <Arduino.h>
#include <esp_sleep.h>

// ───────────────── Configuration ─────────────────

static Button sleepButton;
static Button randomButton;
static Button nextButton;
static Button prevButton;

static constexpr uint8_t PIN_RANDOM_BUTTON = 4;
static constexpr uint8_t PIN_NEXT_BUTTON = 5;
static constexpr uint8_t PIN_PREV_BUTTON = 6;
static constexpr uint8_t PIN_SLEEP_BUTTON = 7;

// Toggle this later when you want boot-insult behavior on screen too.
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// ───────────────── App State ─────────────────────

enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };

static ApplicationState currentState = ApplicationState::Boot;

static uint32_t stateEnteredAt = 0;
static constexpr uint32_t LED_BOOT_DURATION_MS = 2000;

// Sleep
static bool sleepArmed = false;
static uint32_t ignoreInputUntil = 0;
static constexpr gpio_num_t WAKEUP_GPIO = GPIO_NUM_7;

/**
 * @brief Transition the application into the boot state.
 *
 * Sets the application state to Boot, records the time of entry, and
 * triggers the boot LED indicator.
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
 * @brief Enter deep sleep and configure wakeup via the Sleep button (EXT0).
 *
 * Turns off LEDs, configures an EXT0 wake source on the Sleep button GPIO, and
 * enters deep sleep. With the button wired to GND and the pin using a pull-up,
 * the wake condition is active-low (GPIO == LOW when the button is pressed).
 *
 * RTC pull configuration is applied so the wake pin remains at the inactive
 * level while asleep (pull-up enabled, pull-down disabled). Serial output is
 * flushed before sleeping to avoid losing the final log line.
 *
 * Note: Deep sleep does not return; the device restarts from setup() on wake.
 */
static void enterSleep() {
  ledOff();
  esp_err_t wakeResponse = esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
  if (wakeResponse != ESP_OK) {
    Serial.printf("EXT0 wake config failed: %d\n", wakeResponse);
    Serial.flush();
    // Abort sleep to avoid unrecoverable state
    return;
  }
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);
  Serial.flush();
  delay(10);
  esp_err_t sleepResponse = esp_deep_sleep_start();
  if (sleepResponse != ESP_OK) {
    Serial.printf("EXT0 wake config failed: %d\n", sleepResponse);
    Serial.flush();
    // Abort sleep to avoid unrecoverable state
    return;
  }
}

// ───────────────── Work Orchestration ────────────

/**
 * @brief Handle a button intent event and trigger state-appropriate actions.
 *
 * Consumes a single debounced ButtonEvent and applies application-level policy.
 *
 * Input gating:
 * - If the current time is still within the post-boot / post-wake ignore window
 *   (now < ignoreInputUntil), the event is ignored to prevent accidental
 *   actions caused by wake/boot jitter or a button being held during startup.
 *
 * Sleep button behavior (allowed in any state):
 * - HoldStart arms sleep and may show a "sleep arming" indicator.
 * - HoldEnd (release) triggers deep sleep if sleep was armed. This implements
 *   a "hold → release to sleep" gesture and avoids immediate wake loops.
 * - Tap clears any pending sleep arming (no-op by default).
 *
 * Random/Next/Prev behavior:
 * - Only processed while in the Idle state.
 * - A Tap starts the corresponding insult operation and transitions the app
 *   into the Updating state.
 *
 * @param buttonId Identifier of the button that generated the event.
 * @param event The debounced intent event (None, Tap, HoldStart, HoldEnd).
 * @param now Current time in milliseconds (typically from millis()) used for
 *            input gating and timestamping operations.
 */
static void handleButtonEvent(ButtonId buttonId, ButtonEvent event,
                              uint32_t now) {
  if (event == ButtonEvent::None) {
    return;
  }

  // Ignore all button intent events for a short window after boot/wake.
  // Wraparound-safe: if now is "before" ignoreInputUntil, (now -
  // ignoreInputUntil) will be negative when interpreted as signed.
  if (static_cast<int32_t>(now - ignoreInputUntil) < 0) {
    // Safety: don't leave sleep armed during the ignore window.
    sleepArmed = false;
    return;
  }

  // Sleep button is special; it’s allowed in any state.
  if (buttonId == ButtonId::Sleep) {
    if (event == ButtonEvent::HoldStart) {
      ledShowSleep();
      sleepArmed = true;
      Serial.println(F("[Sleep] HoldStart (armed). Release to sleep."));
      return;
    }
    if (event == ButtonEvent::HoldEnd) {
      if (sleepArmed) {
        Serial.println(F("[Sleep] HoldEnd (released). Going to sleep."));
        sleepArmed = false;
        enterSleep();
      }
      return;
    }

    if (event == ButtonEvent::Tap) {
      // optional: tap does nothing or cancels pending sleep
      sleepArmed = false;
      // Restore LED to current state
      if (currentState == ApplicationState::Idle) {
        ledShowIdle();
      } else if (currentState == ApplicationState::Boot) {
        ledShowBoot();
      } else if (currentState == ApplicationState::Updating) {
        ledShowUpdating();
      }
      return;
    }
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
 * @brief Initialize hardware, application state, and modules for the device
 * boot/wake.
 *
 * Performs global initialization: configures serial output, seeds the random
 * number generator, initializes LEDs, and initializes all button inputs.
 *
 * Sets a short post-boot / post-wake input ignore window to prevent accidental
 * actions caused by startup jitter or a button being held during wake.
 *
 * If waking from deep sleep using EXT0, deinitializes the wake GPIO from RTC IO
 * mode so it can be used as a normal digital input with INPUT_PULLUP.
 *
 * Enters the Boot state (showing the boot LED pattern) and initializes the
 * insults module, which is responsible for rendering the title screen and any
 * optional boot-time content (e.g., printing an initial insult).
 */
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("Booting Bard's Assistant..."));

  // Seed RNG for deck shuffling
  randomSeed(esp_random());

  // ignore intent events briefly after boot/wake
  ignoreInputUntil = millis() + 200;

  ledInit();

  // After deep sleep wake (and harmless on cold boot) the wake pin is
  // configured as RTC IO; deinit it so we can use it as a normal GPIO again.
  rtc_gpio_deinit(WAKEUP_GPIO);

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  enterBoot();

  insultsInit(PRINT_INSULT_ON_BOOT);
}

/**
 * @brief Main application loop that polls inputs and advances the application
 * state machine.
 *
 * Polls all four buttons, dispatches their intent events to the unified button
 * handler, and advances the high-level application state.
 *
 * During Boot, the device holds the boot LED pattern for a short splash window
 * and then transitions to Idle. Random/Next/Prev actions are only accepted in
 * Idle, while the Sleep button is allowed in any state. A short post-boot /
 * post-wake ignore window may suppress early button events to avoid accidental
 * triggers.
 *
 * During Updating, the active insult operation is progressed by calling
 * insultsPoll() until it completes, then the device returns to Idle.
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
    // Boot splash: hold in Boot briefly (later this may become the title/screen
    // intro).
    if (now - stateEnteredAt >= LED_BOOT_DURATION_MS) {
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
