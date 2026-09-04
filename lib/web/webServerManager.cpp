#include "webServerManager.h"
#include "insults.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

String WebServerManager::mimeTypeFor(const String &path) {
  if (path.endsWith(".html"))
    return "text/html";
  if (path.endsWith(".css"))
    return "text/css";
  if (path.endsWith(".js"))
    return "application/javascript";
  if (path.endsWith(".json"))
    return "application/json";
  return "text/plain";
}

void WebServerManager::handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void WebServerManager::handleNotFound() {
  const String path = server.uri();
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  server.streamFile(file, mimeTypeFor(path));
  file.close();
}

void WebServerManager::start() {
  registerRoutes();
  server.begin();
  Serial.println("[WebServerManager] Started Web Server");
}

void WebServerManager::stop() { server.stop(); }

void WebServerManager::handle() { server.handleClient(); }

void WebServerManager::registerRoutes() {
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/api/decks", HTTP_GET, [this]() { handleGetDeck(); });
  server.onNotFound([this]() { handleNotFound(); });
}

void WebServerManager::handleGetDeck() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  // Temporary: deck routing belongs in DeckManager once multiple decks exist.
  const std::vector<std::string> *entries = nullptr;
  if (id == "insults") {
    entries = &insultsGetAll();
  }

  if (entries == nullptr) {
    server.send(404, "text/plain", "Deck not found");
    return;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (size_t i = 0; i < entries->size(); i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = i;
    obj["text"] = (*entries)[i];
  }

  String response;
  serializeJson(doc, response);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", response);
}