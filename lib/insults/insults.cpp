#include "insults.h"
#include "persist_keys.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

// ============================================================================
// INTERNAL TYPES
// ============================================================================

enum class RenderReason {
  Boot,
  OperationStart,
  OperationComplete,
  UserTap,
  Wake,
  Deleted
};

enum class OperationPhase { Idle, Waiting };

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================

// -----------------------------------------------------------------------------
// Persistent state configuration
// -----------------------------------------------------------------------------

// Non-volatile storage (NVS) namespace + magic marker for saved-state
// validation.
static constexpr uint32_t NVS_MAGIC = 0xBADC0FFE;

// -----------------------------------------------------------------------------
// Operation configuration
// -----------------------------------------------------------------------------

// Minimum duration for Random/Next/Prev operations, keeping the LED active
// while the e-ink display refreshes.
static constexpr uint32_t OPERATION_DURATION_MS = 800;

// -----------------------------------------------------------------------------
// History configuration
// -----------------------------------------------------------------------------

static constexpr size_t HISTORY_CAP = 50;

// ============================================================================
// MODULE STATE
// ============================================================================

// -----------------------------------------------------------------------------
// Deck data
// -----------------------------------------------------------------------------

std::vector<DeckEntry> insults;

// -----------------------------------------------------------------------------
// Active deck state
// -----------------------------------------------------------------------------

static std::vector<uint32_t> deck;
static size_t deckPosition = 0;

// -----------------------------------------------------------------------------
// Operation state
// -----------------------------------------------------------------------------

static PendingAction pendingAction = PendingAction::None;
static OperationPhase operationPhase = OperationPhase::Idle;

// Tracks whether Next produced a brand-new insult (vs just moving within
// history).
static bool operationIsNewInsult = false;

static uint32_t operationStartedAt = 0;
static uint32_t pendingInsultId = 0;

// ============================================================================
// PERSISTENT STATE (RTC)
// ============================================================================
//
// RTC_DATA_ATTR values survive deep sleep resets, but NOT power cycles.
// That’s fine for “fast resume” style state; we still persist to NVS for
// reliability across deeper resets / edge cases.
//
// If you add more state that should survive deep sleep, this is where it goes.
// ============================================================================

static RTC_DATA_ATTR uint32_t history[HISTORY_CAP] = {0};

static RTC_DATA_ATTR size_t historyHead =
    0; // physical write index (next append), where the newest history entry is
       // stored physically.

static RTC_DATA_ATTR size_t historySize =
    0; // number of valid entries (0..HISTORY_CAP), how many valid entries are
       // in history

static RTC_DATA_ATTR size_t historyPosition =
    0; // logical cursor (0=oldest .. size-1=newest), where you currently are
       // when navigating Prev/Next

static RTC_DATA_ATTR uint32_t currentInsultId = 0;

// ============================================================================
// JSON / FILE STORAGE HELPERS
// ============================================================================
//
// These functions know how to read/write JSON files.
//
// They should NOT contain deck behavior, HTTP behavior, or UI behavior.
// If you add another JSON-backed deck in the future, this is where shared
// file-level functionality belongs.
// ============================================================================

/**
 * @brief Loads insult entries from a JSON array stored in LittleFS.
 *
 * @param path Path to the JSON file.
 * @return std::vector<DeckEntry> Parsed entries, or an empty vector if the file
 * cannot be opened or parsed.
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

    if (LittleFS.exists(bakPath.c_str())) {
      LittleFS.remove(bakPath.c_str());
    }

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

// ============================================================================
// DECK ENTRY HELPERS
// ============================================================================
//
// These functions operate on individual DeckEntry objects.
//
// This is where helpers such as:
// - find by ID
// - generate ID
// - validate entry data
//
// should live.
// ============================================================================

/**
 * @brief Generates the next available insult identifier.
 *
 * @return uint32_t One greater than the highest identifier in the loaded insult
 * collection.
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

/**
 * @brief Finds an insult by its persistent identifier.
 *
 * @param entryId Identifier to locate.
 * @return An iterator to the matching entry, or `insults.end()` when no entry
 * matches.
 */
static std::vector<DeckEntry>::iterator findInsultById(uint32_t entryId) {

  auto it = std::find_if(
      insults.begin(), insults.end(),
      [entryId](const DeckEntry &entry) { return entry.id == entryId; });

  if (it != insults.end()) {
    return it;
  }

  return insults.end();
}

// ============================================================================
// PUBLIC DATA ACCESS
// ============================================================================
//
// These functions expose read-only access to the insult collection without
// exposing the vector itself for modification.
// ============================================================================

/**
 * @brief Provides access to all loaded insults.
 *
 * @return const std::vector<DeckEntry>& Reference to the loaded insult
 * collection.
 */
const std::vector<DeckEntry> &insultsGetAll() { return insults; }

// ============================================================================
// DECK MECHANICS
// ============================================================================
//
// This section is concerned with the playable/randomized deck.
//
// IMPORTANT:
// `insults` = the actual persistent collection.
//
// `deck` = the shuffled collection of ids from `insults`.
//
// CRUD operations may need to interact with this section when the collection
// changes.
// ============================================================================

/**
 * @brief Populate the deck with IDs and shuffle it, resetting draw
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
    deck[i] = insults[i].id;
  }

  for (size_t i = insults.size() - 1; i > 0; --i) {

    const long r = random(0, static_cast<long>(i + 1)); // 0..i

    const uint32_t tmp = deck[i];
    deck[i] = deck[r];
    deck[r] = tmp;
  }

  deckPosition = 0;
}

/**
 * @brief Draws the next insult ID from the shuffled deck.
 *
 * Rebuilds the deck when all entries have been drawn.
 *
 * @return uint32_t The selected insult id, or 0 when no insults are loaded.
 */
static uint32_t drawFromDeck() {

  if (insults.size() == 0) {
    return 0;
  }

  if (deckPosition >= insults.size()) {
    // Remember the last drawn ID so we can avoid an immediate repeat.
    const uint32_t lastId =
        (deckPosition > 0 && deck.size() > 0) ? deck[deckPosition - 1] : 0;

    initDeck();

    // If the new deck's first entry matches the last drawn, swap it with the
    // second entry (when there are at least 2 insults).
    if (deck.size() >= 2 && deck[0] == lastId) {
      const uint32_t tmp = deck[0];
      deck[0] = deck[1];
      deck[1] = tmp;
    }
  }

  const uint32_t id = deck[deckPosition];

  deckPosition++;

  return id;
}

// ============================================================================
// HISTORY MECHANICS
// ============================================================================
//
// Previous / Next navigation lives here.
//
// This section should remain concerned with navigation history rather than
// persistence or HTTP.
// ============================================================================

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
 * @param outId Receives the stored insult ID.
 * @return true if found; false if history empty/out of range.
 */
static bool historyGetAtLogical(size_t logicalPos, uint32_t &outId) {

  if (HISTORY_CAP == 0 || historySize == 0) {
    return false;
  }

  if (logicalPos >= historySize) {
    return false;
  }

  const size_t oldest = historyOldestPhysicalIndex();
  const size_t physical = wrapIndex(oldest + logicalPos, HISTORY_CAP);

  outId = history[physical];

  return true;
}

/**
 * @brief Append an insult ID to the displayed-history ring buffer.
 *
 * Ring-buffer behavior:
 * - If history is not full, historySize grows.
 * - If history is full, the oldest entry is overwritten.
 *
 * After appending, historyPosition is set to the newest entry.
 */
static void appendToHistory(uint32_t id) {

  if (HISTORY_CAP == 0) {
    return;
  }

  history[historyHead] = id;

  historyHead = wrapIndex(historyHead + 1, HISTORY_CAP);

  if (historySize < HISTORY_CAP) {
    historySize++;
  }

  historyPosition = historySize - 1;
}

/**
 * @brief Removes every occurrence of an insult ID from navigation history.
 *
 * Rebuilds the ring buffer and moves its cursor to the current insult when it
 * remains in history, or to the newest remaining entry otherwise. The cursor
 * resets to zero when history becomes empty.
 *
 * @param id Insult identifier to remove.
 * @return `true` if the identifier was present, or `false` if history was
 * unchanged.
 */
static bool removeFromHistory(uint32_t id) {
  // 1. Find the ID in the logical history and record its position.
  bool isHistoryIdFound = false;
  size_t deletedLogicalPosition;

  for (size_t i = 0; i < historySize; ++i) {
    uint32_t historyId;

    // Get the ID at this logical history position.
    if (historyGetAtLogical(i, historyId)) {

      // Check whether this is the ID being deleted.
      if (historyId == id) {

        // Record where the deleted ID is located in logical history.
        deletedLogicalPosition = i;
        isHistoryIdFound = true;

        // The ID was found, so there is no need to keep searching.
        break;
      }
    }
  }

  // 2. If the ID wasn't found in history, there is nothing to remove.
  if (!isHistoryIdFound) {
    return false;
  }

  // 3. Build a new linear representation of history,
  //    excluding the entry that is being deleted.
  std::array<uint32_t, HISTORY_CAP> tempHistory;
  size_t tempHistorySize = 0;

  for (size_t i = 0; i < historySize; ++i) {
    uint32_t historyId;

    // Get the ID at this logical history position.
    if (historyGetAtLogical(i, historyId)) {

      // Skip all occurrences of the deleted ID.
      if (historyId == id) {
        continue;
      }

      // Add the remaining ID to the temporary history.
      tempHistory[tempHistorySize] = historyId;
      tempHistorySize++;
    }
  }

  // 4. Reset the circular buffer so the rebuilt history
  //    starts at physical position 0.
  historySize = tempHistorySize;
  historyHead = tempHistorySize % HISTORY_CAP;

  // Copy the rebuilt history back into the circular buffer.
  for (size_t i = 0; i < tempHistorySize; ++i) {
    history[i] = tempHistory[i];
  }

  // 5. Reposition the cursor to where currentInsultId now sits in the rebuilt
  //    history. This handles multiple removals cleanly without counting shifts.
  if (historySize == 0) {
    historyPosition = 0;
  } else {
    historyPosition = historySize - 1; // default to newest
    for (size_t i = 0; i < historySize; ++i) {
      if (tempHistory[i] == currentInsultId) {
        historyPosition = i;
        break;
      }
    }
  }

  // 6. The ID was successfully removed from history.
  return true;
}

// ============================================================================
// RENDERING
// ============================================================================
//
// Device display / rendering behavior belongs here.
//
// This section should not know about HTTP or LittleFS.
// ============================================================================

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
 * @param id ID of the insult to display.
 * @param action Action associated with the insult.
 * @param reason Reason the insult is being rendered.
 */
static void renderInsultById(uint32_t id, PendingAction action,
                             RenderReason reason) {

  if (insults.size() == 0) {
    Serial.println(F("[WARN] No insults available."));
    return;
  }

  auto it = findInsultById(id);

  if (it == insults.end()) {
    Serial.print(F("[WARN] Invalid insult ID: "));
    Serial.println(id);
    return;
  }

  const char *line = it->text.c_str();

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

  case RenderReason::Deleted:
    Serial.println(F("[Deleted]"));
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

// ============================================================================
// NVS PERSISTENCE
// ============================================================================
//
// This section handles persistent navigation state across deep sleep.
//
// It should not be responsible for persisting the actual insult JSON.
// That belongs in the JSON/file storage section above.
// ============================================================================

/**
 * @brief Restores the current insult and navigation history from NVS.
 *
 * Validates the persisted metadata and active history entries against the
 * currently loaded insult collection before updating the in-memory state.
 *
 * @param[out] outId Receives the restored current insult ID.
 * @return `true` if valid state was restored, `false` otherwise.
 */
static bool loadInsultsStateFromNvs(uint32_t &outId) {

  Preferences prefs;

  if (!prefs.begin(NVS_NS, true)) {
    return false;
  }

  const uint32_t magic = prefs.getUInt("m", 0);

  if (magic != NVS_MAGIC) {
    prefs.end();
    return false;
  }

  const uint32_t savedCur = prefs.getUInt("cur", 0);
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

  // Validate saved metadata against the current insult collection.
  if (insults.size() == 0) {
    return false;
  }

  const bool curValid = findInsultById(savedCur) != insults.end();

  const bool sizeValid = (savedSize <= HISTORY_CAP);

  const bool headValid = (savedHead < HISTORY_CAP);

  const bool posValid = (savedPos <= (savedSize == 0 ? 0 : (savedSize - 1)));

  if (!curValid || !sizeValid || !headValid || !posValid) {
    return false;
  }

  // Validate every active history entry against the current insult collection.
  // If insults.json changed since last sleep, saved IDs may no longer exist.

  const size_t oldest =
      wrapIndex(savedHead + HISTORY_CAP - savedSize, HISTORY_CAP);

  for (size_t i = 0; i < savedSize; i++) {

    const size_t physical = wrapIndex(oldest + i, HISTORY_CAP);

    auto it = findInsultById(history[physical]);

    if (it == insults.end()) {

      return false;
    }
  }

  historyHead = savedHead;
  historySize = savedSize;
  historyPosition = savedPos;

  currentInsultId = savedCur;
  outId = savedCur;

  return true;
}

/**
 * @brief Persist current id + history cursor to NVS before deep sleep.
 *
 * Called from main right before esp_deep_sleep_start().
 */
void insultsPersistForSleep() {

  Preferences prefs;

  if (!prefs.begin(NVS_NS, false)) {
    return;
  }

  prefs.putUInt("m", NVS_MAGIC);
  prefs.putUInt("cur", currentInsultId);
  prefs.putUShort("hH", static_cast<uint16_t>(historyHead));
  prefs.putUShort("hS", static_cast<uint16_t>(historySize));
  prefs.putUShort("hP", static_cast<uint16_t>(historyPosition));

  prefs.putBytes("hist", history, sizeof(history));

  prefs.end();
}

// ============================================================================
// OPERATION ORCHESTRATION
// ============================================================================
//
// Random / Next / Previous operation behavior lives here.
//
// These functions coordinate deck + history + rendering but do not deal with
// HTTP or persistent JSON CRUD.
// ============================================================================

/**
 * @brief Prepare internal state for a given user action.
 *
 * Chooses what ID will be shown after the simulated work delay completes:
 * - Random always draws a new insult.
 * - Prev moves back within history if possible.
 * - Next moves forward within history, but draws a new insult if at the end.
 *
 * @param action Navigation action to prepare.
 * @return `true` if the action was queued, or `false` if the action is
 * unsupported or its requested history entry cannot be selected.
 */
static bool beginWorkFor(PendingAction action) {

  operationIsNewInsult = false;

  if (action == PendingAction::Random) {

    pendingInsultId = drawFromDeck();
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

    if (!historyGetAtLogical(historyPosition, pendingInsultId)) {

      Serial.println(F("[Prev] History read failed."));
      return false;
    }

    operationPhase = OperationPhase::Waiting;

    return true;
  }

  if (action == PendingAction::Next) {

    if (historySize == 0) {

      // No history yet; treat Next like Random.
      pendingInsultId = drawFromDeck();
      operationIsNewInsult = true;
      operationPhase = OperationPhase::Waiting;

      return true;
    }

    if (historyPosition < historySize - 1) {

      // Still within history; move forward.
      historyPosition++;

      if (!historyGetAtLogical(historyPosition, pendingInsultId)) {

        Serial.println(F("[Next] History read failed."));
        return false;
      }

      operationPhase = OperationPhase::Waiting;

      return true;
    }

    // At newest entry; Next generates a new insult.
    pendingInsultId = drawFromDeck();
    operationIsNewInsult = true;
    operationPhase = OperationPhase::Waiting;

    return true;
  }

  return false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================
//
// Boot / wake initialization belongs here.
//
// If future decks need initialization, keep the deck-specific initialization
// logic together rather than mixing it into CRUD or rendering.
// ============================================================================

/**
 * @brief Initializes the insult collection, deck, history, and startup state.
 *
 * On cold boot, resets history and renders the title screen. On wake from
 * sleep, restores persisted state when available or seeds history with a
 * newly drawn insult without rendering it.
 *
 * @param printInsultOnBoot Whether to draw and render an insult during cold
 * boot.
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

      currentInsultId = drawFromDeck();

      appendToHistory(currentInsultId);

      renderInsultById(currentInsultId, PendingAction::Random,
                       RenderReason::Boot);

      return true;
    }

    return false;
  }

  // Wake from deep sleep: restore state, but don't render to avoid flash.
  uint32_t unusedRestoredId = 0;

  if (loadInsultsStateFromNvs(unusedRestoredId)) {

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

  currentInsultId = drawFromDeck();

  appendToHistory(currentInsultId);

  Serial.println("[Wake] no saved state; seeded first insult");

  return false;
}

// ============================================================================
// PUBLIC OPERATION API
// ============================================================================
//
// These are the functions main.cpp uses to tell the insults module to perform
// Random / Next / Previous operations.
// ============================================================================

/**
 * @brief Starts a delayed Random, Next, or Previous operation.
 *
 * Selects the pending insult and records the start time. The caller should
 * transition to its updating state only when this function returns `true`, then
 * call insultsPoll() to complete the operation.
 *
 * @param action Operation to start.
 * @param now Current time in milliseconds.
 * @return `true` if the operation was queued, or `false` if the action is
 * unsupported or its requested history entry cannot be selected.
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
 * @brief Polls the pending insult operation and completes it when its duration
 * has elapsed.
 *
 * Selects a replacement if the pending insult was deleted. A completed
 * operation updates history as needed and renders the result unless no insults
 * remain, then resets the operation to idle.
 *
 * @param now Current time in milliseconds.
 * @return true if an operation completed during this call, false otherwise.
 */
bool insultsPoll(uint32_t now) {

  if (operationPhase != OperationPhase::Waiting) {
    return false;
  }

  if ((now - operationStartedAt) < OPERATION_DURATION_MS) {
    return false;
  }

  const PendingAction completedAction = pendingAction;

  // If the pending insult was deleted while the operation was in flight,
  // fall back to a fresh random draw so main.cpp can still transition out
  // of Updating and the display shows a valid insult.
  bool usedFallback = false;
  if (findInsultById(pendingInsultId) == insults.end()) {
    if (insults.empty()) {
      operationPhase = OperationPhase::Idle;
      pendingAction = PendingAction::None;
      operationIsNewInsult = false;
      return true;
    }
    pendingInsultId = drawFromDeck();
    usedFallback = true;
  }

  currentInsultId = pendingInsultId;

  // Maintain history semantics:
  // - Random always appends
  // - Next appends only if it generated a new insult
  // - Prev does not append (cursor moved within beginWork)
  // - Fallback (deleted-while-in-flight) always appends regardless of action
  if (usedFallback) {

    appendToHistory(currentInsultId);

  } else if (completedAction == PendingAction::Random) {

    appendToHistory(currentInsultId);

  } else if (completedAction == PendingAction::Next) {

    if (operationIsNewInsult) {
      appendToHistory(currentInsultId);
    }
  }

  renderInsultById(currentInsultId, completedAction,
                   RenderReason::OperationComplete);

  pendingAction = PendingAction::None;
  operationPhase = OperationPhase::Idle;
  operationIsNewInsult = false;

  return true;
}

// ============================================================================
// CURRENT INSULT ACCESS
// ============================================================================
//
// Read-only information about the currently displayed insult.
// ============================================================================

/**
 * @brief Determines whether any insults are loaded.
 *
 * @return `true` if at least one insult is loaded, `false` otherwise.
 */
bool insultsHasAny() { return insults.size() > 0; }

/**
 * @brief Retrieves the ID of the current insult.
 *
 * @return uint32_t Current insult ID.
 */
uint32_t insultsGetCurrentId() { return currentInsultId; }

/**
 * @brief Gets the text of the current insult.
 *
 * @return const char* The current insult text, or a status message when no
 * valid insult is selected.
 */
const char *insultsGetCurrentText() {

  if (insults.size() == 0)
    return "No insults";

  auto it = findInsultById(currentInsultId);

  if (it == insults.end()) {

    return "Invalid insult";
  }

  return it->text.c_str();
}

// ============================================================================
// CRUD Operations
// ============================================================================

/**
 * @brief Adds a user-sourced insult to the collection and persists it.
 *
 * @param text Text of the insult to create.
 * @return DeckEntryResult indicating whether the entry was saved successfully
 * and containing the created entry.
 */
DeckEntryResult createInsult(std::string text) {

  // 1. Generate ID
  uint32_t id = generateNextId();

  // 2. Create Deck Entry
  DeckEntry newEntry{id, text, "user"};

  // 3. Add to in-memory vector
  insults.emplace_back(newEntry);

  if (saveJsonFile("/insults.json", insults)) {

    initDeck();

    // If this is the first insult, set it as current so the display has
    // something valid to render.
    if (insults.size() == 1) {
      currentInsultId = drawFromDeck();
      appendToHistory(currentInsultId);
    }

    // Saved successfully.
    Serial.println("[createInsult] JSON file updated successfully!");

    return {true, newEntry};

  } else {

    // Save failed.
    Serial.println("[createInsult] error saving insult");

    insults.pop_back();

    return {false, newEntry};
  }
}

/**
 * @brief Updates an insult's text and persists the collection.
 *
 * Restores the previous text when persistence fails.
 *
 * @param entryId Identifier of the insult to update.
 * @param text Replacement text.
 * @return A successful result containing the updated entry, or a failed result
 * whose reason distinguishes a missing entry from a persistence failure.
 */
DeckEntryResult editInsult(uint32_t entryId, std::string text) {

  // Find existing entry.
  auto it = findInsultById(entryId);

  if (it == insults.end()) {
    return {false, std::nullopt, EntryFailReason::NotFound};
  }

  std::string oldText = it->text;

  // Update existing text.
  it->text = text;

  if (saveJsonFile("/insults.json", insults)) {

    // Saved successfully.
    Serial.println("[editInsult] JSON file updated successfully!");

    return {true, *it};

  } else {

    // Save failed.
    Serial.println("[editInsult] error saving insult");

    // Roll back the in-memory change.
    it->text = oldText;

    return {false, std::nullopt, EntryFailReason::PersistenceFailed};
  }
}

// ============================================================================
// CRUD — DELETE
// ============================================================================

/**
 * @brief Deletes an insult and persists the remaining collection.
 *
 * After persistence succeeds, removes the identifier from the shuffled deck
 * and navigation history. Deleting the current insult selects a replacement or
 * clears the current identifier when the collection becomes empty. A
 * persistence failure restores the removed in-memory entry.
 *
 * @param entryId Identifier of the insult to delete.
 * @return A result indicating success, a missing entry, or a persistence
 * failure.
 */
DeleteDeckEntryResult deleteInsult(uint32_t entryId) {
  bool isCurrentInsult = false;

  // Find the existing entry by its persistent ID.
  auto it = findInsultById(entryId);

  // The entry does not exist, so there is nothing to delete.
  if (it == insults.end()) {
    return {false, DeleteFailReason::NotFound};
  }

  if (it->id == currentInsultId) {
    isCurrentInsult = true;
  }

  // Save a copy of the entry and its position so the in-memory
  // change can be rolled back if saving to the JSON file fails.
  DeckEntry oldEntry = *it;
  size_t oldPosition = std::distance(insults.begin(), it);

  // Remove the entry from the collection of insults.
  // This does not automatically remove its ID from the deck or history.
  insults.erase(it);

  // Persist the updated collection to the JSON file.
  if (saveJsonFile("/insults.json", insults)) {
    Serial.println("[deleteInsult] JSON file updated successfully!");

    // Remove the deleted ID from the shuffled deck.
    auto deckIt = std::find(deck.begin(), deck.end(), entryId);

    if (deckIt != deck.end()) {
      // Get the deleted ID's position in the deck.
      size_t deckIndex = std::distance(deck.begin(), deckIt);

      // If the deleted ID was before the next-to-draw position,
      // the next-to-draw position shifts backward by one.
      if (deckIndex < deckPosition) {
        deckPosition--;
      }

      // Remove the ID from the deck.
      deck.erase(deckIt);
    }

    // Remove the deleted ID from navigation history.
    removeFromHistory(entryId);

    // render a new insult.
    if (isCurrentInsult) {
      if (insults.size() == 0) {
        currentInsultId = 0;
      } else {
        currentInsultId = drawFromDeck();
        appendToHistory(currentInsultId);
        renderInsultById(currentInsultId, PendingAction::Random,
                         RenderReason::Deleted);
      }
    }
    return {true};
  }

  // Saving failed, so restore the deleted entry in memory.
  Serial.println("[deleteInsult] Error saving insult");

  insults.insert(insults.begin() + oldPosition, oldEntry);

  return {false, DeleteFailReason::PersistenceFailed};
}

// ============================================================================
// FUTURE DECK FEATURES
// ============================================================================
//
// Good place for future functionality such as:
//   - multiple deck support
//   - deck switching
//   - deck metadata
//   - deck import/export
//   - deck-specific settings
// ============================================================================

// ============================================================================
// FUTURE VALIDATION / MIGRATION
// ============================================================================
//
// Good place for functionality such as:
//   - validating loaded JSON
//   - schema versions
//   - migrating old deck formats
//   - repairing malformed entries
//   - handling newly introduced fields
// ============================================================================

// ============================================================================
// END OF MODULE
// ============================================================================