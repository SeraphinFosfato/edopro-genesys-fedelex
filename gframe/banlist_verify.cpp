#include "banlist_verify.h"

#include "banlist_keys.h"
// #include "tweetnacl/tweetnacl.h"  // vendored in FASE 4b
#include <nlohmann/json.hpp>

namespace ygo::banlist {

// SKELETON — FASE 4b. Contract and signatures are fixed (banlist_verify.h);
// the bodies are left to the implementation pass.

bool VerifySignatureWith(const std::string& document, const std::string& signature,
						 const uint8_t* const* keys, size_t key_count) {
	// 1. reject unless signature.size() == ED25519_SIGNATURE_SIZE
	// 2. for each key: verify the detached signature over the exact bytes of
	//    `document`. Accept on the first key that verifies — accepting ANY
	//    trusted key, not only the first, is what makes rotation possible
	//    without a new client build (§2b).
	// 3. nothing in the log says which key matched.
	(void)document; (void)signature; (void)keys; (void)key_count;
	return false;
}

bool VerifySignature(const std::string& document, const std::string& signature) {
	return VerifySignatureWith(document, signature, TRUSTED_KEYS,
							   sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus Parse(const std::string& document, Payload& out, std::string& error) {
	// nlohmann::json::parse with exceptions disabled (accept_discarded /
	// parse(..., nullptr, false)) — a throw out of here would reach a worker
	// thread with nothing to catch it.
	//
	// Schema checks, all of them mandatory (the artifact contract):
	//  - format_version is an INTEGER. Not a string, not a float, not semver:
	//    D62 made it a plain monotonic integer and D58's MAJOR comparison no
	//    longer exists. A non-integer is a malformed payload → refuse, exactly
	//    like a bad signature.
	//  - entries is an array; every entry has id (unsigned), limit (0..3),
	//    points (int), name, macro, source. No field is optional and none has
	//    a client-side default — the artifact always emits them (the artifact contract rule 1).
	//  - reason is ABSENT when there is none. Not null, not "". Absent →
	//    has_reason = false. A present-but-empty reason is a schema violation,
	//    not an empty motivation.
	//  - macro must be one of the ten values of D60. An unknown value is a
	//    schema violation: the enumeration is closed on the producing side, so
	//    a new value here means the artifact and this client disagree about
	//    what the format contains, and guessing is how that becomes silent.
	//  - duplicate id → schema violation.
	(void)document; (void)out; (void)error;
	return VerifyStatus::SchemaViolation;
}

VerifyStatus VerifyAndParse(const std::string& document, const std::string& signature,
							Payload& out, std::string& error) {
	if(!VerifySignature(document, signature)) {
		error = "signature does not verify against any trusted key";
		return VerifyStatus::BadSignature;
	}
	return Parse(document, out, error);
}

VersionDecision CompareVersion(int incoming_version, int active_version) {
	if(incoming_version > active_version)
		return VersionDecision::Accept;
	if(incoming_version == active_version)
		return VersionDecision::AlreadyCurrent;
	return VersionDecision::Rollback;
}

}
