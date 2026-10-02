#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

static constexpr uint8_t MAX_NETWORK_COUNT = 10;

enum class CredentialStoreResult {
  SUCCESS,
  FULL,
  NOT_FOUND,
  STORAGE_FAILED,
  UPDATE_COUNT_FAILED,
  INVALID_INPUT,
  INVALID_COUNT
};
enum class CredentialStoreInitResult { SUCCESS, STORAGE_FAILED, INVALID_COUNT };

struct WiFiCredential {
  std::string ssid;
  std::string password;
};

CredentialStoreInitResult credentialStoreInit();

CredentialStoreResult addCredential(const WiFiCredential& credential);

std::optional<size_t> getCredentialCount();

std::optional<WiFiCredential> getCredential(size_t index);

CredentialStoreResult removeCredential(const std::string& ssid);

CredentialStoreResult clearCredentials();

#endif