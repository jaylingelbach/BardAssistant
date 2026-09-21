#include "provisioning.h"
#include "networkManager.h"
#include <cstdint>

static uint32_t operationStartedAt = 0;
static uint32_t pollingStartedAt = 0;
ProvisioningStartResult provisioningStart(uint32_t now) {
  operationStartedAt = now;
  return ProvisioningStartResult::STARTED;
};

ProvisioningPollResult provisioningPoll(uint32_t now) {
  // is it finished? Determined by provisioning state
  //   ask WiFiManager:
  //     "Are we done?"
  //     ↓
  // WiFiManager says:
  //     still running
  //     OR
  //     credentials successfully obtained
  //     OR
  //     failed
  //     ↓
  // return IN_PROGRESS / SUCCESS / FAILED
  // do I need uint32_ now? maybe for a timeout.
  //     ├── no → IN_PROGRESS
  //     ├── yes → SUCCESS
  //     ├── failed → FAILED
  //     └── cancelled → CANCELLED
  //   pollingStartedAt = now;
  return ProvisioningPollResult::IN_PROGRESS;
};

bool provisioningIsActive() { return true; };