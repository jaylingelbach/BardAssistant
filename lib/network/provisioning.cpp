#include "provisioning.h"
#include "networkManager.h"
#include <cstdint>

ProvisioningStartResult provisioningStart() {
  WiFiConfigurationStartResult result = startWiFiConfiguration();

  switch (result) {
    case WiFiConfigurationStartResult::ALREADY_ACTIVE:
      return ProvisioningStartResult::ALREADY_ACTIVE;
    case WiFiConfigurationStartResult::STARTED:
      return ProvisioningStartResult::STARTED;
    case WiFiConfigurationStartResult::START_FAILED:
      return ProvisioningStartResult::START_FAILED;
  }

  return ProvisioningStartResult::START_FAILED;
}

ProvisioningPollResult provisioningPoll() {

  WiFiConfigurationPollResult result = pollWiFiConfiguration();

  switch (result) {
  case WiFiConfigurationPollResult::IN_PROGRESS:
  return ProvisioningPollResult::IN_PROGRESS;

case WiFiConfigurationPollResult::SUCCESS:
  return ProvisioningPollResult::SUCCESS;

case WiFiConfigurationPollResult::FAILED:
  return ProvisioningPollResult::FAILED;

case WiFiConfigurationPollResult::CANCELLED:
  return ProvisioningPollResult::CANCELLED;
}

return ProvisioningPollResult::FAILED;
}