#ifndef BANLIST_VERIFY_H
#define BANLIST_VERIFY_H

// Signature verification and parsing of the signed banlist artifact.
//
// This header deliberately includes NOTHING from the rest of gframe: no
// utils.h, no irrlicht, no curl, no globals. That is what makes it linkable
// into the standalone test binary (tests/premake5.lua) and testable without a
// network, a window or a game. Keep it that way — the moment it needs a
// gframe header, the security-relevant half of the updater stops being
// testable in isolation and starts being tested by hand, which means not
// tested.
//
// Contract of the artifact: the artifact contract (vault side).
// Client behaviour: design/banlist-distribution.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ygo::banlist {

struct Entry {
	uint32_t id = 0;
	int limit = 3;      // 0..3 — always present in the artifact, never defaulted
	int points = 0;
	std::string name;
	std::string macro;  // closed enumeration of ten values (D60)
	std::string source; // "custom" | "genesys"
	std::string reason; // empty iff !has_reason
	bool has_reason = false;
};

struct Payload {
	int format_version = 0;
	std::string generated_at;
	std::string expires_at;   // advisory only, never blocks play (§5)
	std::string license;
	std::string author;
	std::string url;
	std::vector<Entry> entries;
};

enum class VerifyStatus {
	Ok,
	BadSignature,   // no trusted key validates these bytes, or the .sig is not 64 bytes
	MalformedJson,  // not parseable as JSON at all
	SchemaViolation // parseable, but not the artifact we contracted for
};

// Verifies the detached Ed25519 signature of `document` against every key in
// TRUSTED_KEYS. `signature` must be exactly 64 raw bytes (no base64, no
// newline) — anything else is a bad signature, not a parse error.
bool VerifySignature(const std::string& document, const std::string& signature);

// Same, against an explicit key set. This exists so the tests can sign with a
// throwaway keypair instead of needing the real private key — which they could
// never have (D25). The shipping client always goes through the overload
// above, which is hardwired to TRUSTED_KEYS; nothing in the client may pass
// its own keys in.
bool VerifySignatureWith(const std::string& document, const std::string& signature,
						 const uint8_t* const* keys, size_t key_count);

// Parses bytes whose signature HAS ALREADY BEEN VERIFIED. Never call this on
// bytes straight off the network: the whole point of a detached signature is
// that nlohmann/json never sees a byte we have not authenticated.
// On failure `error` carries a one-line reason for the log, `out` is untouched.
VerifyStatus Parse(const std::string& document, Payload& out, std::string& error);

// The only function callers outside this module should use: verifies first,
// parses second, and cannot be made to do it in the other order.
VerifyStatus VerifyAndParse(const std::string& document, const std::string& signature,
							Payload& out, std::string& error);

enum class VersionDecision {
	Accept,        // strictly newer — stage it
	AlreadyCurrent,// same version — no action, no notification
	Rollback       // older — refuse and log; a downgrade attempt is not a no-op
};

// Anti-rollback, applied AFTER the signature has verified (§3). Pure on
// purpose: the rule is worth a test of its own, and it has none of the
// ambient state the fetch does.
VersionDecision CompareVersion(int incoming_version, int active_version);

}

#endif //BANLIST_VERIFY_H
