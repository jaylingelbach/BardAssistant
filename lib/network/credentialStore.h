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

/**
 * @brief Checks whether the credential namespace can be read and its count is
 * within MAX_NETWORK_COUNT; does not create storage or validate entries.
 *
 * @return SUCCESS for a valid count (defaulting to zero if unavailable),
 * STORAGE_FAILED if the namespace cannot be opened, or INVALID_COUNT if the
 * count exceeds MAX_NETWORK_COUNT.
 */
CredentialStoreInitResult credentialStoreInit();

/**
 * @brief Persists a network, updating its password if the SSID already exists.
 *
 * Existing SSIDs can be updated even when the store is full. Writes completed
 * before a failure are not rolled back. Empty passwords are accepted, but only
 * the password key's existence is checked after writing an empty value.
 *
 * @param credential Network to save; the SSID must be nonempty.
 * @return SUCCESS on completion, INVALID_INPUT for an empty SSID, INVALID_COUNT
 * if the stored count exceeds MAX_NETWORK_COUNT, FULL if a new SSID would
 * exceed
 * that limit, STORAGE_FAILED on an open, write, or password verification
 * failure,
 * or UPDATE_COUNT_FAILED if saving the incremented count fails.
 */
CredentialStoreResult addCredential(const WiFiCredential& credential);

/**
 * @brief Reads the saved network count without checking individual entries.
 *
 * @return The count, defaulting to zero if the count cannot be read, or
 * std::nullopt if the namespace cannot be opened or the count exceeds
 * MAX_NETWORK_COUNT.
 */
std::optional<size_t> getCredentialCount();

/**
 * @brief Reads one saved network by its current position in the store.
 *
 * @param index Zero-based position; removing a network shifts later positions.
 * @return The credential, or std::nullopt if storage cannot be opened, the
 * count
 * is invalid, the index is out of range, or either credential key is missing.
 * Once both keys exist, a string read failure yields an empty field.
 */
std::optional<WiFiCredential> getCredential(size_t index);

/**
 * @brief Removes the first matching SSID and shifts later credentials down.
 *
 * Writes and removals completed before a failure are not rolled back.
 *
 * @param ssid Nonempty, case-sensitive SSID to remove.
 * @return SUCCESS on completion, INVALID_INPUT for an empty SSID, NOT_FOUND if
 * no match exists, INVALID_COUNT if the count exceeds MAX_NETWORK_COUNT,
 * STORAGE_FAILED on an open, shift, or key removal failure, or
 * UPDATE_COUNT_FAILED if saving the decremented count fails.
 */
CredentialStoreResult removeCredential(const std::string& ssid);

/**
 * @brief Attempts to clear the credential namespace and resets its count to
 * zero.
 *
 * @return SUCCESS if the namespace opens and the zero count is saved, or
 * STORAGE_FAILED otherwise. The clear operation's result is not checked, so
 * SUCCESS does not guarantee that old credential keys were erased.
 */
CredentialStoreResult clearCredentials();

#endif