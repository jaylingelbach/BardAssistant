#include "networkManager.h"
#include "WiFiType.h"
#include "log.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

bool isMdnsRunning = false;
WiFiManager wm;
bool portalHasTimedOut = false;
bool portalConnectionFailed = false;

bool hasKnownNetwork() {
  WiFi.mode(WIFI_STA);
  return wm.getWiFiIsSaved();
}

void resetWiFiSettings() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true /* wifioff */, true /* eraseap */);
  wm.resetSettings();
}

WiFiConfigurationStartResult startWiFiConfiguration() {
  if (wm.getConfigPortalActive()) {
    return WiFiConfigurationStartResult::ALREADY_ACTIVE;
  } else {
    WiFi.mode(WIFI_STA);
    portalHasTimedOut = false;
    portalConnectionFailed = false;
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(
        180); // 3 minutes for the user to complete Wi-Fi configuration.
    wm.setConnectTimeout(
        15); // Wait 15 seconds for a router response before opening portal
    wm.setConnectRetries(
        1); // Close portal after one failed attempt instead of re-opening
    wm.setConfigPortalTimeoutCallback([]() { portalHasTimedOut = true; });
    // Mark that credentials were submitted; if the portal closes without
    // process() succeeding, we know the connection attempt failed.
    wm.setSaveParamsCallback([]() { portalConnectionFailed = true; });
    wm.startConfigPortal("BardsAssistant", BARDS_HOSTNAME);

    return wm.getConfigPortalActive()
               ? WiFiConfigurationStartResult::STARTED
               : WiFiConfigurationStartResult::START_FAILED;
  }
}

WiFiConfigurationPollResult pollWiFiConfiguration() {
  if (wm.process()) {
    return WiFiConfigurationPollResult::SUCCESS;
  } else if (portalHasTimedOut) {
    return WiFiConfigurationPollResult::FAILED;
  } else if (wm.getConfigPortalActive()) {
    return WiFiConfigurationPollResult::IN_PROGRESS;
  } else {
    // Portal closed — either user dismissed it or credentials were rejected
    // (setConnectRetryMax(1) closes the portal after one failed attempt).
    return portalConnectionFailed ? WiFiConfigurationPollResult::FAILED
                                  : WiFiConfigurationPollResult::CANCELLED;
  }
}

/**
 * @brief Configures the device as a Wi-Fi station and establishes a network
 * connection.
 *
 * Clears stored Wi-Fi settings and opens a configuration portal when a
 * connection cannot be established within the configured timeouts.
 *
 * @return SetupModeResult::SUCCESS if connected successfully;
 *         SetupModeResult::SETUP_FAILED otherwise.
 */
SetupModeResult setupWiFi() {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;

  wm.resetSettings();

  bool res;

  wm.setConfigPortalTimeout(
      180); // 3 minutes to input credentials, else autoConnect() returns false

  wm.setConnectTimeout(
      15); // Wait 15 seconds for a router response before opening portal

  res = wm.autoConnect("BardsAssistant",
                       "bardsassistant"); // password protected ap

  if (!res) {
    LOG_ERROR("WiFi setup failed.");
    return SetupModeResult::SETUP_FAILED;
  } else {
    LOG_INFO("WiFi setup complete.");
    LOG_INFO_PRINT("IP Address: ");
    LOG_INFO_RAW(WiFi.localIP());
    return SetupModeResult::SUCCESS;
  }
}

/**
 * @brief Disconnects from Wi-Fi and powers down the radio.
 *
 * Credentials are preserved (eraseap=false) so enterWebMode() can reconnect
 * without re-provisioning.
 */
void disconnectWiFi() {
  WiFi.disconnect(true /* wifioff */, false /* eraseap */);
}

/**
 * @brief Enters web mode by connecting to the configured Wi-Fi network and
 * starting the mDNS responder.
 *
 * @return WebModeResult `SUCCESS` if Wi-Fi and mDNS are initialized,
 * `MDNS_FAILED` if mDNS setup fails, or `CONNECTION_FAILED` if Wi-Fi connection
 * fails.
 */
WebModeResult enterWebMode() {
  WiFi.mode(WIFI_STA);

  LOG_INFO("Attempting automatic connection...");

  WiFi.begin();

  const unsigned long timeout = 5000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    LOG_DEBUG_PRINT(".");
  }

  LOG_DEBUG("");

  if (WiFi.status() == WL_CONNECTED) {

    LOG_INFO("WiFi connected.");
    LOG_INFO_PRINT("Network: ");
    LOG_INFO_RAW(WiFi.SSID());
    LOG_INFO_PRINT("IP address: ");
    LOG_INFO_RAW(WiFi.localIP());

    if (!MDNS.begin(BARDS_HOSTNAME)) {
      LOG_ERROR("mDNS setup failed. Connect via IP:");
      LOG_INFO_RAW(WiFi.localIP());
      return WebModeResult::MDNS_FAILED;
    }
    isMdnsRunning = true;
    LOG_INFO("Visit bardsassistant.local to manage decks.");
    return WebModeResult::SUCCESS;
  } else {
    LOG_ERROR("Failed to connect to WiFi.");
    disconnectWiFi();
    return WebModeResult::CONNECTION_FAILED;
  }
}

/**
 * @brief Exits web mode by stopping mDNS and disconnecting Wi-Fi.
 *
 * mDNS is stopped before Wi-Fi disconnects so the goodbye multicast packet
 * reaches the network. Callers should stop WebServerManager before calling
 * this.
 */
void exitWebMode() {
  if (isMdnsRunning) {
    MDNS.end();
    isMdnsRunning = false;
  }
  disconnectWiFi();
}