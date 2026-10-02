#include "credentialStore.h"

#include <Preferences.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "persist_keys.h"

static bool writeVerifiedPassword(Preferences& prefs, const std::string& key,
                                  const std::string& password) {
  if (password.empty()) {
    if (prefs.putString(key.c_str(), password.c_str()) == 0) {
      return false;
    }

    if (!prefs.isKey(key.c_str())) {
      return false;
    }
  } else {
    // write, read back compare.

    if (prefs.putString(key.c_str(), password.c_str()) == 0) {
      return false;
    }

    String savedPass = prefs.getString(key.c_str(), "");

    std::string pass = savedPass.c_str();

    if (pass != password) {
      return false;
    }
  }
  return true;
}

CredentialStoreInitResult credentialStoreInit() {
  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, true)) {
    return CredentialStoreInitResult::STORAGE_FAILED;
  }

  if (prefs.getUChar(NVS_KEY_COUNT, 0) > MAX_NETWORK_COUNT) {
    prefs.end();
    return CredentialStoreInitResult::INVALID_COUNT;
  }

  prefs.end();

  return CredentialStoreInitResult::SUCCESS;
}

CredentialStoreResult addCredential(const WiFiCredential& credential) {
  if (credential.ssid.empty()) {
    return CredentialStoreResult::INVALID_INPUT;
  }

  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, false)) {
    return CredentialStoreResult::STORAGE_FAILED;
  }

  uint8_t count = prefs.getUChar(NVS_KEY_COUNT, 0);

  if (count > MAX_NETWORK_COUNT) {
    prefs.end();
    return CredentialStoreResult::INVALID_COUNT;
  }

  // before writing, check for duplicates.

  for (uint8_t i = 0; i < count; i++) {
    std::string ssidKey = std::string(NVS_KEY_SSID) + std::to_string(i);

    String ssidValue = prefs.getString(ssidKey.c_str(), "");

    std::string ssid = ssidValue.c_str();

    // ssid exists, update password
    if (ssid == credential.ssid) {
      std::string passKey = std::string(NVS_KEY_PASS) + std::to_string(i);

      bool passwordWriteVerified =
          writeVerifiedPassword(prefs, passKey, credential.password);

      if (!passwordWriteVerified) {
        prefs.end();
        return CredentialStoreResult::STORAGE_FAILED;
      }
      prefs.end();
      return CredentialStoreResult::SUCCESS;
    }
  }

  if (count >= MAX_NETWORK_COUNT) {
    prefs.end();
    return CredentialStoreResult::FULL;
  }

  std::string ssidKey = std::string(NVS_KEY_SSID) + std::to_string(count);

  std::string passKey = std::string(NVS_KEY_PASS) + std::to_string(count);

  if (prefs.putString(ssidKey.c_str(), credential.ssid.c_str()) == 0) {
    prefs.end();
    return CredentialStoreResult::STORAGE_FAILED;
  }
  bool passwordWriteVerified =
      writeVerifiedPassword(prefs, passKey, credential.password);

  if (!passwordWriteVerified) {
    prefs.end();
    return CredentialStoreResult::STORAGE_FAILED;
  }

  if (prefs.putUChar(NVS_KEY_COUNT, count + 1) == 0) {
    prefs.end();
    return CredentialStoreResult::UPDATE_COUNT_FAILED;
  }

  prefs.end();
  return CredentialStoreResult::SUCCESS;
}

std::optional<WiFiCredential> getCredential(size_t index) {
  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, true)) {
    return std::nullopt;
  }

  uint8_t count = prefs.getUChar(NVS_KEY_COUNT, 0);

  if (index >= count || count > MAX_NETWORK_COUNT) {
    prefs.end();
    return std::nullopt;
  }
  std::string ssidKey = std::string(NVS_KEY_SSID) + std::to_string(index);

  std::string passKey = std::string(NVS_KEY_PASS) + std::to_string(index);

  if (!prefs.isKey(ssidKey.c_str())) {
    prefs.end();
    return std::nullopt;
  }
  if (!prefs.isKey(passKey.c_str())) {
    prefs.end();
    return std::nullopt;
  }

  String ssidValue = prefs.getString(ssidKey.c_str(), "");

  std::string ssid = ssidValue.c_str();

  String passwordValue = prefs.getString(passKey.c_str(), "");

  std::string password = passwordValue.c_str();

  prefs.end();

  WiFiCredential credential{ssid, password};
  return credential;
}

// Current implementation detects the failure, but doesn't roll back
// earlier writes. That is for the first pass. A
// future improvement could use a recovery marker or a temporary copy of the
// credential list to make removal more resilient to interrupted operations.

CredentialStoreResult removeCredential(const std::string& ssid) {
  if (ssid.empty()) {
    return CredentialStoreResult::INVALID_INPUT;
  }

  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, false)) {
    return CredentialStoreResult::STORAGE_FAILED;
  }

  uint8_t count = prefs.getUChar(NVS_KEY_COUNT, 0);

  if (count == 0) {
    prefs.end();
    return CredentialStoreResult::NOT_FOUND;
  }

  if (count > MAX_NETWORK_COUNT) {
    prefs.end();
    return CredentialStoreResult::INVALID_COUNT;
  }

  for (uint8_t i = 0; i < count; i++) {
    std::string ssidKey = std::string(NVS_KEY_SSID) + std::to_string(i);
    std::string passwordKey = std::string(NVS_KEY_PASS) + std::to_string(i);

    String ssidValue = prefs.getString(ssidKey.c_str(), "");

    std::string storedSSID = ssidValue.c_str();

    if (storedSSID == ssid) {
      uint8_t targetIndex = i;
      // last index - remove.
      if (targetIndex == count - 1) {
        bool successSSIDRemoval = prefs.remove(ssidKey.c_str());
        bool successPasswordRemoval = prefs.remove(passwordKey.c_str());

        if (successSSIDRemoval && successPasswordRemoval) {
          if (prefs.putUChar(NVS_KEY_COUNT, count - 1) == 0) {
            prefs.end();
            return CredentialStoreResult::UPDATE_COUNT_FAILED;
          }
          prefs.end();
          return CredentialStoreResult::SUCCESS;

        } else {
          prefs.end();
          return CredentialStoreResult::STORAGE_FAILED;
        }
      }
      // Shifting branch
      for (uint8_t j = targetIndex; j < count - 1; j++) {
        std::string sourceSSIDKey =
            std::string(NVS_KEY_SSID) + std::to_string(j + 1);

        std::string sourcePasswordKey =
            std::string(NVS_KEY_PASS) + std::to_string(j + 1);

        // before reading validate
        if (!prefs.isKey(sourceSSIDKey.c_str()) ||
            !prefs.isKey(sourcePasswordKey.c_str())) {
          prefs.end();
          return CredentialStoreResult::STORAGE_FAILED;
        }

        String ssidValueCopy = prefs.getString(sourceSSIDKey.c_str(), "");

        if (ssidValueCopy.isEmpty()) {
          prefs.end();
          return CredentialStoreResult::STORAGE_FAILED;
        }

        String passwordValueCopy =
            prefs.getString(sourcePasswordKey.c_str(), "");

        std::string destinationSSIDKey =
            std::string(NVS_KEY_SSID) + std::to_string(j);

        std::string destinationPasswordKey =
            std::string(NVS_KEY_PASS) + std::to_string(j);

        if (prefs.putString(destinationSSIDKey.c_str(),
                            ssidValueCopy.c_str()) == 0) {
          prefs.end();

          return CredentialStoreResult::STORAGE_FAILED;
        }

        // write, read back compare.

        String savedSSID = prefs.getString(destinationSSIDKey.c_str(), "");

        std::string savedSSIDValue = savedSSID.c_str();

        std::string expectedSSID = ssidValueCopy.c_str();

        std::string passwordToWrite = passwordValueCopy.c_str();

        if (savedSSIDValue != expectedSSID) {
          prefs.end();
          return CredentialStoreResult::STORAGE_FAILED;
        }

        // password can be empty
        bool passwordWriteVerified = writeVerifiedPassword(
            prefs, destinationPasswordKey, passwordToWrite);

        if (!passwordWriteVerified) {
          prefs.end();
          return CredentialStoreResult::STORAGE_FAILED;
        }
      }

      // remove the last
      std::string lastSSIDKey =
          std::string(NVS_KEY_SSID) + std::to_string(count - 1);

      std::string lastPasswordKey =
          std::string(NVS_KEY_PASS) + std::to_string(count - 1);

      bool successSSIDRemoval = prefs.remove(lastSSIDKey.c_str());

      bool successPasswordRemoval = prefs.remove(lastPasswordKey.c_str());

      if (successSSIDRemoval && successPasswordRemoval) {
        if (prefs.putUChar(NVS_KEY_COUNT, count - 1) == 0) {
          prefs.end();
          return CredentialStoreResult::UPDATE_COUNT_FAILED;
        }
        prefs.end();
        return CredentialStoreResult::SUCCESS;

      } else {
        prefs.end();
        return CredentialStoreResult::STORAGE_FAILED;
      }
    }
  }
  prefs.end();
  return CredentialStoreResult::NOT_FOUND;
}

CredentialStoreResult clearCredentials() {
  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, false)) {
    return CredentialStoreResult::STORAGE_FAILED;
  }

  prefs.clear();

  if (prefs.putUChar(NVS_KEY_COUNT, 0) == 0) {
    prefs.end();
    return CredentialStoreResult::STORAGE_FAILED;
  }

  prefs.end();

  return CredentialStoreResult::SUCCESS;
}

std::optional<size_t> getCredentialCount() {
  Preferences prefs;

  if (!prefs.begin(NVS_NS_CREDS, true)) {
    return std::nullopt;
  }

  uint8_t count = prefs.getUChar(NVS_KEY_COUNT, 0);

  if (count > MAX_NETWORK_COUNT) {
    prefs.end();
    return std::nullopt;
  }

  prefs.end();

  return count;
}