#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

enum class WebModeResult { SUCCESS, CONNECTION_FAILED, MDNS_FAILED };
enum class DisconnectModeResult { SUCCESS, DISCONNECT_FAILED };
enum class SetupModeResult { SUCCESS, SETUP_FAILED };

SetupModeResult setupWiFi();
DisconnectModeResult disconnectWiFi();
WebModeResult enterWebMode();

#endif
