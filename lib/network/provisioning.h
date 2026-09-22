#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdint.h>

/** Result of attempting to start the provisioning portal. */
enum class ProvisioningStartResult { STARTED, ALREADY_ACTIVE, START_FAILED };

/** Result of polling an active provisioning session. */
enum class ProvisioningPollResult { IN_PROGRESS, SUCCESS, FAILED, CANCELLED };

/**
 * @brief Starts non-blocking Wi-Fi configuration.
 *
 * @return STARTED if the portal opens, ALREADY_ACTIVE if it is already open,
 *         or START_FAILED if it does not open.
 */
ProvisioningStartResult provisioningStart();

/**
 * @brief Services Wi-Fi configuration and reports its current outcome.
 *
 * @return IN_PROGRESS while the portal is open, SUCCESS when configuration
 *         succeeds, FAILED on timeout or connection failure, or CANCELLED
 *         when the portal closes without a submission.
 */
ProvisioningPollResult provisioningPoll();

#endif