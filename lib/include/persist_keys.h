#ifndef PERSIST_KEYS_H
#define PERSIST_KEYS_H

// Shared NVS namespace used by BOTH:
// - main.cpp (wake detection via "slept")
// - insults.cpp (persist/restore insult state)
#define NVS_NS "bards"

// Credential store (credentialStore.cpp)
#define NVS_NS_CREDS "bards_creds"
#define NVS_KEY_COUNT "cnt"
#define NVS_KEY_SSID "ssid"  // appended with index: ssid0, ssid1, ...
#define NVS_KEY_PASS "pass"  // appended with index: pass0, pass1, ...

#endif  // PERSIST_KEYS_H
