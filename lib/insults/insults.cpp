#include "insults.h"
#include "persist_keys.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <string>
#include <vector>

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
static constexpr uint32_t NVS_MAGIC = 0xBADC0FFE;

// Simulated “work” duration for operations (Random/Next/Prev).
static constexpr uint32_t MOCK_WORK_MS = 800;

// ───────────────── Module State ─────────────────

// std::vector<std::string> insults;
std::vector<DeckEntry> insults;

// ───────────────── Persistent State (RTC) ─────────────────
//
// RTC_DATA_ATTR values survive deep sleep resets, but NOT power cycles.
// That’s fine for “fast resume” style state; we still persist to NVS for
// reliability across deeper resets / edge cases.

// static uint16_t deck[insultCount] = {0};
static std::vector<uint16_t> deck;

static size_t deckPosition = 0;

static constexpr size_t HISTORY_CAP = 50;

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

/**
 * @brief Loads insult entries from a JSON array stored in LittleFS.
 *
 * @param path Path to the JSON file.
 * @return std::vector<DeckEntry> Parsed entries, or an empty vector if the file cannot be opened or parsed.
 */

static std::vector<DeckEntry> readJsonFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("[readJsonFile] Failed to open file");
    return {};
  }
  JsonDocument doc;
  std::vector<DeckEntry> deckEntries;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.println("[readJsonFle] Invalid JSON");
    return deckEntries;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject entry : arr) {
    uint32_t id = entry["id"];
    const char *text = entry["text"];
    const char *source = entry["source"];
    deckEntries.emplace_back(id, text, source);
  }
  Serial.println("deckEntries size: ");
  Serial.println(deckEntries.size());
  return deckEntries;
}

/**
 * @brief Serializes a deck to a JSON file with backup and failure recovery.
 *
 * @param path Destination file path.
 * @param deck Entries to serialize.
 * @return `true` if the file is saved successfully, `false` otherwise.
 */
static bool saveJsonFile(const char *path, const std::vector<DeckEntry> &deck) {
  // Write to a temp file first so the primary file is never truncated before
  // we know serialization succeeded.
  String tmpPath = String(path) + ".tmp";
  String bakPath = String(path) + ".bak";

  File file = LittleFS.open(tmpPath.c_str(), "w");
  if (!file) {
    Serial.println("[saveJsonFile] Failed to open temp file for writing");
    return false;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const DeckEntry &card : deck) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = card.id;
    obj["text"] = card.text;
    obj["source"] = card.source;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println(F("[saveJsonFile] Failed to serialize JSON to temp file"));
    file.close();
    LittleFS.remove(tmpPath.c_str());
    return false;
  }
  file.close();

  // Keep the previous file as a backup before replacing it.
  if (LittleFS.exists(path)) {
    LittleFS.remove(bakPath.c_str());
    if (!LittleFS.rename(path, bakPath.c_str())) {
      Serial.println(F("[saveJsonFile] Failed to back up existing file"));
      LittleFS.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!LittleFS.rename(tmpPath.c_str(), path)) {
    Serial.println(F("[saveJsonFile] Failed to rename temp file to primary"));
    // Attempt to restore the backup so the deck isn't lost.
    if (LittleFS.exists(bakPath.c_str())) {
      LittleFS.rename(bakPath.c_str(), path);
    }
    return false;
  }

  return true;
}

/**
 * @brief Generates the next available insult identifier.
 *
 * @return uint32_t One greater than the highest identifier in the loaded insult collection.
 */
static uint32_t generateNextId() {
  uint32_t highestId = 0;

  for (const DeckEntry &insult : insults) {
    if (insult.id > highestId) {
      highestId = insult.id;
    }
  }

  return highestId + 1;
}

// ───────────────── Public Data Access ─────────────────

/**
 * @brief Provides access to all loaded insults.
 *
 * @return const std::vector<DeckEntry>& Reference to the loaded insult collection.
 */
const std::vector<DeckEntry> &insultsGetAll() { return insults; }

// ───────────────── Deck Mechanics ─────────────────

/**
 * @brief Populate the deck with indices and shuffle it, resetting draw
 * position.
 *
 * The deck provides a simple “no immediate repeats until deck exhausted”
 * pattern.
 */
static void initDeck() {
  deck.resize(insults.size());
  if (insults.empty()) {
    return;
  }
  for (size_t i = 0; i < insults.size(); ++i) {
    deck[i] = static_cast<uint16_t>(i);
  }

  for (size_t i = insults.size() - 1; i > 0; --i) {
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
 * Rebuilds the deck when all entries have been drawn.
 *
 * @return uint16_t The selected insult index, or 0 when no insults are loaded.
 */
static uint16_t drawFromDeck() {
  if (insults.size() == 0) {
    return 0;
  }

  if (deckPosition >= insults.size()) {
    initDeck();
  }

  const uint16_t idx = deck[deckPosition];
  deckPosition++;
  return idx;
}

/**
 * @brief Wraps an index within a modulus range.
 *
 * @param index Index to wrap.
 * @param mod Modulus defining the range; zero returns zero.
 * @return size_t Remainder of index divided by mod, or zero when mod is zero.
 */

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
 * @brief Displays the selected insult with its action and rendering reason.
 *
 * @param index Index of the insult to display.
 * @param action Action associated with the insult.
 * @param reason Reason the insult is being rendered.
 */
static void renderInsultAtIndex(uint16_t index, PendingAction action,
                                RenderReason reason) {
  if (insults.size() == 0) {
    Serial.println(F("[WARN] No insults available."));
    return;
  }

  if (index >= insults.size()) {
    Serial.print(F("[WARN] Invalid insult index: "));
    Serial.println(index);
    return;
  }

  const char *line = insults[index].text.c_str();

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
 * @brief Restores the current insult and navigation history from NVS.
 *
 * Validates the persisted metadata and active history entries against the
 * currently loaded insult collection before updating the in-memory state.
 *
 * @param[out] outIndex Receives the restored current insult index.
 * @return `true` if valid state was restored, `false` otherwise.
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

  // Validate saved metadata against current deck size.
  if (insults.size() == 0) {
    return false;
  }

  const bool curValid = (savedCur < insults.size());
  const bool sizeValid = (savedSize <= HISTORY_CAP);
  const bool headValid = (savedHead < HISTORY_CAP);
  const bool posValid = (savedPos <= (savedSize == 0 ? 0 : (savedSize - 1)));

  if (!curValid || !sizeValid || !headValid || !posValid) {
    return false;
  }

  // Validate every active history entry against the current deck size.
  // If insults.json changed since last sleep, stale indices could be out of
  // range.
  const size_t oldest =
      wrapIndex(savedHead + HISTORY_CAP - savedSize, HISTORY_CAP);
  for (size_t i = 0; i < savedSize; i++) {
    const size_t physical = wrapIndex(oldest + i, HISTORY_CAP);
    if (history[physical] >= insults.size()) {
      return false;
    }
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
 * @brief Initializes the insult collection, deck, history, and startup state.
 *
 * On cold boot, resets history and renders the title screen. On wake from
 * sleep, restores persisted state when available or seeds history with a
 * newly drawn insult without rendering it.
 *
 * @param printInsultOnBoot Whether to draw and render an insult during cold boot.
 * @param wokeFromSleep Whether to restore state from sleep persistence.
 * @return true if an insult is rendered during initialization, false otherwise.
 */
bool insultsInit(bool printInsultOnBoot, bool wokeFromSleep) {
  insults = readJsonFile("/insults.json");

  initDeck();

  if (!wokeFromSleep) {
    // Cold boot: reset history and show the splash/title.
    historyHead = 0;
    historySize = 0;
    historyPosition = 0;

    renderTitleScreen();

    if (printInsultOnBoot && insults.size() > 0) {
      currentInsultIndex = drawFromDeck();
      appendToHistory(currentInsultIndex);
      renderInsultAtIndex(currentInsultIndex, PendingAction::Random,
                          RenderReason::Boot);
      return true;
    }

    return false;
  }

  // Wake from deep sleep: restore state, but don't render to avoid flash
  uint16_t unusedRestoredIndex = 0;
  if (loadInsultsStateFromNvs(unusedRestoredIndex)) {
    Serial.println("[Wake] restored insult state");
    return false; // nothing rendered
  }

  // Fallback: no saved state; draw one and seed history so Next/Prev behave.
  if (insults.size() == 0) {
    return false;
  }

  historyHead = 0;
  historySize = 0;
  historyPosition = 0;
  currentInsultIndex = drawFromDeck();
  appendToHistory(currentInsultIndex);

  Serial.println("[Wake] no saved state; seeded first insult");
  return false;
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
 * @brief Polls the pending insult operation and completes it when its duration has elapsed.
 *
 * Updates the current insult, records the completed action in history, renders the result,
 * and resets the operation to idle.
 *
 * @param now Current time in milliseconds.
 * @return true if an operation completed during this call, false otherwise.
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

/**
 * @brief Determines whether any insults are loaded.
 *
 * @return `true` if at least one insult is loaded, `false` otherwise.
 */

bool insultsHasAny() { return insults.size() > 0; }

/**
 * @brief Retrieves the index of the current insult.
 *
 * @return uint16_t Current insult index.
 */
uint16_t insultsGetCurrentIndex() { return currentInsultIndex; }

/**
 * @brief Gets the text of the current insult.
 *
 * @return const char* The current insult text, or a status message when no valid insult is selected.
 */
const char *insultsGetCurrentText() {
  if (insults.size() == 0)
    return "No insults";
  if (currentInsultIndex >= insults.size())
    return "Invalid insult";
  return insults[currentInsultIndex].text.c_str();
}

/**
 * @brief Adds a user-sourced insult to the collection and persists it.
 *
 * @param text Text of the insult to create.
 * @return CreateEntryResult indicating whether the entry was saved successfully and containing the created entry.
 */

CreateEntryResult createInsult(std::string text) {
  // 1. Generate ID
  uint32_t id = generateNextId();

  // 2. Create Deck Entry
  DeckEntry newEntry{id, text, "user"};
  // 3. Add to in-memory vector
  insults.emplace_back(newEntry);

  if (saveJsonFile("/insults.json", insults)) {
    initDeck();
    // saved successfully
    Serial.println("[createInsult] JSON file updated successfully!");
    return {true, newEntry};
  } else {
    // save failed
    Serial.println("[createInsult] error saving insult");
    insults.pop_back();
    return {false, newEntry};
  }
}