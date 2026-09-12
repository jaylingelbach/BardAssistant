#ifndef DECK_TYPES_H
#define DECK_TYPES_H

#include <optional>
#include <string>
#include <cstdint>

struct DeckEntry {
  uint32_t id;
  std::string text;
  std::string source;

  DeckEntry() = default;
  DeckEntry(uint32_t id, std::string text, std::string source)
      : id(id), text(text), source(source) {}
};

enum class EntryFailReason { None, NotFound, PersistenceFailed };

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

enum class DeleteFailReason { None, NotFound, PersistenceFailed };

struct DeleteDeckEntryResult {
  bool success;
  DeleteFailReason reason = DeleteFailReason::None;
};

#endif // DECK_TYPES_H
