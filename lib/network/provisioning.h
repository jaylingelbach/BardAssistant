#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdint.h>

enum class ProvisioningStartResult { STARTED, ALREADY_ACTIVE, START_FAILED };

enum class ProvisioningPollResult { IN_PROGRESS, SUCCESS, FAILED, CANCELLED };

ProvisioningStartResult provisioningStart(uint32_t now);

ProvisioningPollResult provisioningPoll(uint32_t now);

bool provisioningIsActive();

#endif