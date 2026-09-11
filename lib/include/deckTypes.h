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

struct DeckEntryResult {
  bool success;
  std::optional<DeckEntry> entry;
  DeckEntryResult(bool success, DeckEntry entry)
      : success(success), entry(entry) {}
  DeckEntryResult(bool success, std::nullopt_t)
      : success(success), entry(std::nullopt) {}
};

struct DeleteDeckEntryResult {
  bool success;
};

#endif // DECK_TYPES_H
