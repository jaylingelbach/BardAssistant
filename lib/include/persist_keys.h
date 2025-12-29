#ifndef PERSIST_KEYS_H
#define PERSIST_KEYS_H

// Shared NVS namespace used by BOTH:
// - main.cpp (wake detection via "slept")
// - insults.cpp (persist/restore insult state)
inline constexpr const char *NVS_NS = "bards";

#endif // PERSIST_KEYS_H
