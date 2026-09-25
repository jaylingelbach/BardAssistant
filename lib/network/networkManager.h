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
enum class WebModeStartResult { STARTED, START_FAILED };

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
 * Credentials are preserved so startWebModeConnection() can reconnect.
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

/**
 * @brief Starts connecting to Wi-Fi with saved credentials without waiting.
 *
 * Enables station mode. Call pollWebModeConnection() after STARTED to check
 * the outcome; STARTED does not guarantee a connection. Each STARTED result
 * resets the connection timeout. Does not start mDNS or the web server.
 *
 * @param now Current uptime in milliseconds, used as the timeout's start time.
 * @return WebModeStartResult::START_FAILED if WiFi.begin() reports
 * WL_CONNECT_FAILED, or STARTED otherwise. Failure does not disconnect Wi-Fi.
 */
WebModeStartResult startWebModeConnection(uint32_t now);

/**
 * @brief Checks a connection attempt started by startWebModeConnection().
 *
 * Call repeatedly after STARTED. Success takes precedence over the timeout,
 * including at or after 5,000 milliseconds. Failure does not disconnect Wi-Fi;
 * callers must handle cleanup.
 *
 * @param now Current uptime in milliseconds, on the same clock as the start.
 * @return WebModePollResult::SUCCESS when connected with a nonzero local IP,
 * FAILED if 5,000 milliseconds have elapsed or Wi-Fi reports WL_CONNECT_FAILED
 * or WL_NO_SSID_AVAIL, or IN_PROGRESS otherwise.
 */
WebModePollResult pollWebModeConnection(uint32_t now);

/**
 * @brief Disconnects Wi-Fi, powers down the radio, and clears saved settings.
 */
void resetWiFiSettings();

#endif
