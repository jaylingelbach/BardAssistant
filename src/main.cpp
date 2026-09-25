#include <Arduino.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <cstdint>

#include "HWCDC.h"
#include "button.h"
#include "display.h"
#include "driver/rtc_io.h"
#include "insults.h"
#include "led.h"
#include "log.h"
#include "networkManager.h"
#include "persist_keys.h"
#include "provisioning.h"
#include "webServerManager.h"

// ───────────────── Development flags ─────────────

// ───────────────── Configuration ─────────────────

// Buttons
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

// If true, on cold boot we immediately show an insult (after init).
// Wake-from-sleep always shows the last insult regardless of this flag.
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// EXT0 wake requires an RTC-capable GPIO.
// Using the same physical Sleep button for both sleep + wake.
static constexpr gpio_num_t WAKEUP_GPIO = GPIO_NUM_7;

// Display configuration (pins/rotation/default mode live in DisplayConfig
// defaults)
static DisplayConfig displayConfig{};

// Web Server
static WebServerManager webServerManager;

// ───────────────── App State ─────────────────────

enum class ApplicationState {
  Boot,
  Idle,
  Updating,
  Provisioning,
  ProvisioningConfirmation,
  WebModeConnecting
};
enum class ButtonId { Sleep, Random, Next, Prev };
// Idle means not provisioning. Waiting means provisioning has started and
// we are waiting for a result.
enum class ProvisioningState { Idle, Waiting, Success, Failed };

static ApplicationState currentState = ApplicationState::Boot;
static ProvisioningState provisioningState = ProvisioningState::Idle;

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

static bool gestureActive = false;
static uint32_t gestureStartedAt = 0;
static bool gestureTriggered = false;
static bool isWebModeActive = false;
static bool gestureIsProvisioning = false;

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
 * Provisioning uses the updating pattern; confirmation leaves the LED
 * unchanged.
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
    case ApplicationState::Provisioning:
      ledShowUpdating();
  }
}

/**
 * @brief Called exactly once when an insult operation completes.
 *
 * This is the "source of truth" for rendering after button-driven actions:
 * when the operation completes, the insults module has updated its internal
 * state (current insult + history pointer), so we render whatever is current.
 */
static void onOperationCompleted() {
  if (insultsHasAny()) {
    displayRenderInsult(insultsGetCurrentText());
  }
  enterIdle();
}

/**
 * @brief Enter deep sleep and configure wake via the Sleep button (EXT0).
 *
 * EXT0 wake is level-based (not edge-based): the chip wakes when the RTC GPIO
 * is held at the configured logic level.
 *
 * With the button wired to GND and the pin using a pull-up, the “pressed” level
 * is LOW, so we wake on LOW (wake happens immediately on press).
 *
 * New sleep behavior (chosen UX):
 * - Persist insult state so we can restore and re-render the last insult on
 * wake.
 * - Blank the e-ink screen so the device looks "off" while sleeping.
 * - Hibernate the display driver to minimize power while asleep.
 * - Set an NVS "slept" flag so setup() can classify the next boot as
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
    LOG_ERRORF("EXT0 wake config failed: %d\n", err);
  }

  // Keep the wake pin at the inactive level while asleep.
  // (Pull-up enabled since inactive is HIGH, pull-down disabled.)
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);

  // Persist app/module state for restore after wake.
  insultsPersistForSleep();

  // Make the device look "off" while sleeping.
  // (E-ink holds the last image with no power, so we must blank it BEFORE
  // sleep.)
  displayRenderBlankScreen();
  displaySleep(DisplaySleepMode::Hibernate);

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

  esp_deep_sleep_start();  // never returns
}

// ───────────────── Work Orchestration ────────────

/**
 * @brief Starts Wi-Fi provisioning and shows connection instructions.
 *
 * On startup failure, returns to idle and restores the current insult or
 * empty-state screen. An already active portal leaves the state unchanged.
 */
static void startProvisioning() {
  ProvisioningStartResult provisioningResult = provisioningStart();

  if (provisioningResult == ProvisioningStartResult::STARTED) {
    currentState = ApplicationState::Provisioning;
    provisioningState = ProvisioningState::Waiting;
    displayRenderMessage(
        "Connect to WiFi:\nBardsAssistant\n\nPassword:\nbardsassistant\n\nThen "
        "visit\n192.168.4.1");

  } else if (provisioningResult == ProvisioningStartResult::ALREADY_ACTIVE) {
    LOG_DEBUG("[startProvisioning] Provisioning is already active.");

  } else if (provisioningResult == ProvisioningStartResult::START_FAILED) {
    LOG_ERROR("[startProvisioning] Provisioning Failed to start.");

    currentState = ApplicationState::Idle;

    if (insultsHasAny()) {
      displayRenderInsult(insultsGetCurrentText());
    } else {
      displayRenderEmptyState();
    }
  }
}

/**
 * @brief Attempts to enter web mode and displays its connection address.
 *
 * Starts the web server even if mDNS fails, showing the IP address instead of
 * the hostname. If Wi-Fi connection fails, leaves web mode inactive and
 * restores the current insult or empty-state screen.
 */
static void handleWebModeToggle(uint32_t now) {
  WebModeStartResult result = startWebModeConnection(now);

  if (result == WebModeStartResult::STARTED) {
    currentState = ApplicationState::WebModeConnecting;
    isWebModeActive = false;
  } else if (result == WebModeStartResult::START_FAILED) {
    LOG_ERROR("[handleWebModeToggle] Web Mode start failed.");
  }
}

/**
 * @brief Exits web mode and restores the current insult or empty-state screen.
 *
 * Stops the web server, ends mDNS if active, and disconnects Wi-Fi.
 */
static void handleExitWebMode() {
  webServerManager.stop();
  exitWebMode();
  isWebModeActive = false;
  LOG_INFO("[WebMode] Exited successfully.");
  if (insultsHasAny()) {
    displayRenderInsult(insultsGetCurrentText());
  } else {
    displayRenderEmptyState();
  }
}

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
 * - Tap cancels any pending arming; in provisioning confirmation it also
 *   returns to idle and restores the insult or empty-state screen.
 *
 * Random/Next/Prev behavior:
 * - In provisioning confirmation, a Next tap starts provisioning; other
 *   non-Sleep events are ignored.
 * - Otherwise, only processed while in Idle and outside web mode.
 * - Tap attempts the corresponding insult operation and transitions to Updating
 *   when an operation starts.
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
      LOG_DEBUG("[Sleep] HoldStart (armed). Release to sleep.");
      return;
    }
    if (event == ButtonEvent::HoldEnd) {
      if (sleepArmed) {
        LOG_DEBUG("[Sleep] HoldEnd (released). Going to sleep.");
        sleepArmed = false;
        enterSleep();
      }
      return;
    }
    if (event == ButtonEvent::Tap) {
      sleepArmed = false;
      if (currentState == ApplicationState::ProvisioningConfirmation) {
        currentState = ApplicationState::Idle;
        if (insultsHasAny()) {
          displayRenderInsult(insultsGetCurrentText());
        } else {
          displayRenderEmptyState();
        }
      } else {
        restoreLedForState();
      }
      return;
    }
  }

  // Provisioning Confirmation buttons mean different things.
  if (currentState == ApplicationState::ProvisioningConfirmation) {
    if (event != ButtonEvent::Tap) {
      return;
    }
    if (buttonId == ButtonId::Next) {
      startProvisioning();
      return;
    }
    return;
  }

  // For Random/Next/Prev we only start work from Idle and outside Web Mode.
  if (currentState != ApplicationState::Idle || isWebModeActive) {
    return;
  }

  if (event == ButtonEvent::Tap) {
    switch (buttonId) {
      case ButtonId::Random:
        LOG_DEBUG("[Random] Tap");
        if (insultsStartOperation(PendingAction::Random, now)) {
          enterUpdating();
        }
        break;

      case ButtonId::Next:
        LOG_DEBUG("[Next] Tap");
        if (!gestureActive && !gestureTriggered) {
          if (insultsStartOperation(PendingAction::Next, now)) {
            enterUpdating();
          }
        }
        break;

      case ButtonId::Prev:
        LOG_DEBUG("[Prev] Tap");
        if (!gestureActive && !gestureTriggered) {
          if (insultsStartOperation(PendingAction::Prev, now)) {
            enterUpdating();
          }
        }
        break;

      default:
        break;
    }
  }
}

/**
 * @brief Handles long-press gestures for provisioning and web mode.
 *
 * While idle, holding Next, Prev, and Random for two seconds starts
 * provisioning or requests confirmation when credentials are saved. Holding
 * Next and Prev for two seconds toggles web mode and updates the display.
 *
 * @param now Current uptime in milliseconds, used to time the held gesture.
 */
static void handleButtonGestures(uint32_t now) {
  const bool webmodeGesture = nextButton.state == ButtonState::Pressed &&
                              prevButton.state == ButtonState::Pressed;

  const bool provisioningGesture = nextButton.state == ButtonState::Pressed &&
                                   prevButton.state == ButtonState::Pressed &&
                                   randomButton.state == ButtonState::Pressed;

  if (provisioningGesture && currentState == ApplicationState::Idle) {
    if (!gestureActive || !gestureIsProvisioning) {
      gestureActive = true;
      gestureStartedAt = now;
      gestureTriggered = false;
      gestureIsProvisioning = true;
    } else if (!gestureTriggered && now - gestureStartedAt >= 2000) {
      if (hasKnownNetwork()) {
        displayRenderMessage(
            "Wifi Already configured, change/add settings?, "
            "press Next to confirm, or Sleep to cancel.");
        currentState = ApplicationState::ProvisioningConfirmation;
      } else {
        startProvisioning();
      }
      gestureTriggered = true;
    }
  } else if (webmodeGesture && currentState == ApplicationState::Idle) {
    if (!gestureActive || gestureIsProvisioning) {
      gestureActive = true;
      gestureStartedAt = now;
      gestureTriggered = false;
      gestureIsProvisioning = false;
    } else if (!gestureTriggered && now - gestureStartedAt >= 2000) {
      if (!isWebModeActive) {
        handleWebModeToggle(now);
      } else {
        handleExitWebMode();
      }
      gestureTriggered = true;
    }
  } else {
    gestureActive = false;
    gestureStartedAt = 0;
    gestureTriggered = false;
    gestureIsProvisioning = false;
  }
}

// ───────────────── Arduino lifecycle ─────────────

/**
 * @brief Initializes the device for a cold boot or wake from deep sleep.
 *
 * Configures hardware, restores persistent insult state, renders the
 * appropriate display content, and starts Wi-Fi and the web server when Web
 * Mode startup is enabled.
 */
void setup() {
  Serial.begin(115200);
  delay(50);

  if (!LittleFS.begin(true)) {
    LOG_ERROR("LittleFS mount failed — restarting");
    Serial.flush();
    ESP.restart();
  }

  bool wokeFromSleep = false;
  {
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
      const uint8_t slept = prefs.getUChar("slept", 0);
      wokeFromSleep = (slept == 1);

      // IMPORTANT: do NOT clear here.
      // We clear later (after boot splash) so monitor reconnect/reset can't
      // hide the wake classification.
      needsSleepFlagClear = wokeFromSleep;

      prefs.end();
    }
  }

  LOG_INFO("");
  LOG_INFO("Booting Bard's Assistant...");

  // Seed RNG for deck shuffling
  randomSeed(esp_random());

  // Ignore intent events briefly after boot/wake.
  ignoreInputUntil = millis() + 200;

  ledInit();

  // After EXT0 deep-sleep wake, the wake pin may be latched as RTC IO.
  // Deinit it so we can use it as a normal GPIO with INPUT_PULLUP.
  // On cold boot this is a no-op (returns error, which we ignore).
  rtc_gpio_deinit(WAKEUP_GPIO);

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  enterBoot();

  if (!displayInit(displayConfig)) {
    LOG_ERROR("Display init failed");
  }

  // Initialize insults state (restores from NVS on wake).
  insultsInit(PRINT_INSULT_ON_BOOT, wokeFromSleep);

  // Render policy:
  // - Wake: show last insult immediately (screen was blanked before sleep).
  // - Cold boot: show an insult only if PRINT_INSULT_ON_BOOT is enabled.
  if (insultsHasAny()) {
    if (wokeFromSleep) {
      displayRenderInsult(insultsGetCurrentText());
    } else if (PRINT_INSULT_ON_BOOT) {
      displayRenderInsult(insultsGetCurrentText());
    }
  } else {
    displayRenderEmptyState();
  }
}
/**
 * @brief Polls device inputs, advances the application state, and services
 * the web server.
 *
 * Processes debounced button events, transitions from the boot splash to
 * idle, advances active insult operations, and renders completed operations
 * before returning to the idle state. It also polls Wi-Fi provisioning,
 * starts web mode after successful configuration, and services web requests
 * whenever web mode is active.
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

  // enable/disable webMode
  handleButtonGestures(now);

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
        onOperationCompleted();
      }
      break;
    case ApplicationState::Provisioning: {
      ProvisioningPollResult pollResult = provisioningPoll();
      if (pollResult == ProvisioningPollResult::SUCCESS) {
        provisioningState = ProvisioningState::Success;
        WebModeResult webRes = enterWebModeAlreadyConnected();
        if (webRes == WebModeResult::SUCCESS) {
          webServerManager.start();
          isWebModeActive = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "WiFi saved!\n%s.local\n%s",
                   BARDS_HOSTNAME, WiFi.localIP().toString().c_str());
          displayRenderMessage(msg);
        } else if (webRes == WebModeResult::MDNS_FAILED) {
          webServerManager.start();
          isWebModeActive = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "WiFi saved!\n%s",
                   WiFi.localIP().toString().c_str());
          displayRenderMessage(msg);
        } else {
          displayRenderMessage("WiFi saved!\nCouldn't start web mode.");
        }
        currentState = ApplicationState::Idle;
      } else if (pollResult == ProvisioningPollResult::FAILED) {
        provisioningState = ProvisioningState::Failed;
        currentState = ApplicationState::Idle;
        displayRenderMessage("WiFi setup cancelled/failed.");
      } else if (pollResult == ProvisioningPollResult::CANCELLED) {
        provisioningState = ProvisioningState::Idle;
        currentState = ApplicationState::Idle;
        if (insultsHasAny()) {
          displayRenderInsult(insultsGetCurrentText());
        } else {
          displayRenderEmptyState();
        }
      }
      break;
    }
    case ApplicationState::ProvisioningConfirmation: {
      break;
    }
    case ApplicationState::WebModeConnecting: {
      WebModePollResult result = pollWebModeConnection(now);

      if (result == WebModePollResult::SUCCESS) {
        currentState = ApplicationState::Idle;

        MDNSResult mdnsRes = startMDNS();

        if (mdnsRes == MDNSResult::MDNS_FAILED) {
          currentState = ApplicationState::Idle;

          isWebModeActive = false;

          displayRenderMessage(WiFi.localIP().toString().c_str());
        } else {
          isWebModeActive = true;
          webServerManager.start();
          displayRenderMessage(
              "Web Mode Activated. Visit BardsAssistant.local in the browser.");
        }

      } else if (result == WebModePollResult::FAILED) {
        currentState = ApplicationState::Idle;

        isWebModeActive = false;

        displayRenderMessage("Error activating Web Mode.");

      } else if (result == WebModePollResult::IN_PROGRESS) {
        // Nothing to do; keep polling next loop.
      }
      break;
    }
  }
  if (isWebModeActive) {
    webServerManager.handle();
  }
}
