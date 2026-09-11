# Bard's Assistant — Wi-Fi Provisioning & Web Mode Strategy

## Purpose

Record the intended Wi-Fi architecture so future changes do not mix up **Wi-Fi provisioning** with **Web Mode**.

> **`setupWiFi()` configures Wi-Fi. `enterWebMode()` starts Web Mode.**

mDNS belongs to Web Mode, not generic Wi-Fi provisioning.

## Mental Model

Bard's Assistant is **offline-first**. Normal use should not automatically turn on Wi-Fi or the web server.

### First-time setup

```text
NORMAL / OFFLINE
       |
       | User chooses Wi-Fi Setup
       v
  setupWiFi()
       |
       | Wi-Fi credentials configured
       v
   SUCCESS
       |
       v
  enterWebMode()
       |
       +-- Connect/use saved Wi-Fi
       +-- Start mDNS
       +-- Start Web Server
       |
       v
     WEB MODE
```

### Later Web Mode entry

```text
NORMAL / OFFLINE
       |
       | User chooses Web Mode
       v
  enterWebMode()
       |
       +-- Connect using saved credentials
       +-- Start mDNS
       +-- Start Web Server
       |
       v
     WEB MODE
```

### Exiting Web Mode

```text
WEB MODE
   |
   v
exitWebMode()
   |
   +-- Stop Web Server
   +-- Stop mDNS
   +-- Disconnect Wi-Fi
   |
   v
NORMAL / OFFLINE
```

Wi-Fi credentials remain saved when Web Mode is exited.

---

## `setupWiFi()`

### Meaning

> "I want to configure or change the Wi-Fi network."

Owns Wi-Fi provisioning through WiFiManager:

- Start the configuration portal
- Let the user provide Wi-Fi credentials
- Connect to the selected network
- Return a setup result

It should **not** be responsible for:

- Starting mDNS
- Starting the WebServer
- Keeping Web Mode alive
- Serving the web UI

Having a Wi-Fi connection does not necessarily mean the device is in Web Mode.

---

## `enterWebMode()`

### Meaning

> "I want to use the device's web interface."

Owns the services required for Web Mode:

1. Connect using saved Wi-Fi credentials.
2. Verify Wi-Fi connection.
3. Start mDNS with hostname:

```text
bardsassistant
```

which provides:

```text
http://bardsassistant.local
```

4. Start `WebServerManager`.
5. Return the appropriate result.

### Important

**mDNS belongs here.**

Do not move `MDNS.begin("bardsassistant")` into `setupWiFi()` just because Wi-Fi happens to be connected after setup.

---

## `exitWebMode()`

Reverses Web Mode:

1. Stop the WebServer.
2. Stop mDNS if running.
3. Disconnect Wi-Fi.
4. Preserve saved Wi-Fi credentials.

---

## Successful Wi-Fi Setup

The application/main layer coordinates the transition:

```text
if user requested Wi-Fi Setup:
    result = setupWiFi()

    if result == SUCCESS:
        enterWebMode()
```

`setupWiFi()` does not need to call `enterWebMode()` itself.

This keeps responsibilities clear.

---

## After Initial Provisioning

The user should not need to configure Wi-Fi every time.

Later:

```text
User chooses Web Mode
 ↓
enterWebMode()
 ↓
Use saved credentials
 ↓
Wi-Fi connects
 ↓
MDNS.begin("bardsassistant")
 ↓
WebServerManager.start()
```

No WiFiManager configuration portal should appear unless the user explicitly chooses Wi-Fi Setup.

---

## Why mDNS Is NOT in `setupWiFi()`

Avoid:

```text
setupWiFi()
    ↓
Wi-Fi connected
    ↓
MDNS.begin(...)
```

Instead:

```text
setupWiFi()
    ↓
Wi-Fi configuration
```

and:

```text
enterWebMode()
    ↓
Wi-Fi connection
    +
mDNS
    +
Web Server
```

---

## Temporary Development Exception

During development it is okay to temporarily do:

```text
setupWiFi()
webServerManager.start()
```

for convenience.

However, this bypasses `enterWebMode()`, so **mDNS will not start**. That is expected.

If `.local` access is needed during development, temporarily use:

```text
setupWiFi()
    ↓
enterWebMode()
```

Do not permanently move mDNS into `setupWiFi()` to accommodate the development shortcut.

---

## Current mDNS Contract

The frontend expects:

```text
bardsassistant.local
```

Therefore preserve:

```text
MDNS.begin("bardsassistant")
```

unless the hostname is intentionally changed throughout the application.

If mDNS fails but Wi-Fi succeeds, Web Mode may still be usable through the ESP32's local IP address.

Current conceptual results:

```text
SUCCESS
CONNECTION_FAILED
MDNS_FAILED
```

---

## Responsibility Boundaries

### NetworkManager

Owns:

- Wi-Fi provisioning
- Wi-Fi connection/disconnection
- mDNS lifecycle

### WebServerManager

Owns:

- HTTP server
- HTTP routes
- HTTP request/response handling
- Web UI/API serving

It should **not** own:

- Wi-Fi provisioning
- LittleFS persistence
- Deck business logic

### Main/Application Layer

Owns:

- User-driven state transitions
- Deciding when to enter/exit Web Mode
- Coordinating managers

---

## Rule to Remember

If future development raises:

> "Wi-Fi is connected. Should I start mDNS here?"

Ask:

> **"Is the device entering Web Mode?"**

If **no** → don't start mDNS.

If **yes** → `enterWebMode()` handles mDNS.

### Intended architecture

```text
Wi-Fi Setup
    ↓
setupWiFi()
    ↓
SUCCESS
    ↓
enterWebMode()
    ↓
mDNS + WebServer
```

**Do not put mDNS in `setupWiFi()` merely because setup resulted in a Wi-Fi connection.**
