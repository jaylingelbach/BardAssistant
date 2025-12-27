#ifndef INSULTS_H
#define INSULTS_H

#include <stdint.h>

enum class PendingAction { None = 0, Random, Next, Prev };

/**
 * @brief Initialize the insults module and render the boot/title UI.
 *
 * Initializes deck/history state and prints the title screen. If
 * `printInsultOnBoot` is true and at least one insult exists, also prints an
 * initial insult.
 *
 * @param printInsultOnBoot Whether to print an insult immediately at boot.
 * @return true if an insult was printed on boot; false otherwise.
 */
bool insultsInit(bool printInsultOnBoot);

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

#endif // INSULTS_H
