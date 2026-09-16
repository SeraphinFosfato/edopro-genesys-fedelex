#ifndef TITLE_VERIFY_H
#define TITLE_VERIFY_H

// Signature verification and parsing of the title-API responses: title,
// revocation and suspension (D79/D80, D102-D104).
//
// This header deliberately includes NOTHING from the rest of gframe: no
// utils.h, no irrlicht, no curl, no globals. Same reason as banlist_verify.h
// — linkable into the standalone test binary and testable without a
// network, a window or a game.
//
// Structural difference from banlist_verify.h, worth stating once: the
// banlist artifact signs the RAW DOCUMENT BYTES, so that module verifies
// first and only ever parses bytes already authenticated. The title API
// signs a CANONICAL STRING REBUILT FROM PARSED FIELDS (bot side:
// src/util/titleKey.ts, signWithDomain), so here parsing necessarily comes
// before the signature can even be checked — there is no way to know what
// bytes were signed without first reading which fields the response claims
// to carry. This is not a laxer posture: every field access below is
// find()+is_*()-guarded before use (same discipline as banlist_verify.cpp),
// a malformed body can only ever produce "no effect" per the caller's
// contract, and nothing here treats a value as authentic before
// VerifyWithDomain succeeds against it.
//
// Design: design/fork-edopro/access-control.md (private vault — this repo
// only ever gets code and sober "what", the strategy "why" stays there; see
// this repo's own CLAUDE.md).
// Wire contract: bot-telegram src/api/titles.ts, src/util/titleKey.ts.

#include <cstddef>
#include <cstdint>
#include <string>

namespace ygo::title {

inline constexpr size_t ED25519_SIGNATURE_SIZE = 64;

// Domain-separation labels (RFC 8410-style), byte-identical to the bot's
// TITLE_DOMAIN/REVOCATION_DOMAIN (src/api/titles.ts). Without these, the
// same signed bytes could be re-read as the other message type — see the
// dedicated cross-domain test on both the bot and this module.
inline constexpr const char* TITLE_DOMAIN = "fedelex-title-v1";
inline constexpr const char* REVOCATION_DOMAIN = "fedelex-revocation-v1";
// Reserved (D90, FASE 11 inerte): the bot never emits this today. Field
// shape below is this client's best-effort forward compatibility guess,
// not yet a contract negotiated with the bot side — see SuspensionFields.
inline constexpr const char* SUSPENSION_DOMAIN = "fedelex-suspension-v1";

enum class MessageType {
	Title,
	Revocation,
	Suspension, // reserved — see SUSPENSION_DOMAIN above
	Unknown,    // "type" missing or not one of the three above: never applied (§10)
};

struct TitleFields {
	std::string user_ref;
	std::string issued_at;
	std::string expires_at;
};

struct RevocationFields {
	std::string user_ref;
	std::string revoked_at;
	// Canonical form already folds JSON null to "" (matches the bot's own
	// `banned_until ?? ""` in canonicalRevocation) — that folding has to
	// happen before the signature can verify, so it is not a separate step
	// the caller can get wrong. has_banned_until is for display only (§8):
	// true iff the wire value was a JSON string, however that string reads.
	std::string banned_until;
	bool has_banned_until = false;
};

struct SuspensionFields {
	// Mirrors RevocationFields' "when it took effect" field on purpose: §10
	// requires suspension to supersede the same way a revocation does (a
	// title with a later issued_at wins), and title_store.h implements one
	// supersede rule shared by both. Whatever real fields the eventual bot
	// payload adds beyond this are ignored, not rejected, same tolerance
	// banlist_verify.cpp already gives unknown top-level JSON keys.
	std::string user_ref;
	std::string suspended_at;
};

enum class VerifyStatus {
	Ok,
	BadSignature,    // no trusted key validates these bytes under this domain, or the signature is not 64 raw bytes
	MalformedJson,   // not parseable as JSON at all
	SchemaViolation, // parseable, but missing/mistyped a field this type requires
};

// Reads {"type": "..."} to decide which of the three parsers below applies —
// nothing else here is trusted from this call. An attacker who relabels a
// real revocation body as "type":"title" gains nothing: the wrong parser
// either fails on missing fields (SchemaViolation) or reconstructs bytes
// that were never signed under the title domain (BadSignature) — this
// function only selects a codepath, it never authenticates one.
MessageType PeekType(const std::string& document);

// Each parses the type-specific required fields out of `document`, rebuilds
// the exact canonical string the bot signs (domain + "\n" + fields joined by
// "\n"), and verifies the response's own "signature" field (base64) against
// it under every key in title::TRUSTED_KEYS. `error` carries a one-line
// reason for the log on failure; `out` is untouched unless VerifyStatus::Ok.
VerifyStatus VerifyAndParseTitle(const std::string& document, TitleFields& out, std::string& error);
VerifyStatus VerifyAndParseRevocation(const std::string& document, RevocationFields& out, std::string& error);
VerifyStatus VerifyAndParseSuspension(const std::string& document, SuspensionFields& out, std::string& error);

// Same three, against an explicit key set instead of title::TRUSTED_KEYS —
// what the three above actually call. Exists so tests can sign with a
// throwaway keypair (D25 — the real private keys can never exist in a test
// binary) and still exercise the REAL canonical-reconstruction code in this
// module, rather than a hand-rolled copy of it living in the test file that
// could silently drift from what VerifyAndParseTitle et al. actually do.
// The shipping client only ever calls the three hardwired overloads above.
VerifyStatus VerifyAndParseTitleWith(const std::string& document, TitleFields& out, std::string& error,
									 const uint8_t* const* keys, size_t key_count);
VerifyStatus VerifyAndParseRevocationWith(const std::string& document, RevocationFields& out, std::string& error,
										  const uint8_t* const* keys, size_t key_count);
VerifyStatus VerifyAndParseSuspensionWith(const std::string& document, SuspensionFields& out, std::string& error,
										  const uint8_t* const* keys, size_t key_count);

// Verifies an arbitrary domain-separated (canonical, signature) pair against
// title::TRUSTED_KEYS. What VerifyAndParse* actually calls; exposed on its
// own because title_store.h's supersede rule needs to re-verify a cached
// message read back from disk without re-deriving its canonical form.
bool VerifyWithDomain(const std::string& domain, const std::string& canonical,
					 const std::string& signature_base64);

// Same, against an explicit key set — lets tests sign with a throwaway
// keypair instead of needing the real private key, which they could never
// have (D25). The shipping client always goes through the overload above,
// hardwired to title::TRUSTED_KEYS.
bool VerifyWithDomainUsing(const std::string& domain, const std::string& canonical,
						  const std::string& signature_base64,
						  const uint8_t* const* keys, size_t key_count);

}

#endif //TITLE_VERIFY_H
