#include "networkManager.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

bool isMdnsRunning = false;

/**
 * @brief Configures the device as a Wi-Fi station and connects it to a network.
 *
 * Opens a configuration portal when a saved network cannot be connected to
 * within the configured timeouts.
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
    Serial.println("Failed to connect");
    return SetupModeResult::SETUP_FAILED;
  } else {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return SetupModeResult::SUCCESS;
  }
}

/**
 * @brief Disconnects the device from the Wi-Fi network.
 *
 * @return DisconnectModeResult::SUCCESS if disconnection is verified;
 *         DisconnectModeResult::DISCONNECT_FAILED if the device remains
 * connected.
 */
DisconnectModeResult disconnectWiFi() {
  WiFi.disconnect(true, false);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Disconnection verified.");
    return DisconnectModeResult::SUCCESS;
  } else {
    Serial.println("Disconnection failed. Device still online.");
    return DisconnectModeResult::DISCONNECT_FAILED;
  }
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

  Serial.println("Attempting automatic connection...");

  WiFi.begin();

  const unsigned long timeout = 5000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi connected");
    Serial.print("Network: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("bardsassistant")) {
      Serial.println("Error setting up MDNS responder");
      Serial.println("Visit: ");
      Serial.println(WiFi.localIP());
      Serial.println("to view decks");
      return WebModeResult::MDNS_FAILED; // can print to user to use the localip
                                         // in main.
    }
    isMdnsRunning = true;
    Serial.println("Visit BardAssistant.local to view decks");
    return WebModeResult::SUCCESS; // SUCCESS includes MDNS bc it's the enter
                                   // web mode's overall success, if Wifi
                                   // connects but mdns doesn't it returns a
                                   // failure of mdns
  } else {
    Serial.println("Failed to connect to WiFi.");
    WiFi.disconnect();
    return WebModeResult::CONNECTION_FAILED;
  }
}

/**
 * @brief Exits web mode by disconnecting Wi-Fi and stopping the mDNS responder.
 */
void exitWebMode() {
  disconnectWiFi();
  if (isMdnsRunning) {
    MDNS.end();
    isMdnsRunning = false;
  }
}