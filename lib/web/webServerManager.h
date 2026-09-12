#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H
#include <WebServer.h>

#include <stdint.h>

/**
 * @brief Manages the HTTP web server and its request routes.
 *
 * Owns server lifecycle (start/stop) and all route registration. Does not own
 * Wi-Fi, mDNS, LittleFS, or deck business logic.
 */
class WebServerManager {
public:
  /** @brief Registers all routes and starts the HTTP server on port 80. */
  void start();

  /** @brief Stops the HTTP server. Call before exitWebMode(). */
  void stop();

  /** @brief Processes pending client requests; call once per loop(). */
  void handle();

private:
  WebServer server{80};

  void registerRoutes();
  void handleRoot();
  void handleGetDeck();
  void handleNotFound();
  void handleCreateDeckEntry();
  void handleEditDeckEntry();
  void handleDeleteDeckEntry();
  static String mimeTypeFor(const String &path);
};
#endif