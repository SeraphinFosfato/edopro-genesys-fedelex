#ifndef BANLIST_KEYS_H
#define BANLIST_KEYS_H

#include <cstddef>
#include <cstdint>

// The public halves of the two signing keys, generated 2026-09-10.
// Public by definition: there is nothing to protect here. The private halves
// have never passed through an agent context (decision D25) — the operational
// one lives in a GitHub Secret, the reserve one offline and never in CI.
//
// Two keys, not one, is what makes rotation a release of the vault instead of
// a reinstall for every player: a signature valid under ANY entry of this
// array is accepted (see design/fork-edopro/banlist-distribution.md §2).

namespace ygo::banlist {

inline constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;
inline constexpr size_t ED25519_SIGNATURE_SIZE = 64;

// operational — signs every current release
// base64: u/hxqvtR3vU+3Om/ayCn6pqHlTi05uoZSmjb+Bpf0V8=
inline constexpr uint8_t TRUSTED_KEY_OPERATIONAL[ED25519_PUBLIC_KEY_SIZE] = {
	0xbb, 0xf8, 0x71, 0xaa, 0xfb, 0x51, 0xde, 0xf5, 0x3e, 0xdc, 0xe9, 0xbf,
	0x6b, 0x20, 0xa7, 0xea, 0x9a, 0x87, 0x95, 0x38, 0xb4, 0xe6, 0xea, 0x19,
	0x4a, 0x68, 0xdb, 0xf8, 0x1a, 0x5f, 0xd1, 0x5f
};

// reserve — inert until it takes over at a rotation
// base64: IrmMhJj7rED/jHyuDROPvd7saahv2H4txV3iGUbOUxc=
inline constexpr uint8_t TRUSTED_KEY_RESERVE[ED25519_PUBLIC_KEY_SIZE] = {
	0x22, 0xb9, 0x8c, 0x84, 0x98, 0xfb, 0xac, 0x40, 0xff, 0x8c, 0x7c, 0xae,
	0x0d, 0x13, 0x8f, 0xbd, 0xde, 0xec, 0x69, 0xa8, 0x6f, 0xd8, 0x7e, 0x2d,
	0xc5, 0x5d, 0xe2, 0x19, 0x46, 0xce, 0x53, 0x17
};

inline constexpr const uint8_t* TRUSTED_KEYS[] = {
	TRUSTED_KEY_OPERATIONAL,
	TRUSTED_KEY_RESERVE,
};

}

#endif //BANLIST_KEYS_H
