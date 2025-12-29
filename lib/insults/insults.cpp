#include "insults.h"
#include <Arduino.h>
#include <Preferences.h>

// Internal-only enums (not exposed in insults.h)
enum class RenderReason {
  Boot,
  OperationStart,
  OperationComplete,
  UserTap,
  Wake
};
enum class OperationPhase { Idle, Waiting };

// ───────────────── Module Configuration ─────────────────

// Non-volatile storage (NVS) namespace + magic marker for saved-state
// validation.
static constexpr const char *NVS_NS = "bards";
static constexpr uint32_t NVS_MAGIC = 0xBADC0FFE;

// Simulated “work” duration for operations (Random/Next/Prev).
static constexpr uint32_t MOCK_WORK_MS = 800;

// Source data (future: load from flash/SD/API)
static const char *const insults[] = {
    "You fight like a dairy farmer.",
    "You have the manners of a troll.",
    "I’ve spoken with sewer rats more polite than you.",
    "Oh look, both your weapons are tiny!",
};

static constexpr size_t insultCount = sizeof(insults) / sizeof(insults[0]);

// ───────────────── Persistent State (RTC) ─────────────────
//
// RTC_DATA_ATTR values survive deep sleep resets, but NOT power cycles.
// That’s fine for “fast resume” style state; we still persist to NVS for
// reliability across deeper resets / edge cases.

static RTC_DATA_ATTR uint16_t deck[insultCount] = {0};
static RTC_DATA_ATTR size_t deckPosition = 0;

static constexpr size_t HISTORY_CAP = insultCount;
static RTC_DATA_ATTR uint16_t history[HISTORY_CAP] = {0};

static RTC_DATA_ATTR size_t historyHead =
    0; // physical write index (next append)
static RTC_DATA_ATTR size_t historySize =
    0; // number of valid entries (0..HISTORY_CAP)
static RTC_DATA_ATTR size_t historyPosition =
    0; // logical cursor (0=oldest .. size-1=newest)

static RTC_DATA_ATTR uint16_t currentInsultIndex = 0;

// ───────────────── Operation State (RAM) ─────────────────

static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// Tracks whether Next produced a brand-new insult (vs just moving within
// history)
static bool operationIsNewInsult = false;

static uint32_t operationStartedAt = 0;
static uint16_t pendingInsultIndex = 0;

// ───────────────── Utilities ─────────────────

/**
 * @brief Populate the deck with indices and shuffle it, resetting draw
 * position.
 *
 * The deck provides a simple “no immediate repeats until deck exhausted”
 * pattern.
 */
static void initDeck() {
  for (size_t i = 0; i < insultCount; ++i) {
    deck[i] = static_cast<uint16_t>(i);
  }

  for (size_t i = insultCount - 1; i > 0; --i) {
    const long r = random(0, static_cast<long>(i + 1)); // 0..i
    const uint16_t tmp = deck[i];
    deck[i] = deck[r];
    deck[r] = tmp;
  }

  deckPosition = 0;
}

/**
 * @brief Draw the next insult index from the shuffled deck.
 *
 * If the deck is exhausted, it is reshuffled automatically.
 */
static uint16_t drawFromDeck() {
  if (insultCount == 0) {
    return 0;
  }

  if (deckPosition >= insultCount) {
    initDeck();
  }

  const uint16_t idx = deck[deckPosition];
  deckPosition++;
  return idx;
}

static size_t wrapIndex(size_t index, size_t mod) {
  if (mod == 0) {
    return 0;
  }
  return index % mod;
}

/**
 * @brief Physical index of the “oldest” entry in the ring buffer.
 *
 * historyHead points to the next write position, so the oldest is:
 *   head - size (with wrap).
 */
static size_t historyOldestPhysicalIndex() {
  if (HISTORY_CAP == 0) {
    return 0;
  }
  return wrapIndex(historyHead + HISTORY_CAP - historySize, HISTORY_CAP);
}

/**
 * @brief Read a history entry by logical position (0..historySize-1).
 *
 * @param logicalPos Logical cursor position (0=oldest, size-1=newest).
 * @param outIndex Receives the stored insult index.
 * @return true if found; false if history empty/out of range.
 */
static bool historyGetAtLogical(size_t logicalPos, uint16_t &outIndex) {
  if (HISTORY_CAP == 0 || historySize == 0) {
    return false;
  }
  if (logicalPos >= historySize) {
    return false;
  }

  const size_t oldest = historyOldestPhysicalIndex();
  const size_t physical = wrapIndex(oldest + logicalPos, HISTORY_CAP);
  outIndex = history[physical];
  return true;
}

/**
 * @brief Append an insult index to the displayed-history ring buffer.
 *
 * Ring-buffer behavior:
 * - If history is not full, historySize grows.
 * - If history is full, the oldest entry is overwritten.
 *
 * After appending, historyPosition is set to the newest entry.
 */
static void appendToHistory(uint16_t index) {
  if (HISTORY_CAP == 0) {
    return;
  }

  history[historyHead] = index;
  historyHead = wrapIndex(historyHead + 1, HISTORY_CAP);

  if (historySize < HISTORY_CAP) {
    historySize++;
  }

  historyPosition = historySize - 1;
}

// ───────────────── Rendering ─────────────────

static void renderLogo() {
  Serial.println(F(" /$$      /$$                     /$$                      "
                   "             "));
  Serial.println(F("| $$$    /$$$                    | $$                      "
                   "             "));
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

/**
 * @brief Render a single insult with a small “reason/action” header.
 *
 * This module prints to Serial today; later you can swap these prints
 * for display drawing calls without changing the higher-level flow.
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
  case RenderReason::Wake:
    Serial.println(F("[Wake]"));
    break;
  case RenderReason::OperationStart:
    Serial.println(F("[Starting]"));
    break;
  case RenderReason::OperationComplete:
    Serial.println(F("[Done]"));
    break;
  case RenderReason::UserTap:
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

// ───────────────── Persistence (NVS) ─────────────────

/**
 * @brief Load last-seen insult + history cursor from NVS.
 *
 * This is used on wake-from-sleep to restore exactly what the user last saw.
 * A magic marker + size checks are used to avoid applying incompatible data.
 */
static bool loadInsultsStateFromNvs(uint16_t &outIndex) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) {
    return false;
  }

  const uint32_t magic = prefs.getUInt("m", 0);
  if (magic != NVS_MAGIC) {
    prefs.end();
    return false;
  }

  const uint16_t savedCur = prefs.getUShort("cur", 0);

  const uint16_t savedHead = prefs.getUShort("hH", 0);
  const uint16_t savedSize = prefs.getUShort("hS", 0);
  const uint16_t savedPos = prefs.getUShort("hP", 0);

  const size_t expectedBytes = sizeof(history);
  const size_t gotBytes = prefs.getBytesLength("hist");
  if (gotBytes != expectedBytes) {
    prefs.end();
    return false;
  }

  const size_t readBytes = prefs.getBytes("hist", history, expectedBytes);
  prefs.end();

  if (readBytes != expectedBytes) {
    return false;
  }

  // Validate saved metadata against current compiled-in sizes.
  if (insultCount == 0) {
    return false;
  }

  const bool curValid = (savedCur < insultCount);
  const bool sizeValid = (savedSize <= HISTORY_CAP);
  const bool headValid = (savedHead < HISTORY_CAP);
  const bool posValid = (savedPos <= (savedSize == 0 ? 0 : (savedSize - 1)));

  if (!curValid || !sizeValid || !headValid || !posValid) {
    return false;
  }

  historyHead = savedHead;
  historySize = savedSize;
  historyPosition = savedPos;

  currentInsultIndex = savedCur;
  outIndex = savedCur;
  return true;
}

/**
 * @brief Persist current insult + history cursor to NVS before deep sleep.
 *
 * Called from main right before esp_deep_sleep_start().
 */
void insultsPersistForSleep() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    return;
  }

  prefs.putUInt("m", NVS_MAGIC);
  prefs.putUShort("cur", currentInsultIndex);
  prefs.putUShort("hH", static_cast<uint16_t>(historyHead));
  prefs.putUShort("hS", static_cast<uint16_t>(historySize));
  prefs.putUShort("hP", static_cast<uint16_t>(historyPosition));
  prefs.putBytes("hist", history, sizeof(history));
  prefs.end();
}

// ───────────────── Work Orchestration ─────────────────

/**
 * @brief Prepare internal state for a given user action.
 *
 * Chooses what index will be shown after the simulated work delay completes:
 * - Random always draws a new insult.
 * - Prev moves back within history if possible.
 * - Next moves forward within history, but draws a new insult if at the end.
 */
static bool beginWorkFor(PendingAction action) {
  operationIsNewInsult = false;

  if (action == PendingAction::Random) {
    pendingInsultIndex = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;
    return true;
  }

  if (action == PendingAction::Prev) {
    if (historySize == 0) {
      Serial.println(F("[Prev] No history yet."));
      return false;
    }
    if (historyPosition == 0) {
      Serial.println(F("[Prev] Already at oldest entry."));
      return false;
    }

    historyPosition--;
    if (!historyGetAtLogical(historyPosition, pendingInsultIndex)) {
      Serial.println(F("[Prev] History read failed."));
      return false;
    }

    operationPhase = OperationPhase::Waiting;
    return true;
  }

  if (action == PendingAction::Next) {
    if (historySize == 0) {
      // No history yet; treat Next like Random.
      pendingInsultIndex = drawFromDeck();
      operationIsNewInsult = true;
      operationPhase = OperationPhase::Waiting;
      return true;
    }

    if (historyPosition < historySize - 1) {
      // Still within history; move forward.
      historyPosition++;
      if (!historyGetAtLogical(historyPosition, pendingInsultIndex)) {
        Serial.println(F("[Next] History read failed."));
        return false;
      }
      operationPhase = OperationPhase::Waiting;
      return true;
    }

    // At newest entry; Next generates a new insult.
    pendingInsultIndex = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;
    return true;
  }

  return false;
}

/**
 * @brief Initialize the insults module and render the startup UI.
 *
 * - Always rebuilds the randomized deck.
 * - On cold boot: resets history, renders title, and optionally prints an
 * insult.
 * - On wake-from-sleep: attempts to restore from NVS and render the last
 * insult.
 *
 * @param printInsultOnBoot If true, prints an initial insult on cold boot.
 * @param wokeFromSleep If true, attempts NVS restore and renders [Wake] output.
 * @return true if an insult was rendered immediately; false otherwise.
 */
bool insultsInit(bool printInsultOnBoot, bool wokeFromSleep) {
  initDeck();

  if (!wokeFromSleep) {
    // Cold boot: reset history and show the splash/title.
    historyHead = 0;
    historySize = 0;
    historyPosition = 0;

    renderTitleScreen();

    if (printInsultOnBoot && insultCount > 0) {
      currentInsultIndex = drawFromDeck();
      appendToHistory(currentInsultIndex);
      renderInsultAtIndex(currentInsultIndex, PendingAction::Random,
                          RenderReason::Boot);
      return true;
    }

    return false;
  }

  // Wake path: restore last displayed insult/history if possible.
  uint16_t restoredIndex = 0;
  if (loadInsultsStateFromNvs(restoredIndex)) {
    renderInsultAtIndex(restoredIndex, PendingAction::None, RenderReason::Wake);
    return true;
  }

  // Fallback: no saved state; draw one and seed history so Next/Prev behave.
  if (insultCount == 0) {
    return false;
  }

  currentInsultIndex = drawFromDeck();
  historyHead = 0;
  historySize = 0;
  historyPosition = 0;
  appendToHistory(currentInsultIndex);

  renderInsultAtIndex(currentInsultIndex, PendingAction::None,
                      RenderReason::Wake);
  return true;
}

/**
 * @brief Start a mocked “operation” (Random/Next/Prev).
 *
 * This sets internal operation state and returns true if there is work to do.
 * The caller typically transitions the app into an Updating state only if true.
 */
bool insultsStartOperation(PendingAction action, uint32_t now) {
  pendingAction = action;
  operationPhase = OperationPhase::Idle;
  operationIsNewInsult = false;
  operationStartedAt = now;

  if (!beginWorkFor(action)) {
    pendingAction = PendingAction::None;
    operationPhase = OperationPhase::Idle;
    operationIsNewInsult = false;
    return false;
  }

  return true;
}

/**
 * @brief Advance the mocked operation while in Updating.
 *
 * Returns true exactly once when the operation completes, then resets internal
 * operation state back to Idle.
 */
bool insultsPoll(uint32_t now) {
  if (operationPhase != OperationPhase::Waiting) {
    return false;
  }

  if ((now - operationStartedAt) < MOCK_WORK_MS) {
    return false;
  }

  const PendingAction completedAction = pendingAction;
  currentInsultIndex = pendingInsultIndex;

  // Maintain history semantics:
  // - Random always appends
  // - Next appends only if it generated a new insult
  // - Prev does not append (cursor moved within beginWorkFor)
  if (completedAction == PendingAction::Random) {
    appendToHistory(currentInsultIndex);
  } else if (completedAction == PendingAction::Next) {
    if (operationIsNewInsult) {
      appendToHistory(currentInsultIndex);
    }
  }

  renderInsultAtIndex(currentInsultIndex, completedAction,
                      RenderReason::OperationComplete);

  pendingAction = PendingAction::None;
  operationPhase = OperationPhase::Idle;
  operationIsNewInsult = false;

  return true;
}
