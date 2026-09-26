#ifndef UPDATE_VERIFY_H
#define UPDATE_VERIFY_H

// Signature verification and parsing of the signed client-update manifest.
//
// This header deliberately includes NOTHING from the rest of gframe: no
// utils.h, no irrlicht, no curl, no globals. Same reason as
// banlist_verify.h — linkable into the standalone test binary
// (tests/premake5.lua) and testable without a network, a window or a game.
//
// Structural note, worth stating once because it looks similar to both
// siblings and is neither: like banlist_verify.h (and unlike title_verify.h)
// this signs the RAW DOCUMENT BYTES, never fields reconstructed after
// parsing — verify first, parse second, so the JSON parser never sees a
// byte nobody has authenticated yet (design/client-update.md, "Si firma il
// manifesto, sui byte grezzi"). UNLIKE banlist_verify.h, the signed message
// is not the raw document alone: it is UPDATE_DOMAIN + "\n" + the raw
// document bytes. The domain label is a second, independent lock on top of
// the update key already being its own dedicated array (update_keys.h):
// without it, a document that happens to verify under an update key for
// some other reason (key reuse, a future message type signed by the same
// custodian) would silently double as a manifest. Prepending a fixed label
// costs nothing and does not require parsing anything first — it is still
// exactly one verify-before-parse step.
//
// Contract: design/client-update.md.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "update_keys.h" // ED25519_PUBLIC_KEY_SIZE, ED25519_SIGNATURE_SIZE, TRUSTED_KEYS

namespace ygo::update {

// Domain-separation label, byte-identical in spirit to title_verify.h's
// TITLE_DOMAIN/REVOCATION_DOMAIN, and REQUIRED to be distinct from those and
// from any label the banlist side might ever adopt (design/client-update.md
// calls out an explicit cross-domain test on both sides — see
// tests/update_tests.cpp).
inline constexpr const char* UPDATE_DOMAIN = "fedelex-update-v1";

struct ManifestFile {
	std::string name;
	std::string url;
	std::string sha256; // 64 lowercase hex chars — the only thing that authorizes installing this file
	std::string md5;    // optional, upstream compatibility only (see .cpp); never a verification
};

struct Manifest {
	int version = 0;
	// 0 means the key was absent from the wire manifest — "nessuna soglia
	// dichiarata" (design/client-update.md §9). A manifest that omits it is
	// valid and closes nothing; one that carries a value keeps every client
	// that is not below it free to host and join online. Same sentinel
	// convention as banlist::Payload::points_budget (banlist_verify.h):
	// real thresholds are >= 1, so 0 is never ambiguous with a real value.
	int min_supported = 0;
	std::vector<ManifestFile> files;
};

enum class VerifyStatus {
	Ok,
	BadSignature,   // no trusted key validates UPDATE_DOMAIN + "\n" + document, or the signature is not 64 raw bytes
	MalformedJson,  // not parseable as JSON at all
	SchemaViolation // parseable, but not the manifest we contracted for
};

// True iff at least one entry of TRUSTED_KEYS (update_keys.h) is not the
// all-zero placeholder. Both keys are unfilled as of this writing (no
// production update key has been generated yet — see update_keys.h), so
// this returns false today, and callers use it to fail closed
// (design/client-update.md, "Fail-closed": no compiled key means the
// updater does not update, and says so) instead of silently emitting
// BadSignature for every single check, which would look identical to a
// tampered manifest in the log.
bool AnyTrustedKeyConfigured();

// Verifies the detached Ed25519 signature of `document` (see the domain
// note above) against every key in TRUSTED_KEYS. `signature` must be
// exactly 64 raw bytes (no base64, no newline) — anything else is a bad
// signature, not a parse error.
bool VerifySignature(const std::string& document, const std::string& signature);

// Same, against an explicit key set. This exists so the tests can sign with
// a throwaway keypair instead of needing the real private key — which does
// not exist yet (update_keys.h) and, once generated, could never live in a
// test binary either way (D25). The shipping client always goes through the
// overload above, hardwired to TRUSTED_KEYS.
bool VerifySignatureWith(const std::string& document, const std::string& signature,
						 const uint8_t* const* keys, size_t key_count);

// Parses bytes whose signature HAS ALREADY BEEN VERIFIED. Never call this on
// bytes straight off the network: the whole point of a detached signature is
// that nlohmann/json never sees a byte we have not authenticated.
// On failure `error` carries a one-line reason for the log, `out` is untouched.
VerifyStatus Parse(const std::string& document, Manifest& out, std::string& error);

// The only function callers outside this module should use: verifies first,
// parses second, and cannot be made to do it in the other order.
VerifyStatus VerifyAndParse(const std::string& document, const std::string& signature,
							Manifest& out, std::string& error);

enum class VersionDecision {
	Accept,        // strictly newer — install it
	AlreadyCurrent,// same version — no action, no notification
	Rollback       // older — refuse and log; a downgrade attempt is not a no-op
};

// Anti-rollback, applied AFTER the signature has verified. Pure on purpose,
// same reasoning as banlist::CompareVersion: the rule is worth a test of its
// own with none of the ambient state a real fetch has.
VersionDecision CompareVersion(int incoming_version, int installed_version);

// design/client-update.md §9: "il manifesto puo' portare un campo
// min_supported, e un client sotto quella versione si rifiuta di ospitare e
// di entrare in stanze online". Pure, same reasoning as CompareVersion — no
// network, no window, no globals, just the two integers the decision
// actually depends on.
//
// `min_supported` is the manifest's field verbatim (0 == absent, see
// Manifest::min_supported above): a manifest that never declares a floor
// never closes anything. `client_version` is THIS client's own dedicated,
// monotonically increasing build number (see client_update_version.h) —
// deliberately NOT config.h's CLIENT_VERSION (that is the network handshake
// number and travels byte-for-byte with HostInfo, see network.h) and NOT
// EDOPRO_VERSION_MAJOR/MINOR/PATCH (upstream Project Ignis' own numbering,
// which we follow but do not govern).
bool IsClientSupported(int min_supported, int client_version);

}

#endif //UPDATE_VERIFY_H
