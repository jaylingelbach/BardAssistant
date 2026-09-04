#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H
#include <WebServer.h>

#include <stdint.h>

class WebServerManager {
public:
  void start();
  void stop();
  void handle();

private:
  WebServer server{80};

  void registerRoutes();
  void handleRoot();
  void handleGetDeck();
  void handleNotFound();
  void handleCreateDeckEntry();
  static String mimeTypeFor(const String &path);
};
#endif