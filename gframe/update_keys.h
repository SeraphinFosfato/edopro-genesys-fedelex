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
// The OPERATIONAL key exists since 2026-09-26. The RESERVE key is still an
// all-zero placeholder, and that is a known gap, not a finished state: a
// reserve can only be added by shipping a new binary, so until one is
// generated and compiled in, rotating the operational key means every player
// reinstalls by hand. All-zero is not a usable Ed25519 public key — no
// signature will ever verify against it — so the zeroed reserve is inert
// rather than dangerous, and AnyTrustedKeyConfigured() (update_verify.h)
// reports true on the strength of the operational key alone.
//
// When the reserve key is generated:
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

// operational — signs every update manifest. Generated 2026-09-26, offline,
// with banlist/scripts/keygen_banlist.py; the private half was written to a
// 0600 file outside any git repository and has never passed through an agent
// context (decision D25). Verified distinct from all four other public keys
// compiled into this client (banlist operational/reserve, title
// operational/reserve).
// base64: 0ZLmdALp6NhQESb0OW83iprD2PaCsWOzxq1MwxFUkoQ=
inline constexpr uint8_t TRUSTED_KEY_OPERATIONAL[ED25519_PUBLIC_KEY_SIZE] = {
	0xd1, 0x92, 0xe6, 0x74, 0x02, 0xe9, 0xe8, 0xd8, 0x50, 0x11, 0x26, 0xf4,
	0x39, 0x6f, 0x37, 0x8a, 0x9a, 0xc3, 0xd8, 0xf6, 0x82, 0xb1, 0x63, 0xb3,
	0xc6, 0xad, 0x4c, 0xc3, 0x11, 0x54, 0x92, 0x84
};

// reserve — NOT YET GENERATED. All-zero placeholder (see header comment).
inline constexpr uint8_t TRUSTED_KEY_RESERVE[ED25519_PUBLIC_KEY_SIZE] = {};

inline constexpr const uint8_t* TRUSTED_KEYS[] = {
	TRUSTED_KEY_OPERATIONAL,
	TRUSTED_KEY_RESERVE,
};

}

#endif //UPDATE_KEYS_H
