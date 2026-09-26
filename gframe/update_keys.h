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
// Both keys were generated on 2026-09-26, offline, and both are compiled in
// below. Two keys rather than one is what makes a rotation a release of the
// manifest instead of a reinstall for every player: a signature valid under
// ANY entry of TRUSTED_KEYS is accepted, so the reserve can take over
// without touching clients already in the field. It only works because the
// reserve shipped in the same binary as the operational key — a reserve
// added later would reach nobody who did not update.
//
// The rules that keep the pair worth having:
//   1. The private halves NEVER go in this repo, in any form, for any
//      reason — see this repo's CLAUDE.md — and never pass through an agent
//      context (decision D25, same rule as the banlist and title keys).
//   2. Only the OPERATIONAL private half may reach the manifest signer (a
//      GitHub Action secret, mirroring banlist_keys.h's operational key).
//      The RESERVE private half stays offline and signs nothing; loading it
//      into CI spends the rotation before it is needed and leaves the pair
//      with no way out.
//   3. At a rotation, the reserve is promoted here and a NEW reserve is
//      generated in the same release, so the client always ships two live
//      keys. Shipping one is the state this file must never return to.

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

// reserve — inert until it takes over at a rotation. Generated 2026-09-26
// alongside the operational key, same custody rules, and verified distinct
// from all five other public keys compiled into this client. Its private
// half must NEVER be loaded by the signer or placed in a GitHub Secret: it
// exists only so a client already in the field can accept a signature made
// with it on the day the operational key is rotated.
// base64: WgbJ4/PKwQAs57FwRm5i2S1UGvpojyKT4FRAQ+3ysNQ=
inline constexpr uint8_t TRUSTED_KEY_RESERVE[ED25519_PUBLIC_KEY_SIZE] = {
	0x5a, 0x06, 0xc9, 0xe3, 0xf3, 0xca, 0xc1, 0x00, 0x2c, 0xe7, 0xb1, 0x70,
	0x46, 0x6e, 0x62, 0xd9, 0x2d, 0x54, 0x1a, 0xfa, 0x68, 0x8f, 0x22, 0x93,
	0xe0, 0x54, 0x40, 0x43, 0xed, 0xf2, 0xb0, 0xd4
};

inline constexpr const uint8_t* TRUSTED_KEYS[] = {
	TRUSTED_KEY_OPERATIONAL,
	TRUSTED_KEY_RESERVE,
};

}

#endif //UPDATE_KEYS_H
