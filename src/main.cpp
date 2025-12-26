#include "button.h"
#include "led.h"
#include <Arduino.h>
#include <esp_sleep.h>

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

// ─── Application States ───────────────────────────────────────────────
enum class ApplicationState { Boot, Idle, Updating };
enum class ButtonId { Sleep, Random, Next, Prev };
enum class PendingAction { None, Random, Next, Prev };
enum class OperationPhase { Idle, Waiting, Done };

static ApplicationState currentState = ApplicationState::Boot;
static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// stateEnteredAt keeps the led indicator blue for a certain period of time.
static uint32_t stateEnteredAt = 0;
// operationStartedAt decides when Updating is done.
static uint32_t operationStartedAt = 0;

static constexpr PendingAction INITIAL_PENDING_ACTION_STATE =
    PendingAction::None;

static uint16_t pendingInsultIndex = 0;
static uint16_t currentInsultIndex = 0;

// A fixed list of string literals (stored in flash / read-only memory)
static const char *const insults[] = {
    "You fight like a dairy farmer.",
    "You have the manners of a troll.",
    "I’ve spoken with sewer rats more polite than you.",
};

static constexpr size_t insultCount = sizeof(insults) / sizeof(insults[0]);

// ─── Forward Declarations ─────────────────────────────────────────────
static void startOperation(PendingAction action, uint32_t now);

// ─── State Entry Functions ────────────────────────────────────────────
void enterBoot() {
  ledShowBoot(); // Blue
  currentState = ApplicationState::Boot;
  stateEnteredAt = millis();
}

void enterIdle() {
  ledShowIdle(); // Green
  currentState = ApplicationState::Idle;
  stateEnteredAt = millis();
}

void enterUpdating() {
  ledShowUpdating(); // Yellow
  currentState = ApplicationState::Updating;
  stateEnteredAt = millis();
}

// ─── Sleep  ───────────────────────────────────────────────────────────
void enterSleep() {
  ledOff();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * US_PER_S);
  esp_deep_sleep_start();
}

// ─── Button Event Handler  ────────────────────────────────────────────
static void handleButtonEvent(ButtonId buttonId, ButtonEvent event,
                              uint32_t now) {
  switch (buttonId) {
  case ButtonId::Sleep: {
    if (event == ButtonEvent::HoldStart) {
      Serial.println("Sleep button held: entering sleep (mock disabled)");
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

// ─── Operation Mocking ───────────────────────────────────────────────

// Set up “work context” for the chosen action.
// Must put the operation into a state that pollOperation() can finish.
static void beginWorkFor(PendingAction action) {
  // Decide what “target index” should be for each action
  if (action == PendingAction::Random) {
    // Pick a random index (0..insultCount-1)
    pendingInsultIndex =
        static_cast<uint16_t>(random(0, static_cast<long>(insultCount)));

    // Optional: avoid repeating the same insult twice in a row
    if (insultCount > 1 && pendingInsultIndex == currentInsultIndex) {
      pendingInsultIndex =
          static_cast<uint16_t>((pendingInsultIndex + 1) % insultCount);
    }

    operationPhase = OperationPhase::Waiting;
    return;
  }

  if (action == PendingAction::Next) {
    pendingInsultIndex = static_cast<uint16_t>(
        (static_cast<size_t>(currentInsultIndex) + 1) % insultCount);
    operationPhase = OperationPhase::Waiting;
    return;
  }

  if (action == PendingAction::Prev) {
    const size_t current = static_cast<size_t>(currentInsultIndex);
    const size_t prev = (current + insultCount - 1) % insultCount;
    pendingInsultIndex = static_cast<uint16_t>(prev);
    operationPhase = OperationPhase::Waiting;
    return;
  }

  // None -> nothing to do
  operationPhase = OperationPhase::Idle;
}

// Start operation (called from Idle only)
static void startOperation(PendingAction action, uint32_t now) {
  pendingAction = action;
  operationStartedAt = now;
  enterUpdating();
  beginWorkFor(action);
}

// Called every loop while Updating
static void pollOperation(uint32_t now) {
  if (operationPhase != OperationPhase::Waiting) {
    return;
  }

  if ((now - operationStartedAt) < MOCK_WORK_MS) {
    return;
  }

  // “Work finished” → apply result
  currentInsultIndex = pendingInsultIndex;

  Serial.print("Selected insult #");
  Serial.print(currentInsultIndex);
  Serial.print(": ");
  Serial.println(insults[currentInsultIndex]);

  // Clear operation state
  pendingAction = PendingAction::None;
  operationPhase = OperationPhase::Idle;

  enterIdle();
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Booting...");

  ledInit();

  buttonInit(sleepButton, PIN_SLEEP_BUTTON);
  buttonInit(randomButton, PIN_RANDOM_BUTTON);
  buttonInit(nextButton, PIN_NEXT_BUTTON);
  buttonInit(prevButton, PIN_PREV_BUTTON);

  pendingAction = INITIAL_PENDING_ACTION_STATE;
  operationPhase = OperationPhase::Idle;

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
    // Nothing here now — taps start operations inside handleButtonEvent()
    break;

  case ApplicationState::Updating:
    pollOperation(now);
    break;

  default:
    break;
  }
}
