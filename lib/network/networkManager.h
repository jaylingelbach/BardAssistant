#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

/** Result of entering web mode: covers WiFi.begin() and MDNS.begin() outcomes. */
enum class WebModeResult { SUCCESS, CONNECTION_FAILED, MDNS_FAILED };

/** Result of a Wi-Fi setup/provisioning attempt. */
enum class SetupModeResult { SUCCESS, SETUP_FAILED };

/**
 * @brief Opens the WiFiManager configuration portal and connects to the
 * selected network.
 *
 * @return SetupModeResult::SUCCESS on connection, SETUP_FAILED otherwise.
 */
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

/**
 * @brief Stops mDNS and disconnects Wi-Fi, returning to offline mode.
 *
 * Callers must stop WebServerManager before calling this.
 */
void exitWebMode();

#endif
