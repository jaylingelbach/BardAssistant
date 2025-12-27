#include "button.h"
#include "led.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h> // esp_random()

// ─── Configuration ────────────────────────────────────────────────────

static Button sleepButton;
static Button randomButton;
static Button nextButton;
static Button prevButton;

static constexpr uint8_t PIN_SLEEP_BUTTON = 6;
static constexpr uint8_t PIN_RANDOM_BUTTON = 4;
static constexpr uint8_t PIN_NEXT_BUTTON = 5;
static constexpr uint8_t PIN_PREV_BUTTON = 7;

// Conversion factor for micro seconds to seconds
static constexpr uint64_t US_PER_S = 1000000ULL;

// Time ESP32 will go to sleep (in seconds)
static constexpr uint64_t TIME_TO_SLEEP_S = 5ULL;

// Mock delay to simulate “work”
static constexpr uint32_t MOCK_WORK_MS = 800;

// Optional: print something once at boot
static constexpr bool PRINT_INSULT_ON_BOOT = true;

// ─── Application States ───────────────────────────────────────────────
enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };
enum class PendingAction { None, Random, Next, Prev };
enum class OperationPhase { Idle, Waiting };
enum class RenderReason { Boot, OperationComplete, UserTap };

static ApplicationState currentState = ApplicationState::Boot;
static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// Boot timing: keeps LED blue for 2 seconds
static uint32_t stateEnteredAt = 0;
// Operation timing: decides when Updating is done
static uint32_t operationStartedAt = 0;

static constexpr ApplicationState INITIAL_STATE = ApplicationState::Boot;
static constexpr PendingAction INITIAL_PENDING_ACTION_STATE =
    PendingAction::None;

// Insult indices
static uint16_t pendingInsultIndex = 0;
static uint16_t currentInsultIndex = 0;
static uint16_t lastRenderedInsultIndex = 0;

// Tracking what last render was “for”
static RenderReason lastRenderedReason = RenderReason::Boot;
static PendingAction lastRenderedAction = PendingAction::None;

// A fixed list of string literals (stored in flash / read-only memory)
static const char *const insults[] = {
    "You fight like a dairy farmer.",
    "You have the manners of a troll.",
    "I’ve spoken with sewer rats more polite than you.",
};

static constexpr size_t insultCount = sizeof(insults) / sizeof(insults[0]);

// ─── Non-repeat “deck” + history ─────────────────────────────────────
static uint16_t deck[insultCount] = {0};
static size_t deckPosition = 0;

// history[] stores indices you’ve displayed (in order)
static constexpr size_t HISTORY_CAP = insultCount; // insult count
static uint16_t history[HISTORY_CAP] = {0};
static size_t historySize = 0;
static size_t historyPosition = 0;

// Random index helper to avoid casting
static uint16_t randomIndex(uint16_t upperExclusive) {
  return static_cast<uint16_t>(random(0, static_cast<long>(upperExclusive)));
}

// example calls
// pendingInsultIndex = randomIndex(static_cast<uint16_t>(insultCount));
// If within safe uint16_t range
// pendingInsultIndex = randomIndex(insultCount);
// pendingInsultIndex = randomIndex(insultCount + 1);

// ─── Forward Declarations ─────────────────────────────────────────────
static void startOperation(PendingAction action, uint32_t now);
static void beginWorkFor(PendingAction action);
static void pollOperation(uint32_t now);
static void renderInsultAtIndex(uint16_t index, PendingAction action,
                                RenderReason reason);

static void initDeck();
static void shuffleDeck();
// static uint16_t drawFromDeck();
static void appendToHistory(uint16_t index);

// ─── State Entry Functions ────────────────────────────────────────────
static void enterBoot() {
  ledShowBoot(); // Blue
  currentState = ApplicationState::Boot;
  stateEnteredAt = millis();
}

static void enterIdle() {
  ledShowIdle(); // Green
  currentState = ApplicationState::Idle;
  stateEnteredAt = millis();
}

static void enterUpdating() {
  ledShowUpdating(); // Yellow
  currentState = ApplicationState::Updating;
  stateEnteredAt = millis();
}

// ─── Sleep ────────────────────────────────────────────────────────────
static void enterSleep() {
  ledOff();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * US_PER_S);
  esp_deep_sleep_start();
}

// ─── Render ───────────────────────────────────────────────────────────
static void renderInsultAtIndex(uint16_t index, PendingAction action,
                                RenderReason reason) {
  if (insultCount == 0) {
    Serial.println("No insults to print");
    return;
  }

  if (index >= insultCount) {
    Serial.println("Invalid index");
    return;
  }

  Serial.print("Insult #");
  Serial.print(index);
  Serial.print(": ");
  Serial.println(insults[index]);

  lastRenderedInsultIndex = index;
  lastRenderedReason = reason;
  lastRenderedAction = action;
}

static void renderTitleScreen() {
  Serial.println("Brown Bear Creative Presents!");
  Serial.println("Vicious Mocker-er");
  Serial.println("The Bard's Assistant");
}

// ─── Deck + History Helpers ───────────────────────────────────────────
static void initDeck() {
  for (size_t index = 0; index < insultCount; index++) {
    deck[index] = static_cast<uint16_t>(index);
  }
  shuffleDeck();
  deckPosition = 0;
}

static void shuffleDeck() {
  if (insultCount <= 1)
    return;

  for (size_t index = insultCount - 1; index > 0; index--) {
    const size_t swapIndex = static_cast<size_t>(randomIndex(index + 1));
    const uint16_t tmp = deck[index];
    deck[index] = deck[swapIndex];
    deck[swapIndex] = tmp;
  }
}

static uint16_t drawFromDeck() {
  if (insultCount == 0)
    return 0;

  if (deckPosition >= insultCount) {
    // shuffle and reset position
    shuffleDeck();
    deckPosition = 0;
  }

  //   // get drawn card from deck position drawnCard = deck[deckPosition]
  const uint16_t drawn = deck[deckPosition];
  deckPosition++;
  return drawn;
}

static void appendToHistory(uint16_t index) {
  if (HISTORY_CAP == 0) // false;
    return;

  if (historySize < HISTORY_CAP) {
    history[historySize] = index;
    historySize++;
    historyPosition = historySize - 1;
    return;
  }

  // If we ever outgrow, we can turn this into a ring buffer.
  // For now (HISTORY_CAP == insultCount), we’ll just clamp.
  historyPosition = historySize - 1;
}

// ─── Operation Mocking ────────────────────────────────────────────────
static void beginWorkFor(PendingAction action) {
  if (action == PendingAction::Random) {
    pendingInsultIndex = drawFromDeck();
    operationPhase = OperationPhase::Waiting;
    return;
  }

  if (action == PendingAction::Prev) {
    if (historySize == 0) {
      pendingInsultIndex = currentInsultIndex;
      operationPhase = OperationPhase::Idle;
      return;
    }

    if (historyPosition > 0) {
      pendingInsultIndex = history[historyPosition - 1];
      operationPhase = OperationPhase::Waiting;
      return;
    }

    // Already at earliest entry
    pendingInsultIndex = history[0];
    operationPhase = OperationPhase::Idle;
    return;
  }

  if (action == PendingAction::Next) {
    // If user previously went “back”, Next should move forward in history if
    // possible.
    if (historySize > 0 && (historyPosition + 1) < historySize) {
      pendingInsultIndex = history[historyPosition + 1];
      operationPhase = OperationPhase::Waiting;
      return;
    }

    // Otherwise, Next means “new”
    // pendingInsultIndex =
    // Deck();
    operationPhase = OperationPhase::Waiting;
    return;
  }

  operationPhase = OperationPhase::Idle;
}

static void startOperation(PendingAction action, uint32_t now) {
  pendingAction = action;
  operationStartedAt = now;
  enterUpdating();
  beginWorkFor(action);
}

// Called every loop while Updating
static void pollOperation(uint32_t now) {
  if (operationPhase != OperationPhase::Waiting) {
    // Nothing to do → go idle
    pendingAction = PendingAction::None;
    enterIdle();
    return;
  }

  if ((now - operationStartedAt) < MOCK_WORK_MS) {
    return;
  }

  // “Work finished” → apply result
  const PendingAction completedAction = pendingAction;

  // Update indices + history cursor rules

  currentInsultIndex = pendingInsultIndex;

  // Render on completion
  renderInsultAtIndex(currentInsultIndex, completedAction,
                      RenderReason::OperationComplete);

  // Clear operation state
  pendingAction = PendingAction::None;
  operationPhase = OperationPhase::Idle;

  enterIdle();
}

// ─── Button Event Handler  ────────────────────────────────────────────
static void handleButtonEvent(ButtonId buttonId, ButtonEvent event,
                              uint32_t now) {
  if (event == ButtonEvent::None)
    return;

  switch (buttonId) {
  case ButtonId::Sleep: {
    if (event == ButtonEvent::HoldStart) {
      Serial.println("Sleep held: entering sleep (currently disabled)");
      // enterSleep();
    }
    break;
  }

  case ButtonId::Random: {
    if (event == ButtonEvent::Tap) {
      Serial.println("Random tapped");
      if (currentState == ApplicationState::Idle) {
        startOperation(PendingAction::Random, now);
      }
    }
    break;
  }

  case ButtonId::Next: {
    if (event == ButtonEvent::Tap) {
      Serial.println("Next tapped");
      if (currentState == ApplicationState::Idle) {
        startOperation(PendingAction::Next, now);
      }
    }
    break;
  }

  case ButtonId::Prev: {
    if (event == ButtonEvent::Tap) {
      Serial.println("Prev tapped");
      if (currentState == ApplicationState::Idle) {
        startOperation(PendingAction::Prev, now);
      }
    }
    break;
  }

  default:
    break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Booting...");

  // Seed RNG (ESP32 hardware RNG)
  randomSeed(static_cast<uint32_t>(esp_random()));

  ledInit();

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  currentState = INITIAL_STATE;
  pendingAction = INITIAL_PENDING_ACTION_STATE;
  operationPhase = OperationPhase::Idle;

  initDeck();

  // Optional: show one insult at boot (counts as “used”)
  // if (PRINT_INSULT_ON_BOOT && insultCount > 0) {
  //   currentInsultIndex = drawFromDeck();
  //   appendToHistory(currentInsultIndex);
  //   renderInsultAtIndex(currentInsultIndex, PendingAction::None,
  //                       RenderReason::Boot);
  // }

  // I think i'd like to print Something like Vicious Mocker-er Device on
  // boot. Or Bard's Assistant. Or something cool.
  renderTitleScreen();

  enterBoot();
}

void loop() {
  const uint32_t now = millis();

  const ButtonEvent sleepEvent = updateButton(sleepButton, now);
  if (sleepEvent != ButtonEvent::None) {
    handleButtonEvent(ButtonId::Sleep, sleepEvent, now);
  }

  const ButtonEvent randomEvent = updateButton(randomButton, now);
  if (randomEvent != ButtonEvent::None) {
    handleButtonEvent(ButtonId::Random, randomEvent, now);
  }

  const ButtonEvent nextEvent = updateButton(nextButton, now);
  if (nextEvent != ButtonEvent::None) {
    handleButtonEvent(ButtonId::Next, nextEvent, now);
  }

  const ButtonEvent prevEvent = updateButton(prevButton, now);
  if (prevEvent != ButtonEvent::None) {
    handleButtonEvent(ButtonId::Prev, prevEvent, now);
  }

  switch (currentState) {
  case ApplicationState::Boot:
    if (now - stateEnteredAt >= 2000) {
      enterIdle();
    }
    break;

  case ApplicationState::Idle:
    // Nothing to do here now — button handler starts operations.
    break;

  case ApplicationState::Updating:
    pollOperation(now);
    break;

  default:
    break;
  }
}
