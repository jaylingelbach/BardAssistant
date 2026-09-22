# Bard's Assistant — Wi-Fi Provisioning & Web Mode Strategy

## Purpose

Record the intended Wi-Fi architecture so future changes do not mix up **Wi-Fi provisioning** with **Web Mode**.

mDNS belongs to Web Mode, not Wi-Fi provisioning.

## Mental Model

Bard's Assistant is **offline-first**. Normal use does not turn on Wi-Fi or the web server.

---

## Provisioning Flow

### First-time setup (no saved credentials)

```text
IDLE
 |
 | Provisioning gesture held 2s
 v
provisioningStart()
 |
 +-- Open WiFiManager captive portal (non-blocking)
 +-- Display connection instructions
 |
 | User connects phone to "BardsAssistant" AP,
 | submits credentials at 192.168.4.1
 |
 v
provisioningPoll() → SUCCESS
 |
 v
enterWebModeAlreadyConnected()
 |
 +-- Start mDNS (bardsassistant.local)
 +-- Start WebServerManager
 |
 v
WEB MODE
```

### Re-provisioning (credentials already saved)

```text
IDLE
 |
 | Provisioning gesture held 2s
 v
hasKnownNetwork() → true
 |
 v
ProvisioningConfirmation state
 |
 | Next tap → confirm
 | Sleep tap → cancel → IDLE
 v
provisioningStart()
 ... (same as first-time setup above)
```

---

## Web Mode Flow

### Entering Web Mode (gesture)

```text
IDLE
 |
 | Web Mode gesture held 2s
 v
enterWebMode()
 |
 +-- WiFi.begin() with saved credentials
 +-- Wait for WL_CONNECTED + non-zero IP
 +-- MDNS.begin(BARDS_HOSTNAME)
 +-- WebServerManager.start()
 |
 v
WEB MODE
```

### Exiting Web Mode

```text
WEB MODE
 |
 | Web Mode gesture held 2s
 v
WebServerManager.stop()
exitWebMode()
 |
 +-- MDNS.end()
 +-- WiFi.disconnect() (credentials preserved)
 |
 v
IDLE
```

---

## Key Functions

### `provisioningStart()` / `provisioningPoll()`

Wraps WiFiManager's non-blocking captive portal. `provisioningPoll()` is called
every loop tick while in the `Provisioning` state and returns:

- `IN_PROGRESS` — portal is open, waiting for user
- `SUCCESS` — credentials submitted and WiFi connected
- `FAILED` — timeout expired, or credentials were submitted but connection failed
- `CANCELLED` — portal closed without a submission

### `enterWebMode()`

Used for the gesture-triggered Web Mode path. Calls `WiFi.begin()` with saved
credentials and waits up to 5 seconds for both `WL_CONNECTED` and a non-zero IP
before starting mDNS.

### `enterWebModeAlreadyConnected()`

Used exclusively after a successful provisioning. Skips `WiFi.begin()` entirely
since WiFiManager already holds a live connection. Only starts mDNS.

### `hasKnownNetwork()`

Calls `WiFi.mode(WIFI_STA)` before querying `wm.getWiFiIsSaved()`. The WiFi
driver must be initialized first or the query returns stale data.

### `resetWiFiSettings()`

Clears both the ESP32's own WiFi NVS store (`WiFi.disconnect(true, true)`) and
WiFiManager's NVS namespace (`wm.resetSettings()`). Both must be cleared — they
are separate stores.

---

## Responsibility Boundaries

### NetworkManager

Owns:

- Wi-Fi provisioning (captive portal lifecycle)
- Wi-Fi connection/disconnection
- mDNS lifecycle
- `BARDS_HOSTNAME` constant

### WebServerManager

Owns:

- HTTP server
- HTTP routes and request/response handling
- Web UI/API serving

Does **not** own:

- Wi-Fi provisioning
- LittleFS persistence
- Deck business logic

### Main / Application Layer

Owns:

- User-driven state transitions (`ApplicationState`)
- Deciding when to enter/exit provisioning and Web Mode
- Coordinating managers

---

## Important Invariants

**mDNS belongs to Web Mode.**
Do not start `MDNS.begin()` in provisioning code. The question to ask is:

> "Is the device entering Web Mode?"

If **no** → don't start mDNS.
If **yes** → `enterWebMode()` or `enterWebModeAlreadyConnected()` handles it.

**`hasKnownNetwork()` requires an initialized WiFi driver.**
Always ensure `WiFi.mode(WIFI_STA)` has been called before querying saved
credentials, or the result is unreliable.

**Two NVS stores, one reset.**
`wm.resetSettings()` alone does not fully clear credentials. Always pair it with
`WiFi.disconnect(true, true)`.

---

## Hostname

The hostname is defined as `BARDS_HOSTNAME` in `networkManager.h`:

```cpp
static constexpr const char *BARDS_HOSTNAME = "bardsassistant";
```

This is used for:
- The WiFiManager AP password
- `MDNS.begin()`
- Display messages

Change it in one place to update everywhere.

---

## OTA Authentication (Deferred)

ElegantOTA is integrated but has no authentication — `/update` is open to anyone
on the network. Acceptable during development on a trusted network, but must be
addressed before shipping.

When the production board is finalized, add `ElegantOTA.setAuth()` sourcing
credentials from NVS rather than hardcoding them.

### ElegantOTA License (AGPL-3.0)

ElegantOTA is AGPL-3.0. Private use on your own device carries no obligation.
If you distribute firmware to others, AGPL requires making the complete source
available to recipients. Before shipping to anyone else, either open-source
under a compatible license or purchase the ElegantOTA Pro commercial license.
