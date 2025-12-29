#ifndef INSULTS_H
#define INSULTS_H

#include <stdint.h>

enum class PendingAction { None = 0, Random, Next, Prev };

/**
 * @brief Initialize the insults module and render the boot/wake UI.
 *
 * On cold boot, initializes deck/history state and prints the title screen.
 * If `printInsultOnBoot` is true and at least one insult exists, also prints an
 * initial insult.
 *
 * On wake-from-sleep, attempts to restore and render the last shown insult from
 * RTC-persisted state. If the stored state is invalid or empty, it draws a new
 * insult and seeds history.
 *
 * @param printInsultOnBoot Whether to print an insult immediately on cold boot.
 * @param wokeFromSleep True if the caller determined this boot followed deep
 * sleep.
 * @return true if an insult was rendered during init; false otherwise.
 */
bool insultsInit(bool printInsultOnBoot, bool wokeFromSleep);

/**
 * @brief Start a mocked “operation” (Random/Next/Prev).
 *
 * Selects pending work (new insult or history navigation) and renders the
 * operation start UI. This does not change application state; the caller should
 * transition to Updating only if this returns true.
 *
 * @param action The action to start (Random, Next, Prev).
 * @param now Current time in milliseconds (typically millis()).
 * @return true if work was started; false if there was nothing to do.
 */
bool insultsStartOperation(PendingAction action, uint32_t now);

/**
 * @brief Advance the mocked operation while in Updating.
 *
 * Returns true exactly once when the operation completes. On completion, this
 * renders the final insult output and resets internal operation state.
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

#endif // INSULTS_H
