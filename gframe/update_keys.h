#ifndef UPDATE_KEYS_H
#define UPDATE_KEYS_H

#include <cstddef>
#include <cstdint>

// The public halves of the two client-update signing keys — DEDICATED to
// the updater, separate from ygo::banlist::TRUSTED_KEYS and
// ygo::title::TRUSTED_KEYS. Same reasoning as title_keys.h's separation from
// banlist_keys.h, but the stakes here are higher: a leaked banlist key lets
// someone publish a wrong point list (revoke, republish); a leaked update
// key lets someone run code on every player's machine. That is why this
// pair gets its own custody and its own rotation schedule, independent of
// the other two (design/client-update.md, "Chiave separata da quella della
// banlist").
//
// NEITHER KEY BELOW HAS BEEN GENERATED YET. Both arrays are all-zero
// placeholders. All-zero is not a usable Ed25519 public key: no signature
// will ever verify against it, so ygo::update::AnyTrustedKeyConfigured()
// (update_verify.h) reads these arrays and reports false, and the updater
// fails closed (design/client-update.md, "Fail-closed") until a real
// operational key is generated and pasted in here.
//
// When the real keys exist:
//   1. Generate an Ed25519 keypair offline (never inside an agent context —
//      decision D25, same rule as the banlist and title keys).
//      operational: lives wherever the manifest signer runs (a GitHub
//      Action secret, mirroring banlist_keys.h's operational key).
//      reserve: generated at the same time, kept offline, never loaded by
//      the signer — it exists only so a client already in the field can
//      accept it after a rotation, exactly like banlist's reserve key.
//   2. Paste each 32-byte public half below, replacing the zeroed array,
//      with a base64 comment above it (see banlist_keys.h for the format
//      other code in this repo already uses).
//   3. The private halves NEVER go in this repo, in any form, for any
//      reason — see this repo's CLAUDE.md.

namespace ygo::update {

inline constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;
inline constexpr size_t ED25519_SIGNATURE_SIZE = 64;

// operational — NOT YET GENERATED. All-zero placeholder (see header comment).
inline constexpr uint8_t TRUSTED_KEY_OPERATIONAL[ED25519_PUBLIC_KEY_SIZE] = {};

// reserve — NOT YET GENERATED. All-zero placeholder (see header comment).
inline constexpr uint8_t TRUSTED_KEY_RESERVE[ED25519_PUBLIC_KEY_SIZE] = {};

inline constexpr const uint8_t* TRUSTED_KEYS[] = {
	TRUSTED_KEY_OPERATIONAL,
	TRUSTED_KEY_RESERVE,
};

}

#endif //UPDATE_KEYS_H
