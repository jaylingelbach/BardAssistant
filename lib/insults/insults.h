#ifndef INSULTS_H
#define INSULTS_H

#include "deckTypes.h"
#include <cstdint>
#include <string>
#include <vector>

enum class PendingAction { None = 0, Random, Next, Prev };

/**
 * @brief Initialize the insults module and render the boot/wake UI.
 *
 * On cold boot, initializes deck/history state and prints the title screen.
 * If `printInsultOnBoot` is true and at least one insult exists, also prints an
 * initial insult.
 *
 * On wake-from-sleep, restore state but don’t render automatically on wake.
 *
 * @param printInsultOnBoot Whether to print an insult immediately on cold boot.
 * @param wokeFromSleep True if the caller determined this boot followed deep
 * sleep.
 * @return true if an insult was rendered during init; false otherwise.
 */
bool insultsInit(bool printInsultOnBoot, bool wokeFromSleep);

/**
 * @brief Starts a delayed Random, Next, or Previous operation.
 *
 * Selects a pending insult and records the operation start time. The caller
 * should transition to Updating only if this returns `true`, then call
 * insultsPoll() to complete the operation.
 *
 * @param action The action to start (Random, Next, Prev).
 * @param now Current time in milliseconds (typically millis()).
 * @return `true` if the operation was queued, or `false` if the action is
 * unsupported or its requested history entry cannot be selected.
 */
bool insultsStartOperation(PendingAction action, uint32_t now);

/**
 * @brief Advances the pending operation while in Updating.
 *
 * Returns true exactly once when the operation completes. On completion, this
 * selects a replacement for a deleted pending insult and renders the final
 * output unless no insults remain, then resets internal operation state.
 *
 * @param now Current time in milliseconds (typically millis()).
 * @return true when the operation completes; false otherwise.
 */
bool insultsPoll(uint32_t now);

/**
 * @brief Persist current insult + history to NVS so we can restore after sleep.
 *
 * Call this right before entering deep sleep.
 */
void insultsPersistForSleep();

const char *insultsGetCurrentText();

uint32_t insultsGetCurrentId();

bool insultsHasAny();

const std::vector<DeckEntry> &insultsGetAll();

DeckEntryResult createInsult(std::string text);

DeckEntryResult editInsult(uint32_t entryId, std::string text);

DeleteDeckEntryResult deleteInsult(uint32_t entryId);

#endif // INSULTS_H
