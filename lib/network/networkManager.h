#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <cstdint>

static constexpr const char* BARDS_HOSTNAME = "bardsassistant";

/** Result of entering web mode: covers WiFi.begin() and MDNS.begin() outcomes.
 */
enum class WebModeResult { SUCCESS, CONNECTION_FAILED, MDNS_FAILED };

/** Result of polling the WebMode configuration process. */
enum class WebModePollResult { IN_PROGRESS, SUCCESS, FAILED };

/** Result of starting the WebMode configuration process. */
enum class WebModeStartResult { STARTED, ALREADY_ACTIVE, START_FAILED };

enum class MDNSResult { SUCCESS, MDNS_FAILED };

/** Result of polling the Wi-Fi configuration process. */
enum class WiFiConfigurationPollResult {
  IN_PROGRESS,
  SUCCESS,
  FAILED,
  CANCELLED
};

/** Result of starting the Wi-Fi configuration process. */
enum class WiFiConfigurationStartResult {
  STARTED,
  ALREADY_ACTIVE,
  START_FAILED
};

/**
 * @brief Disconnects from the current Wi-Fi network and powers down the radio.
 *
 * Credentials are preserved so a future enterWebMode() call can reconnect.
 */
void disconnectWiFi();

/**
 * @brief Connects using saved credentials and starts mDNS.
 *
 * @return WebModeResult::SUCCESS if Wi-Fi has an IP address and mDNS starts,
 * CONNECTION_FAILED if Wi-Fi is disconnected or has no IP address after the
 * timeout, or MDNS_FAILED if mDNS fails.
 */
WebModeResult enterWebMode();

/**
 * @brief Starts mDNS for an existing Wi-Fi connection without reconnecting.
 *
 * Used after a successful provisioning where WiFiManager already holds a live
 * connection. Skips WiFi.begin() to avoid dropping the IP before mDNS starts.
 *
 * @return WebModeResult::SUCCESS if mDNS starts, MDNS_FAILED if it does not.
 */
WebModeResult enterWebModeAlreadyConnected();

/**
 * @brief Starts mDNS for the current Wi-Fi connection.
 *
 * @return MDNSResult::SUCCESS if mDNS starts, MDNS_FAILED if it does not.
 */
MDNSResult startMDNS();

/**
 * @brief Stops mDNS and disconnects Wi-Fi, returning to offline mode.
 *
 * Callers must stop WebServerManager before calling this.
 */
void exitWebMode();

// For provisioning.

/**
 * @brief Determines whether at least one known Wi-Fi network is configured.
 *
 * @return true if Wi-Fi credentials are saved, false otherwise.
 */
bool hasKnownNetwork();

/**
 * @brief Starts the WiFiManager configuration portal in non-blocking mode.
 *
 * The configuration process continues to be serviced by
 * pollWiFiConfiguration() while the main application loop continues running.
 *
 * @return WiFiConfigurationStartResult::STARTED if configuration started,
 *         ALREADY_ACTIVE if configuration is already running, or
 *         START_FAILED if configuration could not be started.
 */
WiFiConfigurationStartResult startWiFiConfiguration();

/**
 * @brief Polls the active WiFiManager configuration process.
 *
 * This function should be called repeatedly from the main application loop
 * while Wi-Fi configuration is active.
 *
 * @return WiFiConfigurationPollResult::IN_PROGRESS while configuration is
 *         still running, SUCCESS when configuration completes successfully,
 *         FAILED after a timeout or unsuccessful credential submission, or
 *         CANCELLED when the portal closes without a submission.
 */
WiFiConfigurationPollResult pollWiFiConfiguration();

WebModeStartResult startWebModeConnection(uint32_t now);

WebModePollResult pollWebModeConnection(uint32_t now);

/**
 * @brief Disconnects Wi-Fi, powers down the radio, and clears saved settings.
 */
void resetWiFiSettings();

#endif
