#include "webServerManager.h"
#include "display.h"
#include "insults.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

/**
 * @brief Sends a JSON-formatted error response.
 *
 * @param server Web server used to send the response.
 * @param code HTTP status code.
 * @param message Error message included in the response.
 */
static void sendError(WebServer &server, int code, const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  server.send(code, "application/json", body);
}

template <typename T>
static void sendDeckEntryResult(WebServer &server, const T &result, int statusCode) {
  if (result.success && result.entry.has_value()) {
    JsonDocument resDoc;
    resDoc["id"] = result.entry->id;
    resDoc["text"] = result.entry->text;
    resDoc["source"] = result.entry->source;
    String response;
    serializeJson(resDoc, response);
    server.send(statusCode, "application/json", response);
  } else {
    sendError(server, 500, "Failed to save deck entry");
  }
}

/**
 * @brief Determines the MIME type for a file path.
 *
 * @param path File path whose extension is used to determine the MIME type.
 * @return String MIME type corresponding to the path extension, or
 * `text/plain` when the extension is unsupported.
 */
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

/**
 * @brief Parses a JSON request body into a document.
 *
 * @param body JSON-encoded request body.
 * @param doc Document to populate with the parsed JSON.
 * @return `true` if parsing succeeds, `false` if the body is empty or invalid.
 */
static bool tryParseJsonBody(const String &body, JsonDocument &doc) {

  if (body.length() == 0) {
    return false;
  }

  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    return false;
  }

  return true;
}

static bool isValidEntryId(const String &entryId) {
  if (entryId.length() == 0) {
    return false;
  }

  for (const char &c : entryId) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {

      return false; // Stop checking immediately if a non-digit is found
    }
  }
  return entryId.toInt() != 0;
}

/**
 * @brief Serves the root HTML page from LittleFS.
 *
 * Sends a 404 response when the page cannot be opened.
 */
void WebServerManager::handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

/**
 * @brief Serves the requested filesystem resource when available.
 *
 * @return void
 */
void WebServerManager::handleNotFound() {
  const String path = server.uri();
  if (!LittleFS.exists(path)) {
    sendError(server, 404, "File not found");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    sendError(server, 404, "File not found");
    return;
  }
  server.streamFile(file, mimeTypeFor(path));
  file.close();
}

/**
 * @brief Creates an entry in the specified deck from a JSON request body.
 *
 * @param id Deck identifier supplied by the request query parameters.
 */
void WebServerManager::handleCreateDeckEntry() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  if (id == "insults") {
    String body = server.arg("plain");

    JsonDocument doc;

    bool parseSuccess = tryParseJsonBody(body, doc);

    if (!parseSuccess) {
      sendError(server, 400, "Invalid JSON");
      return;
    }

    const std::string text = doc["text"];

    if (text.length() == 0) {
      sendError(server, 400, "Missing required field: text");
      return;
    }

    DeckEntryResult result = createInsult(text);

    if (result.success && insultsGetAll().size() == 1) {
      displayRenderInsult(insultsGetCurrentText());
    }

    sendDeckEntryResult(server, result, 201);

  } else {
    sendError(server, 404, "Deck not found");
  }
}

void WebServerManager::handleEditDeckEntry() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  if (!server.hasArg("entryId")) {
    sendError(server, 400, "Missing 'entryId' parameter");
    return;
  }

  const String entryIdStr = server.arg("entryId");

  if (!isValidEntryId(entryIdStr)) {
    sendError(server, 400, "Invalid entryId parameter");
    return;
  }
  const uint32_t entryId = entryIdStr.toInt();

  if (entryId == 0) {
    sendError(server, 400, "Invalid 'entryId' parameter");
    return;
  }

  if (id == "insults") {
    String body = server.arg("plain");

    JsonDocument doc;

    bool parseSuccess = tryParseJsonBody(body, doc);

    if (!parseSuccess) {
      sendError(server, 400, "Invalid JSON");
      return;
    }

    const std::string text = doc["text"];

    if (text.length() == 0) {
      sendError(server, 400, "Missing required field: text");
      return;
    }

    DeckEntryResult result = editInsult(entryId, text);

    if (result.success && entryId == insultsGetCurrentId()) {
      if (!displayRenderInsult(insultsGetCurrentText())) {
        Serial.println("[WARN] Edit succeeded but display refresh failed");
      }
    }

    sendDeckEntryResult(server, result, 200);

  } else {
    sendError(server, 404, "Deck not found");
  }
}

/**
 * @brief Deletes an entry from the specified deck.
 *
 * Requires `id` (deck identifier) and `entryId` query parameters.
 */
void WebServerManager::handleDeleteDeckEntry() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  if (!server.hasArg("entryId")) {
    sendError(server, 400, "Missing 'entryId' parameter");
    return;
  }

  const String entryIdStr = server.arg("entryId");

  if (!isValidEntryId(entryIdStr)) {
    sendError(server, 400, "Invalid 'entryId' parameter");
    return;
  }

  const uint32_t entryId = entryIdStr.toInt();

  if (id == "insults") {
    DeleteDeckEntryResult result = deleteInsult(entryId);
    if (result.success) {
      if (insultsHasAny()) {
        if (!displayRenderInsult(insultsGetCurrentText())) {
          sendError(server, 500, "Unable to render Current Insult");
          return;
        }
      } else {
        displayRenderEmptyState();
      }
      server.send(204);
    } else if (result.reason == DeleteFailReason::NotFound) {
      sendError(server, 404, "Entry not found");
    } else {
      sendError(server, 500, "Failed to delete entry");
    }
  } else {
    sendError(server, 404, "Deck not found");
  }
}

/**
 * @brief Registers the server routes and starts the web server.
 */
void WebServerManager::start() {
  registerRoutes();
  server.begin();
  Serial.println("[WebServerManager] Started Web Server");
}

/**
 * @brief Stops the web server.
 */
void WebServerManager::stop() { server.stop(); }

/**
 * @brief Processes pending web-server client requests.
 */
void WebServerManager::handle() { server.handleClient(); }

/**
 * @brief Registers HTTP handlers for page delivery, deck operations, and
 * unmatched requests.
 */
void WebServerManager::registerRoutes() {
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/api/decks", HTTP_GET, [this]() { handleGetDeck(); });
  server.on("/api/decks", HTTP_POST, [this]() { handleCreateDeckEntry(); });
  server.on("/api/decks", HTTP_PUT, [this]() { handleEditDeckEntry(); });
  server.on("/api/decks", HTTP_DELETE, [this]() { handleDeleteDeckEntry(); });
  server.onNotFound([this]() { handleNotFound(); });
}

/**
 * @brief Sends the requested deck entries as a JSON array.
 *
 * Requires an `id` query parameter. The `insults` deck is supported; unknown
 * deck identifiers return a 404 error.
 */
void WebServerManager::handleGetDeck() {
  if (!server.hasArg("id")) {
    sendError(server, 400, "Missing 'id' parameter");
    return;
  }

  const String id = server.arg("id");

  // Temporary: deck routing belongs in DeckManager once multiple decks exist.
  const std::vector<DeckEntry> *entries = nullptr;
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
    obj["id"] = (*entries)[i].id;
    obj["text"] = (*entries)[i].text;
  }

  String response;
  serializeJson(doc, response);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", response);
}
