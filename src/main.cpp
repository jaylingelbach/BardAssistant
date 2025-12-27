#include "button.h"
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
static constexpr uint32_t MOCK_WORK_MS = 800;

// Toggle this later when you want boot-insult behavior on screen too.
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// ───────────────── App State ─────────────────────

enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };
enum class PendingAction { None, Random, Next, Prev };
enum class OperationPhase { Idle, Waiting, Done };
enum class RenderReason { Boot, OperationStart, OperationComplete, UserTap };

static ApplicationState currentState = ApplicationState::Boot;
static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// “Is the thing we’re working on a brand-new insult or just history
// navigation?”
static bool operationIsNewInsult = false;

// Timing
static uint32_t stateEnteredAt = 0;
static uint32_t operationStartedAt = 0;

// ───────────────── Insults / History / Deck ──────

// Source data (future: swap this to a table from flash or SD)
static const char *const insults[] = {
    "You fight like a dairy farmer.",
    "You have the manners of a troll.",
    "I’ve spoken with sewer rats more polite than you.",
};

static constexpr size_t insultCount = sizeof(insults) / sizeof(insults[0]);

// Random “deck” of indices 0..insultCount-1
static uint16_t deck[insultCount] = {0};
static size_t deckPosition = 0;

// History: sequence of actually displayed insults
static constexpr size_t HISTORY_CAP = insultCount;
static uint16_t history[HISTORY_CAP] = {0};
static size_t historySize = 0;     // how many are valid
static size_t historyPosition = 0; // cursor into history

// Current index being shown
static uint16_t currentInsultIndex = 0;
static uint16_t pendingInsultIndex = 0;

/**
 * @brief Populate the deck with indices for available insults, randomize their order, and reset the draw position.
 *
 * Uses the global insultCount to determine the number of entries, fills the deck with values 0..insultCount-1,
 * randomizes the deck order, and sets deckPosition to 0 so the next draw starts from the beginning of the shuffled deck.
 */

static void initDeck() {
  // Fill with 0,1,2,...,N-1
  for (size_t i = 0; i < insultCount; ++i) {
    deck[i] = static_cast<uint16_t>(i);
  }

  // Fisher–Yates shuffle
  for (size_t i = insultCount - 1; i > 0; --i) {
    const long r = random(0, static_cast<long>(i + 1)); // 0..i
    const uint16_t tmp = deck[i];
    deck[i] = deck[r];
    deck[r] = tmp;
  }

  deckPosition = 0;
}

/**
 * @brief Draws the next insult index from the shuffled deck.
 *
 * If the deck has been exhausted it is reshuffled before drawing. When there
 * are no insults configured, returns 0.
 *
 * @return uint16_t Index of the next insult in the range [0, insultCount - 1], or `0` if `insultCount == 0`.
 */
static uint16_t drawFromDeck() {
  if (insultCount == 0) {
    return 0;
  }

  if (deckPosition >= insultCount) {
    // All insults used; reshuffle for a fresh “deck”
    initDeck();
  }

  const uint16_t idx = deck[deckPosition];
  deckPosition++;
  return idx;
}

/**
 * @brief Appends an insult index to the displayed-history buffer.
 *
 * Adds the provided insult index to history and advances the history cursor to the newest entry.
 *
 * @param index Index of the insult to append (expected to be a valid index into the insults array).
 *
 * If HISTORY_CAP is zero the function is a no-op. If the history is already at capacity the function
 * does not add the new index and instead clamps the history cursor to the most recent entry.
 */
static void appendToHistory(uint16_t index) {
  if (HISTORY_CAP == 0) {
    return;
  }

  if (historySize < HISTORY_CAP) {
    history[historySize] = index;
    historySize++;
    historyPosition = historySize - 1;
    return;
  }

  // For now, just clamp at full; we can convert to a ring buffer later.
  historyPosition = historySize - 1;
}

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
 * @brief Enter the Updating application state and show the updating LED pattern.
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
 * @brief Powers down the device and enters deep sleep until the configured wakeup time.
 *
 * Turns off LEDs, schedules a timer-based wakeup after TIME_TO_SLEEP_S seconds, and starts deep sleep.
 */

static void enterSleep() {
  ledOff();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * US_PER_S);
  esp_deep_sleep_start();
}

/**
 * @brief Print the ASCII logo banner to the Serial console.
 */

static void renderLogo() {
  Serial.println(F(" /$$      /$$                     /$$                      "
                   "              "));
  Serial.println(F("| $$$    /$$$                    | $$                      "
                   "              "));
  Serial.println(F("| $$$$  /$$$$  /$$$$$$   /$$$$$$$| $$   /$$  /$$$$$$   "
                   "/$$$$$$  /$$   /$$"));
  Serial.println(F("| $$ $$/$$ $$ /$$__  $$ /$$_____/| $$  /$$/ /$$__  $$ "
                   "/$$__  $$| $$  | $$"));
  Serial.println(F("| $$  $$$| $$| $$  \\ $$| $$      | $$$$$$/ | $$$$$$$$| $$ "
                   " \\__/| $$  | $$"));
  Serial.println(F("| $$\\  $ | $$| $$  | $$| $$      | $$_  $$ | $$_____/| $$ "
                   "     | $$  | $$"));
  Serial.println(F("| $$ \\/  | $$|  $$$$$$/|  $$$$$$$| $$ \\  $$|  $$$$$$$| "
                   "$$      |  $$$$$$$"));
  Serial.println(F("|__/     |__/ \\______/  \\_______/|__/  \\__/ "
                   "\\_______/|__/       \\____  $$"));
  Serial.println(F("                                                           "
                   "     /$$  | $$"));
  Serial.println(F("                                                           "
                   "    |  $$$$$$/"));
  Serial.println(F("                                                           "
                   "     \\______/ "));
}

/**
 * @brief Render the application's title screen to the serial console.
 *
 * Prints the presentation header, the application title, and the ASCII logo with surrounding blank lines to the Serial output.
 */
static void renderTitleScreen() {
  Serial.println();
  Serial.println(F("Brown Bear Creative presents..."));
  Serial.println(F("The Bard's Assistant"));
  Serial.println();
  renderLogo();
  Serial.println();
}

/**
 * @brief Render a formatted insult block for a given insult index.
 *
 * Prints a framed block to Serial showing the insult text and optional
 * headers for the render reason and the user action. If no insults are
 * available or the index is out of range, a warning line is printed instead.
 *
 * @param index Index of the insult in the `insults` array (0 .. insultCount-1).
 * @param action Optional action label to display (e.g., Random, Next, Prev, None).
 * @param reason Header reason for the render (e.g., Boot, OperationStart, OperationComplete, UserTap).
 */
static void renderInsultAtIndex(uint16_t index, PendingAction action,
                                RenderReason reason) {
  if (insultCount == 0) {
    Serial.println(F("[WARN] No insults available."));
    return;
  }

  if (index >= insultCount) {
    Serial.print(F("[WARN] Invalid insult index: "));
    Serial.println(index);
    return;
  }

  const char *line = insults[index];

  Serial.println(F("────────────────────────────"));
  switch (reason) {
  case RenderReason::Boot:
    Serial.println(F("[Boot]"));
    break;
  case RenderReason::OperationStart:
    Serial.println(F("[Starting]"));
    break;
  case RenderReason::OperationComplete:
    Serial.println(F("[Done]"));
    break;
  case RenderReason::UserTap: // reserved for e-ink UX later
    Serial.println(F("[Tap]"));
    break;
  }

  switch (action) {
  case PendingAction::Random:
    Serial.println(F("(Random)"));
    break;
  case PendingAction::Next:
    Serial.println(F("(Next)"));
    break;
  case PendingAction::Prev:
    Serial.println(F("(Previous)"));
    break;
  case PendingAction::None:
    break;
  }

  Serial.println(line);
  Serial.println(F("────────────────────────────"));
}

// ───────────────── Work Orchestration ────────────

// Decide what work to do for the requested action.
/**
 * @brief Prepare pending work for the specified pending action and mark the operation as waiting when work is available.
 *
 * Depending on the action, this selects the next insult index to process and updates operation-related state:
 * - For `Random`: selects a new insult from the deck and marks the operation as a new insult.
 * - For `Next`: advances to the next history entry if available; otherwise selects a new insult from the deck and marks it new.
 * - For `Prev`: moves to the previous history entry if available.
 *
 * When work is selected, `operationPhase` is set to `OperationPhase::Waiting`, `pendingInsultIndex` is updated, and
 * `operationIsNewInsult` is updated to indicate whether the selected insult is newly drawn. If no work is available
 * for the requested action, no state changes are made and a diagnostic line may be printed to Serial.
 *
 * @param action The pending action to prepare work for (None, Random, Next, Prev).
 */
static void beginWorkFor(PendingAction action) {
  operationIsNewInsult = false;

  if (action == PendingAction::Random) {
    pendingInsultIndex = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;
    return;
  }

  if (action == PendingAction::Prev) {
    if (historySize == 0) {
      Serial.println(F("[Prev] No history yet."));
      return;
    }
    if (historyPosition == 0) {
      Serial.println(F("[Prev] Already at oldest entry."));
      return;
    }

    historyPosition--; // move back one
    pendingInsultIndex = history[historyPosition];
    operationPhase = OperationPhase::Waiting;
    return;
  }

  if (action == PendingAction::Next) {
    // If we’re not at the end of history, walk forward.
    if (historySize > 0 && historyPosition < historySize - 1) {
      historyPosition++;
      pendingInsultIndex = history[historyPosition];
      operationPhase = OperationPhase::Waiting;
      return;
    }

    // Otherwise, Next = “new insult from deck”
    pendingInsultIndex = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;
    return;
  }

  // No work for PendingAction::None here.
}

/**
 * @brief Initiates an operation for the specified pending action and transitions the app into the updating flow.
 *
 * Records the start time, prepares internal operation state, switches to the Updating state, and begins work for
 * the given action. If no work is available for the action, clears the pending action and returns the app to Idle.
 *
 * @param action The pending action to start (e.g., Random, Next, Prev).
 * @param now Current timestamp in milliseconds used to record when the operation started.
 */
static void startOperation(PendingAction action, uint32_t now) {
  pendingAction = action;
  operationPhase = OperationPhase::Idle; // will flip to Waiting if work exists
  operationIsNewInsult = false;
  operationStartedAt = now;

  enterUpdating();
  beginWorkFor(action);

  // If beginWorkFor didn’t set Waiting, there was nothing to do.
  if (operationPhase != OperationPhase::Waiting) {
    pendingAction = PendingAction::None;
    enterIdle(); // bounce straight back
  }
}

/**
 * @brief Finalizes a pending update operation when its work delay has elapsed.
 *
 * When the operation has finished (based on the provided timestamp and the mock
 * work duration), this function applies the pending action: it updates the
 * current insult index, updates the history according to the action taken,
 * renders the completed insult with the "OperationComplete" reason, resets
 * operation-related state, and transitions the application back to Idle.
 *
 * @param now Current time in milliseconds used to determine whether the operation has completed.
 */
static void pollOperation(uint32_t now) {
  if (operationPhase != OperationPhase::Waiting) {
    return;
  }

  if ((now - operationStartedAt) < MOCK_WORK_MS) {
    return;
  }

  const PendingAction completedAction = pendingAction;
  currentInsultIndex = pendingInsultIndex;

  // Maintain history semantics
  if (completedAction == PendingAction::Random) {
    // Always new
    appendToHistory(currentInsultIndex);
  } else if (completedAction == PendingAction::Next) {
    if (operationIsNewInsult) {
      // New entry at the end
      appendToHistory(currentInsultIndex);
    } else {
      // Pure forward navigation within existing history
      // historyPosition already moved in beginWorkFor
    }
  } else if (completedAction == PendingAction::Prev) {
    // Pure backward navigation; historyPosition already moved
  }

  // Render result
  renderInsultAtIndex(currentInsultIndex, completedAction,
                      RenderReason::OperationComplete);

  // Reset operation state
  pendingAction = PendingAction::None;
  operationPhase = OperationPhase::Idle;
  operationIsNewInsult = false;

  enterIdle();
}

/**
 * @brief Handle a button event and trigger state-appropriate actions.
 *
 * Processes a single button event: ignores `None` events; always handles the Sleep
 * button (currently only acknowledging hold events); and for Random/Next/Prev
 * buttons, only reacts when the application is in the Idle state — a Tap starts
 * the corresponding operation.
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
      startOperation(PendingAction::Random, now);
      break;
    case ButtonId::Next:
      Serial.println(F("[Next] Tap"));
      startOperation(PendingAction::Next, now);
      break;
    case ButtonId::Prev:
      Serial.println(F("[Prev] Tap"));
      startOperation(PendingAction::Prev, now);
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
  initDeck();

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  historySize = 0;
  historyPosition = 0;

  enterBoot();
  renderTitleScreen();

  if (PRINT_INSULT_ON_BOOT && insultCount > 0) {
    currentInsultIndex = drawFromDeck();
    appendToHistory(currentInsultIndex);
    renderInsultAtIndex(currentInsultIndex, PendingAction::Random,
                        RenderReason::Boot);
    // After boot splash + first insult, sit in Idle.
    enterIdle();
  }
}

/**
 * @brief Main application loop that polls inputs and advances the application state.
 *
 * Polls all four buttons, dispatches their events to the unified button handler,
 * and advances the high-level state machine. In Boot state, if no insult was
 * printed on boot and at least two seconds have passed since entering the state,
 * transitions to Idle. In Updating state, progresses the ongoing operation by
 * calling pollOperation. Idle state has no time-driven behavior.
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
    if (!PRINT_INSULT_ON_BOOT && (now - stateEnteredAt >= 2000)) {
      enterIdle();
    }
    break;

  case ApplicationState::Idle:
    // Nothing time-based; we only move because of button events.
    break;

  case ApplicationState::Updating:
    pollOperation(now);
    break;
  }
}