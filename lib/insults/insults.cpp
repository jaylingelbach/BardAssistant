#include "insults.h"

#include <Arduino.h>

// private to insults - note: no static not in insults.h
enum class RenderReason { Boot, OperationStart, OperationComplete, UserTap };
enum class OperationPhase { Idle, Waiting };

static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// “Is the thing we’re working on a brand-new insult or just history
// navigation?”
static bool operationIsNewInsult = false;

// Timing
static uint32_t operationStartedAt = 0;

// Mock work duration (kept inside this module to avoid leaking main config).
static constexpr uint32_t MOCK_WORK_MS = 800;

// Source data (future: swap this to a table from flash or SD)
static const char *const insults[] = {
    "You fight like a dairy farmer.",
    "You have the manners of a troll.",
    "I’ve spoken with sewer rats more polite than you.",
    "Oh look, both your weapons are tiny!",
};

static constexpr size_t insultCount = sizeof(insults) / sizeof(insults[0]);

// Random “deck” of indices 0..insultCount-1
static uint16_t deck[insultCount] = {0};
static size_t deckPosition = 0;

// History: sequence of actually displayed insults (ring buffer)
//
// We store a fixed-size rolling window of the most recent HISTORY_CAP entries.
// - historyHead: where the next write goes (wraps around)
// - historySize: how many valid entries exist (0..HISTORY_CAP)
// - historyPosition: logical cursor for browsing (0 = oldest, size-1 = newest)
static constexpr size_t HISTORY_CAP = insultCount;
static uint16_t history[HISTORY_CAP] = {0};

static size_t historyHead = 0;     // write index (next append)
static size_t historySize = 0;     // how many are valid
static size_t historyPosition = 0; // logical cursor (oldest..newest)

// Current index being shown
static uint16_t currentInsultIndex = 0;
static uint16_t pendingInsultIndex = 0;

/**
 * @brief Populate the deck with indices for available insults, randomize their
 * order, and reset the draw position.
 *
 * Uses the global insultCount to determine the number of entries, fills the
 * deck with values 0..insultCount-1, randomizes the deck order, and sets
 * deckPosition to 0 so the next draw starts from the beginning of the shuffled
 * deck.
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
 * @return uint16_t Index of the next insult in the range [0, insultCount - 1],
 * or `0` if `insultCount == 0`.
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
 * @brief Wrap a size_t index into the range [0, mod-1].
 *
 * @param index Index that may be outside the range.
 * @param mod Modulus (buffer capacity). If mod is 0, returns 0.
 * @return Wrapped index within the buffer.
 */
static size_t wrapIndex(size_t index, size_t mod) {
  if (mod == 0) {
    return 0;
  }
  return index % mod;
}

/**
 * @brief Compute the physical array index of the oldest entry in the ring.
 *
 * When not full, the oldest entry is at (historyHead - historySize) wrapped.
 * When full, this points at the element that will be overwritten next.
 *
 * @return Physical index into `history[]` of the oldest entry.
 */
static size_t historyOldestPhysicalIndex() {
  if (HISTORY_CAP == 0) {
    return 0;
  }

  // (historyHead + CAP - historySize) % CAP
  return wrapIndex(historyHead + HISTORY_CAP - historySize, HISTORY_CAP);
}

/**
 * @brief Get a stored history value by logical position (0..historySize-1).
 *
 * Logical position 0 means "oldest", and historySize-1 means "newest".
 *
 * @param logicalPos Logical position into history (0..historySize-1).
 * @param outIndex Output insult index if present.
 * @return true if found; false if out of range or empty.
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
 * @brief Appends an insult index to the displayed-history buffer.
 *
 * Adds the provided insult index to history and advances the history cursor to
 * the newest entry.
 *
 * @param index Index of the insult to append (expected to be a valid index into
 * the insults array).
 *
 * Ring-buffer behavior:
 * - If history is not full, append grows historySize.
 * - If history is full, the oldest entry is overwritten.
 */
static void appendToHistory(uint16_t index) {
  if (HISTORY_CAP == 0) {
    return;
  }

  // Write at the head (overwrite if full).
  history[historyHead] = index;

  // Advance head.
  historyHead = wrapIndex(historyHead + 1, HISTORY_CAP);

  // Grow size until full.
  if (historySize < HISTORY_CAP) {
    historySize++;
  }

  // Snap cursor to newest entry.
  historyPosition = historySize - 1;
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
 * Prints the presentation header, the application title, and the ASCII logo
 * with surrounding blank lines to the Serial output.
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
 * @param action Optional action label to display (e.g., Random, Next, Prev,
 * None).
 * @param reason Header reason for the render (e.g., Boot, OperationStart,
 * OperationComplete, UserTap).
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
 * @brief Prepare pending work for the specified pending action and mark the
 * operation as waiting when work is available.
 *
 * Depending on the action, this selects the next insult index to process and
 * updates operation-related state:
 * - For `Random`: selects a new insult from the deck and marks the operation as
 * a new insult.
 * - For `Next`: advances to the next history entry if available; otherwise
 * selects a new insult from the deck and marks it new.
 * - For `Prev`: moves to the previous history entry if available.
 *
 * When work is selected, `operationPhase` is set to `OperationPhase::Waiting`,
 * `pendingInsultIndex` is updated, and `operationIsNewInsult` is updated to
 * indicate whether the selected insult is newly drawn. If no work is available
 * for the requested action, no state changes are made and a diagnostic line may
 * be printed to Serial.
 *
 * @param action The pending action to prepare work for (None, Random, Next,
 * Prev).
 * @return true if pending work was prepared; false otherwise.
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

    historyPosition--; // move back one (logical)
    if (!historyGetAtLogical(historyPosition, pendingInsultIndex)) {
      Serial.println(F("[Prev] History read failed."));
      return false;
    }

    operationPhase = OperationPhase::Waiting;
    return true;
  }

  if (action == PendingAction::Next) {
    if (historySize == 0) {
      // No history yet: treat Next as new insult.
      pendingInsultIndex = drawFromDeck();
      operationIsNewInsult = true;
      operationPhase = OperationPhase::Waiting;
      return true;
    }

    // If we’re not at the end of history, walk forward.
    if (historyPosition < historySize - 1) {
      historyPosition++; // logical
      if (!historyGetAtLogical(historyPosition, pendingInsultIndex)) {
        Serial.println(F("[Next] History read failed."));
        return false;
      }
      operationPhase = OperationPhase::Waiting;
      return true;
    }

    // Otherwise, Next = “new insult from deck”
    pendingInsultIndex = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;
    return true;
  }

  // No work for PendingAction::None here.
  return false;
}

bool insultsInit(bool printInsultOnBoot) {
  initDeck();
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

bool insultsStartOperation(PendingAction action, uint32_t now) {
  pendingAction = action;
  operationPhase = OperationPhase::Idle; // will flip to Waiting if work exists
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
 * @brief Finalizes a pending update operation when its work delay has elapsed.
 *
 * When the operation has finished (based on the provided timestamp and the mock
 * work duration), this function applies the pending action: it updates the
 * current insult index, updates the history according to the action taken,
 * renders the completed insult with the "OperationComplete" reason, and resets
 * operation-related state.
 *
 * @param now Current time in milliseconds used to determine whether the
 * operation has completed.
 * @return true when the operation completes; false otherwise.
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

  return true;
}
