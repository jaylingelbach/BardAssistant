#include "networkManager.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

bool isMdnsRunning = false;

SetupModeResult setupWiFi() {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;

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

void exitWebMode() {
  disconnectWiFi();
  if (isMdnsRunning) {
    MDNS.end();
    isMdnsRunning = false;
  }
}