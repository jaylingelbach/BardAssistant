#include "button.h"
#include "driver/rtc_io.h"
#include "insults.h"
#include "led.h"
#include "persist_keys.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sleep.h>

// ───────────────── Logging ───────────────────────

// Set to 0 to silence app logs (sleep/random/next/prev messages).
#define ENABLE_APP_LOGS 1

#if ENABLE_APP_LOGS
#define APP_LOGLN(msg) Serial.println(F(msg))
#else
#define APP_LOGLN(msg)                                                         \
  do {                                                                         \
  } while (0)
#endif

// ───────────────── Configuration ─────────────────
static Button sleepButton;
static Button randomButton;
static Button nextButton;
static Button prevButton;

static constexpr uint8_t PIN_RANDOM_BUTTON = 4;
static constexpr uint8_t PIN_NEXT_BUTTON = 5;
static constexpr uint8_t PIN_PREV_BUTTON = 6;
static constexpr uint8_t PIN_SLEEP_BUTTON = 7;

// Boot splash duration (Boot LED pattern)
static constexpr uint32_t LED_BOOT_DURATION_MS = 2000;

// Toggle this later when you want boot-insult behavior on screen too.
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// EXT0 wake requires an RTC-capable GPIO.
// Using the same physical Sleep button for both sleep + wake.
static constexpr gpio_num_t WAKEUP_GPIO = GPIO_NUM_7;

// ───────────────── App State ─────────────────────

enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };

static ApplicationState currentState = ApplicationState::Boot;

// Timing
static uint32_t stateEnteredAt = 0;

// Sleep gesture
static bool sleepArmed = false;

// Ignore early input right after boot/wake (prevents accidental actions)
static uint32_t ignoreInputUntil = 0;

// If we detect a deep-sleep wake, we clear the NVS flag later (after boot
// splash) so USB monitor reconnect/reset doesn’t hide the “woke-from-sleep”
// classification.
static bool needsSleepFlagClear = false;

// ───────────────── State transitions ─────────────

/**
 * @brief Enter the Boot state (boot LED splash).
 *
 * Sets the application state to Boot, shows the boot LED pattern,
 * and records when we entered the state.
 */
static void enterBoot() {
  ledShowBoot();
  currentState = ApplicationState::Boot;
  stateEnteredAt = millis();
}

/**
 * @brief Enter the Idle state (ready for button input).
 *
 * Shows the idle LED pattern and (if we previously woke from sleep)
 * clears the persisted NVS "slept" flag once we're safely running.
 */
static void enterIdle() {
  ledShowIdle();

  // Clear the sleep marker after the boot splash so a monitor-triggered reset
  // right after wake doesn’t misclassify future boots.
  if (needsSleepFlagClear) {
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
      prefs.putUChar("slept", 0);
      prefs.end();
    }
    needsSleepFlagClear = false;
  }

  currentState = ApplicationState::Idle;
  stateEnteredAt = millis();
}

/**
 * @brief Enter the Updating state (operation-in-progress).
 *
 * Shows the updating LED pattern and records when we entered the state.
 */
static void enterUpdating() {
  ledShowUpdating();
  currentState = ApplicationState::Updating;
  stateEnteredAt = millis();
}

/**
 * @brief Restore the LED pattern for the current application state.
 *
 * * Re-applies the LED pattern corresponding to the current state.
 */
static void restoreLedForState() {
  switch (currentState) {
  case ApplicationState::Boot:
    ledShowBoot();
    break;
  case ApplicationState::Idle:
    ledShowIdle();
    break;
  case ApplicationState::Updating:
    ledShowUpdating();
    break;
  }
}

/**
 * @brief Enter deep sleep and configure wake via the Sleep button (EXT0).
 *
 * This configures EXT0 wake on the Sleep button GPIO.
 * EXT0 wake is *level-based* (not edge-based): the chip wakes when the RTC GPIO
 * is held at the configured logic level.
 *
 * With the button wired to GND and the pin using a pull-up, the “pressed” level
 * is LOW, so we wake on LOW. This means wake happens immediately on press.
 *
 * Before sleeping:
 * - Persist the insults module state so we can restore it on wake.
 * - Store an NVS "slept" flag so setup() can treat the next boot as
 * wake-from-sleep.
 *
 * Note: deep sleep never returns; the device restarts from setup() on wake.
 */
static void enterSleep() {
  // Turn off LEDs before power domains drop.
  ledOff();

  // Configure wake on Sleep button press (LOW).
  esp_err_t err = esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0 /* LOW */);
  if (err != ESP_OK) {
    Serial.printf("EXT0 wake config failed: %d\n", err);
  }

  // Keep the wake pin at the inactive level while asleep.
  // (Pull-up enabled since inactive is HIGH, pull-down disabled.)
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);

  // Persist app/module state for restore after wake.
  insultsPersistForSleep();

  // Mark intent-to-sleep in NVS so next boot is treated as "wake".
  {
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
      prefs.putUChar("slept", 1);
      prefs.end();
    }
  }

  // Give serial + flash a moment to flush/commit before sleeping.
  Serial.flush();
  delay(50);

  esp_deep_sleep_start(); // returns void
}

// ───────────────── Work Orchestration ────────────

/**
 * @brief Handle a debounced button intent event and apply app-level behavior.
 *
 * Input gating:
 * - Events are ignored during a short post-boot/post-wake window to prevent
 *   accidental triggers from startup jitter or a button held during reset.
 *
 * Sleep button behavior (allowed in any state):
 * - HoldStart arms sleep.
 * - HoldEnd triggers deep sleep if sleep was armed ("hold → release to sleep").
 * - Tap cancels any pending arming (no-op otherwise).
 *
 * Random/Next/Prev behavior:
 * - Only processed while in Idle.
 * - Tap starts the corresponding insult operation and transitions to Updating.
 */
static void handleButtonEvent(ButtonId buttonId, ButtonEvent event,
                              uint32_t now) {
  if (event == ButtonEvent::None) {
    return;
  }

  // Ignore all button intent events for a short window after boot/wake.
  // Wraparound-safe check.
  if (static_cast<int32_t>(now - ignoreInputUntil) < 0) {
    sleepArmed = false;
    return;
  }

  // Sleep button is special; it’s allowed in any state.
  if (buttonId == ButtonId::Sleep) {
    if (event == ButtonEvent::HoldStart) {
      sleepArmed = true;
      ledShowSleep();
      APP_LOGLN("[Sleep] HoldStart (armed). Release to sleep.");
      return;
    }
    if (event == ButtonEvent::HoldEnd) {
      if (sleepArmed) {
        APP_LOGLN("[Sleep] HoldEnd (released). Going to sleep.");
        sleepArmed = false;
        enterSleep();
      }
      return;
    }
    if (event == ButtonEvent::Tap) {
      sleepArmed = false;
      restoreLedForState();
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
      APP_LOGLN("[Random] Tap");
      if (insultsStartOperation(PendingAction::Random, now)) {
        enterUpdating();
      }
      break;
    case ButtonId::Next:
      APP_LOGLN("[Next] Tap");
      if (insultsStartOperation(PendingAction::Next, now)) {
        enterUpdating();
      }
      break;
    case ButtonId::Prev:
      APP_LOGLN("[Prev] Tap");
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
 * @brief Initialize hardware, application state, and modules for device
 * boot/wake.
 *
 * - Reads an NVS "slept" flag to classify this boot as wake-from-deep-sleep.
 * - Sets a brief ignore window to suppress accidental input immediately after
 * boot/wake.
 * - Initializes LEDs and buttons.
 * - If waking from EXT0 deep sleep, deinitializes the wake GPIO from RTC IO
 * mode so it can be used as a normal digital input with INPUT_PULLUP again.
 * - Enters Boot state (boot LED splash) and initializes the insults module.
 */
void setup() {
  Serial.begin(115200);
  delay(50);

  bool wokeFromSleep = false;
  {
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
      const uint8_t slept = prefs.getUChar("slept", 0);
      wokeFromSleep = (slept == 1);

      // IMPORTANT: do NOT clear here.
      // We clear later (after boot splash) so monitor reconnect/reset can't
      // hide the wake.
      needsSleepFlagClear = wokeFromSleep;

      prefs.end();
    }
  }

  Serial.println();
  Serial.println(F("Booting Bard's Assistant..."));

  // Seed RNG for deck shuffling
  randomSeed(esp_random());

  // Ignore intent events briefly after boot/wake.
  ignoreInputUntil = millis() + 200;

  ledInit();

  // After EXT0 deep-sleep wake, the wake pin may be latched as RTC IO.
  // Deinit it so we can use it as a normal GPIO with INPUT_PULLUP.
  // On cold boot this is a no-op (returns error, which we ignore)
  rtc_gpio_deinit(WAKEUP_GPIO);

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  enterBoot();

  insultsInit(PRINT_INSULT_ON_BOOT, wokeFromSleep);
}

/**
 * @brief Main application loop: poll buttons and advance the state machine.
 *
 * - Polls all buttons and routes debounced intent events through
 * handleButtonEvent().
 * - Boot: holds the boot LED splash for a short duration, then enters Idle.
 * - Idle: waits for button-driven actions.
 * - Updating: advances the active insult operation via insultsPoll() until
 * done.
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
    if (now - stateEnteredAt >= LED_BOOT_DURATION_MS) {
      enterIdle();
    }
    break;

  case ApplicationState::Idle:
    break;

  case ApplicationState::Updating:
    if (insultsPoll(now)) {
      enterIdle();
    }
    break;
  }
}
