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

// ───────────────── Helpers: deck / history ───────

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

// ───────────────── State Entry ───────────────────

static void enterBoot() {
  ledShowBoot();
  currentState = ApplicationState::Boot;
  stateEnteredAt = millis();
}

static void enterIdle() {
  ledShowIdle();
  currentState = ApplicationState::Idle;
  stateEnteredAt = millis();
}

static void enterUpdating() {
  ledShowUpdating();
  currentState = ApplicationState::Updating;
  stateEnteredAt = millis();
}

// ───────────────── Power / Sleep ─────────────────

static void enterSleep() {
  ledOff();
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * US_PER_S);
  esp_deep_sleep_start();
}

// ───────────────── Rendering (Serial stub) ───────

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

static void renderTitleScreen() {
  Serial.println();
  Serial.println(F("Brown Bear Creative presents..."));
  Serial.println(F("The Bard's Assistant"));
  Serial.println();
  renderLogo();
  Serial.println();
}

// In the future this will draw to E-Ink; for now it’s just Serial.
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
// Only sets operationPhase = Waiting when there is actual work.
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

// Called when we want to start doing “work” (Random/Next/Prev)
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

// Called every loop while in Updating
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

// ───────────────── Button Handling ───────────────

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

// ───────────────── Arduino Lifecycle ─────────────

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

void loop() {
  const uint32_t now = millis();

  // Poll buttons
  const ButtonEvent sleepEvent = updateButton(sleepButton, now);
  const ButtonEvent randomEvent = updateButton(randomButton, now);
  const ButtonEvent nextEvent = updateButton(nextButton, now);
  const ButtonEvent prevEvent = updateButton(prevButton, now);

  if (sleepEvent != ButtonEvent::None)
    handleButtonEvent(ButtonId::Sleep, sleepEvent, now);
  if (randomEvent != ButtonEvent::None)
    handleButtonEvent(ButtonId::Random, randomEvent, now);
  if (nextEvent != ButtonEvent::None)
    handleButtonEvent(ButtonId::Next, nextEvent, now);
  if (prevEvent != ButtonEvent::None)
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
