#include "networkManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdint>

#include "WiFiType.h"
#include "log.h"

// WiFi Manager
WiFiManager wm;
static constexpr uint32_t WEB_MODE_CONNECTION_TIMEOUT_MS = 5000;
uint32_t connectionStartedAt = 0;

// MDNS
bool isMdnsRunning = false;

// Provisioning
bool portalConnectionFailed = false;
bool portalHasTimedOut = false;

bool hasKnownNetwork() {
  WiFi.mode(WIFI_STA);
  return wm.getWiFiIsSaved();
}

void resetWiFiSettings() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true /* wifioff */, true /* eraseap */);
  wm.resetSettings();
}

WebModeStartResult startWebModeConnection(uint32_t now) {
  WiFi.mode(WIFI_STA);

  // STARTED means Wi-Fi connection initiation succeeded and the connection is
  // still being resolved. It does not mean Wi-Fi is connected.
  wl_status_t status = WiFi.begin();

  if (status == WL_CONNECT_FAILED) {
    return WebModeStartResult::START_FAILED;
  }

  connectionStartedAt = now;
  LOG_INFO("Attempting automatic connection...");
  return WebModeStartResult::STARTED;
}

WebModePollResult pollWebModeConnection(uint32_t now) {
  wl_status_t status = WiFi.status();

  // Success always wins — even if we're right at the timeout.
  if (status == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    return WebModePollResult::SUCCESS;
  }

  // Anything that hasn't succeeded by the timeout fails.
  if (now - connectionStartedAt >= WEB_MODE_CONNECTION_TIMEOUT_MS) {
    return WebModePollResult::FAILED;
  }

  // Known immediate failures.
  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    return WebModePollResult::FAILED;
  }

  // Still trying.
  return WebModePollResult::IN_PROGRESS;
}

void disconnectWiFi() {
  WiFi.disconnect(true /* wifioff */, false /* eraseap */);
}

MDNSResult startMDNS() {
  if (!MDNS.begin(BARDS_HOSTNAME)) {
    LOG_ERROR("mDNS setup failed. Connect via IP:");
    LOG_INFO_RAW(WiFi.localIP());

    isMdnsRunning = false;
    return MDNSResult::MDNS_FAILED;
  }

  isMdnsRunning = true;
  LOG_INFO("Visit bardsassistant.local to manage decks.");

  return MDNSResult::SUCCESS;
}

WebModeResult enterWebModeAlreadyConnected() {
  MDNSResult result = startMDNS();

  if (result == MDNSResult::MDNS_FAILED) {
    return WebModeResult::MDNS_FAILED;
  }

  return WebModeResult::SUCCESS;
}

void exitWebMode() {
  if (isMdnsRunning) {
    MDNS.end();
    isMdnsRunning = false;
  }

  disconnectWiFi();
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
        180);  // 3 minutes for the user to complete Wi-Fi configuration.
    wm.setConnectTimeout(
        15);  // Wait 15 seconds for a router response before opening portal
    wm.setConnectRetries(
        1);  // Close portal after one failed attempt instead of re-opening

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
  } else if (portalConnectionFailed && wm.getConfigPortalActive()) {
    // Credentials were submitted but connection failed — close the portal
    // rather than leaving the user stuck until the 3-minute timeout.
    wm.stopConfigPortal();
    return WiFiConfigurationPollResult::FAILED;
  } else if (wm.getConfigPortalActive()) {
    return WiFiConfigurationPollResult::IN_PROGRESS;
  } else {
    return portalConnectionFailed ? WiFiConfigurationPollResult::FAILED
                                  : WiFiConfigurationPollResult::CANCELLED;
  }
}
