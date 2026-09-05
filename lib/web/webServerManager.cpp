#include "webServerManager.h"
#include "insults.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

static void sendError(WebServer &server, int code, const char *message) {
  String body = "{\"error\":\"";
  body += message;
  body += "\"}";
  server.send(code, "application/json", body);
}

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
    sendError(server, 404, "Not found");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    sendError(server, 404, "Not found");
    return;
  }
  server.streamFile(file, mimeTypeFor(path));
  file.close();
}

void WebServerManager::handleCreateDeckEntry() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  if (id == "insults") {
    String body = server.arg("plain");

    if (body.length() == 0) {
      sendError(server, 400, "Missing request body");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      sendError(server, 400, "Invalid JSON");
      return;
    }

    const String text = doc["text"].as<String>();
    Serial.println("[POST /api/decks] text: " + text);
    server.send(200, "application/json", "{\"text\":\"" + text + "\"}");
  } else {
    sendError(server, 400, "Unknown deck id");
  }
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
  server.on("/api/decks", HTTP_POST, [this]() { handleCreateDeckEntry(); });
  server.onNotFound([this]() { handleNotFound(); });
}

void WebServerManager::handleGetDeck() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  // Temporary: deck routing belongs in DeckManager once multiple decks exist.
  const std::vector<std::string> *entries = nullptr;
  if (id == "insults") {
    entries = &insultsGetAll();
  }

  if (entries == nullptr) {
    sendError(server, 404, "Deck not found");
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