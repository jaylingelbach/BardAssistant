#ifndef DECK_TYPES_H
#define DECK_TYPES_H

#include <optional>
#include <string>
#include <cstdint>

/** @brief A single entry in any deck (insults, bardic inspiration, etc.). */
struct DeckEntry {
  uint32_t id;       ///< Persistent identifier, unique within the deck.
  std::string text;  ///< Display text shown on the e-ink screen.
  std::string source;///< Attribution or source label (may be empty).

  DeckEntry() = default;
  DeckEntry(uint32_t id, std::string text, std::string source)
      : id(id), text(text), source(source) {}
};

/** @brief Failure reason for a create or edit operation. */
enum class EntryFailReason { None, NotFound, PersistenceFailed };

/**
 * @brief Result of a create or edit deck-entry operation.
 *
 * On success, `entry` holds the created or updated entry. On failure, `reason`
 * distinguishes a missing entry (NotFound) from a storage error
 * (PersistenceFailed).
 */
struct DeckEntryResult {
  bool success;
  std::optional<DeckEntry> entry;
  EntryFailReason reason = EntryFailReason::None;
  DeckEntryResult(bool success, DeckEntry entry)
      : success(success), entry(entry) {}
  DeckEntryResult(bool success, std::nullopt_t,
                  EntryFailReason reason = EntryFailReason::None)
      : success(success), entry(std::nullopt), reason(reason) {}
};

/** @brief Failure reason for a delete operation. */
enum class DeleteFailReason { None, NotFound, PersistenceFailed };

/**
 * @brief Result of a delete deck-entry operation.
 *
 * On failure, `reason` distinguishes a missing entry (NotFound) from a storage
 * error (PersistenceFailed).
 */
struct DeleteDeckEntryResult {
  bool success;
  DeleteFailReason reason = DeleteFailReason::None;
};

#endif // DECK_TYPES_H
