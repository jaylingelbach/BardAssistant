#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

static constexpr const char *BARDS_HOSTNAME = "bardsassistant";

/** Result of entering web mode: covers WiFi.begin() and MDNS.begin() outcomes.
 */
enum class WebModeResult { SUCCESS, CONNECTION_FAILED, MDNS_FAILED };

/** Result of a Wi-Fi setup/provisioning attempt. */
enum class SetupModeResult { SUCCESS, SETUP_FAILED };

/** Result of connecting to a known Wi-Fi network. */
enum class WiFiConnectionResult { CONNECTED, IN_PROGRESS, FAILED };

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
 * @brief Opens the WiFiManager configuration portal and connects to the
 * selected network.
 *
 * @return SetupModeResult::SUCCESS on connection, SETUP_FAILED otherwise.
 */
[[deprecated("Use the provisioning module instead.")]]
SetupModeResult setupWiFi();

/**
 * @brief Disconnects from the current Wi-Fi network and powers down the radio.
 *
 * Credentials are preserved so a future enterWebMode() call can reconnect.
 */
void disconnectWiFi();

/**
 * @brief Connects using saved credentials, starts mDNS, and enables web mode.
 *
 * @return WebModeResult::SUCCESS if Wi-Fi and mDNS both start successfully,
 * CONNECTION_FAILED if Wi-Fi cannot connect, MDNS_FAILED if mDNS fails.
 */
WebModeResult enterWebMode();
WebModeResult enterWebModeAlreadyConnected();

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
 * @return true if a known network is available, false otherwise.
 */
bool hasKnownNetwork();

bool wiFiIsConnected();

/**
 * @brief Begins connecting to a known Wi-Fi network.
 *
 * Starts a connection attempt using saved network credentials. The caller
 * should use the return value to determine whether the connection succeeded,
 * is still in progress, or failed.
 *
 * @return WiFiConnectionResult::CONNECTED if already connected,
 *         WiFiConnectionResult::IN_PROGRESS if the connection attempt has
 *         started, or WiFiConnectionResult::FAILED if the connection could
 *         not be started.
 */
WiFiConnectionResult connectToKnownNetwork();

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
 *         FAILED when configuration fails, or CANCELLED when the configuration
 *         process is cancelled.
 */
WiFiConfigurationPollResult pollWiFiConfiguration();

void resetWiFiSettings();

#endif