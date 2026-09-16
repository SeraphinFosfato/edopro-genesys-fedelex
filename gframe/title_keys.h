#ifndef TITLE_KEYS_H
#define TITLE_KEYS_H

#include <cstddef>
#include <cstdint>

// The public halves of the two title-signing keys — DEDICATED (D79),
// separate from TRUSTED_KEYS in banlist_keys.h. The two arrays must never be
// mixed: this array verifies titles/revocations/suspensions, TRUSTED_KEYS
// verifies the banlist artifact. Domain separation (title_verify.h) already
// stops the same bytes from being misread across the two message families;
// keeping the key arrays apart is the second lock, and costs nothing.
//
// The private halves have never passed through an agent context (D25). The
// operational one lives with the bot process (design/bot-telegram); the
// reserve one is offline and inert until a rotation. Generated 2026-09-16.

namespace ygo::title {

// Ed25519 public keys are always 32 bytes, regardless of which message
// family they verify — same value as banlist::ED25519_PUBLIC_KEY_SIZE
// (banlist_keys.h), defined independently rather than pulled in from
// there: this array has nothing to do with banlist keys, and the
// coincidence in size is not a relationship worth a cross-module include.
inline constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;

// operational — signs every title/revocation/suspension issued today
// base64: ZnPmOrsTZkR8Cch23+RLWABc5BKGn68/iREnWMUD990=
inline constexpr uint8_t TRUSTED_KEY_OPERATIONAL[ED25519_PUBLIC_KEY_SIZE] = {
	0x66, 0x73, 0xe6, 0x3a, 0xbb, 0x13, 0x66, 0x44, 0x7c, 0x09, 0xc8, 0x76,
	0xdf, 0xe4, 0x4b, 0x58, 0x00, 0x5c, 0xe4, 0x12, 0x86, 0x9f, 0xaf, 0x3f,
	0x89, 0x11, 0x27, 0x58, 0xc5, 0x03, 0xf7, 0xdd
};

// reserve — inert until it takes over at a rotation. Lives ONLY here: the
// bot process never loads it (design/fork-edopro/access-control.md §11) —
// it exists to be ACCEPTED by clients already in the field, not to sign.
// base64: KNlrCLkMBM9xlc87IMNnExjzg5iolQf+bvt0gHwjbjo=
inline constexpr uint8_t TRUSTED_KEY_RESERVE[ED25519_PUBLIC_KEY_SIZE] = {
	0x28, 0xd9, 0x6b, 0x08, 0xb9, 0x0c, 0x04, 0xcf, 0x71, 0x95, 0xcf, 0x3b,
	0x20, 0xc3, 0x67, 0x13, 0x18, 0xf3, 0x83, 0x98, 0xa8, 0x95, 0x07, 0xfe,
	0x6e, 0xfb, 0x74, 0x80, 0x7c, 0x23, 0x6e, 0x3a
};

inline constexpr const uint8_t* TRUSTED_KEYS[] = {
	TRUSTED_KEY_OPERATIONAL,
	TRUSTED_KEY_RESERVE,
};

}

#endif //TITLE_KEYS_H
