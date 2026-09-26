// Tests for the signed client-update manifest verification module
// (design/client-update.md). No network, no window, no game — same
// standalone-binary discipline as banlist_tests.cpp and title_tests.cpp.
//
// Cross-domain coverage here is not a nice-to-have: design/client-update.md
// requires it explicitly ("il test di dominio incrociato su entrambi i
// lati"), because update_verify.h's domain label is the ONLY thing that
// stops a document signed for a different purpose under the same custodian
// from being misread as an update manifest.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "banlist_verify.h"
#include "title_verify.h"
#include "update_keys.h"
#include "update_verify.h"
#include "client_update_version.h"
// See the matching comment in banlist_verify.cpp: tweetnacl.h has no
// extern "C" guard of its own.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

// Called from banlist_tests.cpp's main() so the whole suite stays one binary
// with one summary line, per tests/premake5.lua's single ConsoleApp target.
int RunUpdateTests();

using namespace ygo::update;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
	++checks;
	if(!condition) {
		++failures;
		std::printf("  FAIL  %s\n", what);
	}
}

// Same fixture files banlist_tests.cpp and title_tests.cpp already read —
// one throwaway Ed25519 keypair, committed once, used everywhere a test
// needs a signature the real keys (which do not exist yet — update_keys.h)
// could never produce.
std::string ReadFixture(const char* name) {
	const std::string path = std::string("tests/fixtures/") + name;
	std::ifstream f(path, std::ios::binary);
	if(!f) {
		std::printf("  FIXTURE MISSING: %s (run the binary from the repository root)\n", path.c_str());
		return {};
	}
	return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

struct TestKey {
	unsigned char pub[32]{};
	unsigned char sec[64]{};
	bool loaded = false;
};

TestKey LoadTestKey() {
	TestKey key;
	const auto pub_bytes = ReadFixture("test_key.pub");
	const auto sec_bytes = ReadFixture("test_key.sec");
	if(pub_bytes.size() != sizeof(key.pub) || sec_bytes.size() != sizeof(key.sec))
		return key;
	std::memcpy(key.pub, pub_bytes.data(), sizeof(key.pub));
	std::memcpy(key.sec, sec_bytes.data(), sizeof(key.sec));
	key.loaded = true;
	return key;
}

// Raw crypto_sign detached-signature helper, identical in shape to the one
// in banlist_tests.cpp/title_tests.cpp: signs exactly `message` (whatever
// the caller already built, domain prefix included or not) and returns the
// 64 raw signature bytes.
std::string SignDetachedRaw(const std::string& message, const unsigned char sk[64]) {
	std::vector<unsigned char> sm(message.size() + 64);
	unsigned long long smlen = 0;
	crypto_sign(sm.data(), &smlen, reinterpret_cast<const unsigned char*>(message.data()), message.size(), sk);
	return std::string(reinterpret_cast<char*>(sm.data()), 64);
}

// Signs `document` the way this module actually signs a manifest: UPDATE_DOMAIN + "\n" + document.
std::string SignAsUpdateManifest(const std::string& document, const unsigned char sk[64]) {
	std::string message = std::string(UPDATE_DOMAIN) + "\n" + document;
	return SignDetachedRaw(message, sk);
}

std::string MinimalManifest(int version) {
	return "{"
		"\"version\":" + std::to_string(version) + ","
		"\"files\":["
		"{\"name\":\"ocgcore.so\",\"url\":\"https://example.invalid/ocgcore.so\","
		"\"sha256\":\"" + std::string(64, 'a') + "\"}"
		"]}";
}

// ---------------------------------------------------------------------------

void test_valid_manifest_signature_verifies() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "valid_manifest_signature_verifies: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, key.sec);
	const uint8_t* keys[] = { key.pub };
	const bool verified = VerifySignatureWith(document, signature, keys, 1);
	check(verified, "a manifest signed with UPDATE_DOMAIN under a trusted key must verify");

	Manifest out;
	std::string error;
	// Parse() alone, since VerifyAndParse() is hardwired to the (unfilled)
	// production TRUSTED_KEYS and would always fail today — Parse() is what
	// this case is actually about.
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a well-formed manifest must parse once its signature is trusted");
	check(out.version == 3, "version must round-trip");
	check(out.files.size() == 1 && out.files[0].sha256 == std::string(64, 'a'),
		 "files[].sha256 must round-trip verbatim");
}

void test_one_flipped_byte_is_rejected() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "one_flipped_byte_is_rejected: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, key.sec);
	std::string tampered = document;
	tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);
	const uint8_t* keys[] = { key.pub };
	check(!VerifySignatureWith(tampered, signature, keys, 1),
		 "one flipped byte in the manifest must invalidate the signature");
}

void test_signature_from_an_untrusted_key_is_rejected() {
	unsigned char untrusted_pub[32]{}, untrusted_sec[64]{};
	crypto_sign_keypair(untrusted_pub, untrusted_sec);
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "signature_from_an_untrusted_key_is_rejected: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, untrusted_sec);
	// Checked against `key.pub`, a DIFFERENT key than the one that signed.
	const uint8_t* keys[] = { key.pub };
	check(!VerifySignatureWith(document, signature, keys, 1),
		 "a signature from a key not in the trusted set must be rejected");
}

void test_signature_from_the_reserve_key_is_accepted() {
	unsigned char operational_pub[32]{}, operational_sec[64]{};
	unsigned char reserve_pub[32]{}, reserve_sec[64]{};
	crypto_sign_keypair(operational_pub, operational_sec);
	crypto_sign_keypair(reserve_pub, reserve_sec);

	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, reserve_sec);

	const uint8_t* keys[] = { operational_pub, reserve_pub };
	check(VerifySignatureWith(document, signature, keys, 2),
		 "a signature from the SECOND trusted key must be accepted just like the first");
}

void test_all_zero_placeholder_key_never_verifies() {
	// update_keys.h ships both TRUSTED_KEYS entries all-zero today (no
	// production key generated yet). Signing with a REAL keypair and
	// checking against the all-zero placeholder must still fail — an
	// all-zero key is not a wildcard.
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "all_zero_placeholder_key_never_verifies: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, key.sec);
	unsigned char zero_key[32]{};
	const uint8_t* keys[] = { zero_key };
	check(!VerifySignatureWith(document, signature, keys, 1),
		 "an all-zero placeholder key must never validate a signature");
}

void test_any_trusted_key_configured_is_true_since_2026_09_26() {
	// FASE 36 (design/client-update.md, "Fail-closed"): both update_keys.h
	// entries were pasted in on 2026-09-26 (TRUSTED_KEY_OPERATIONAL,
	// TRUSTED_KEY_RESERVE — see update_keys.h), so AnyTrustedKeyConfigured()
	// must now report true. This assertion replaces the placeholder-era one
	// that checked the opposite (`test_any_trusted_key_configured_is_false_today`,
	// true only until the keys existed) — it is not a spurious break, it is
	// the signal that update_keys.h changed for the reason everyone expected.
	check(AnyTrustedKeyConfigured(),
		 "with real update_keys.h entries pasted in since 2026-09-26, AnyTrustedKeyConfigured() must be true");
}

void test_no_trusted_key_entry_is_all_zero() {
	// The most likely copy-paste mistake: one of the two TRUSTED_KEYS slots
	// left at the all-zero placeholder while the other got the real key.
	// AnyTrustedKeyConfigured() alone would not catch this (it only needs
	// ONE non-zero entry to return true) — this checks EVERY entry.
	unsigned char zero[32]{};
	for(const uint8_t* key : TRUSTED_KEYS) {
		check(std::memcmp(key, zero, sizeof(zero)) != 0,
			 "no entry of TRUSTED_KEYS may be the all-zero placeholder");
	}
}

void test_operational_and_reserve_keys_are_distinct() {
	// A pair where the reserve is a duplicate of the operational key looks
	// healthy (both non-zero, AnyTrustedKeyConfigured() true) and protects
	// nothing: a rotation that promotes "the reserve" would republish the
	// same key that might be the one being retired. update_keys.h's own
	// comment says both keys were "verified distinct" by hand — this makes
	// that verification a standing test instead of a one-time claim.
	check(std::memcmp(TRUSTED_KEY_OPERATIONAL, TRUSTED_KEY_RESERVE, sizeof(TRUSTED_KEY_OPERATIONAL)) != 0,
		 "TRUSTED_KEY_OPERATIONAL and TRUSTED_KEY_RESERVE must be different keys");
}

void test_equal_version_is_no_action() {
	check(CompareVersion(5, 5) == VersionDecision::AlreadyCurrent,
		 "equal version must compare as AlreadyCurrent");
}

void test_lower_version_is_rollback() {
	check(CompareVersion(3, 5) == VersionDecision::Rollback,
		 "a lower version than installed must compare as Rollback");
}

void test_higher_version_is_accept() {
	check(CompareVersion(7, 5) == VersionDecision::Accept,
		 "a higher version than installed must compare as Accept");
}

void test_missing_sha256_is_schema_violation() {
	const std::string document =
		"{\"version\":1,\"files\":[{\"name\":\"a\",\"url\":\"https://example.invalid/a\"}]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "a files entry missing sha256 must be a schema violation");
}

void test_malformed_sha256_is_schema_violation() {
	const std::string document =
		"{\"version\":1,\"files\":[{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"not-hex\"}]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "a non-hex sha256 must be a schema violation");
}

void test_duplicate_file_name_is_schema_violation() {
	const std::string hash(64, 'b');
	const std::string document =
		"{\"version\":1,\"files\":["
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + hash + "\"},"
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a2\",\"sha256\":\"" + hash + "\"}"
		"]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "a duplicate files[].name must be a schema violation");
}

void test_manifest_without_min_supported_is_valid() {
	// design/client-update.md §9: a manifest that never declares the field
	// is valid and closes nothing.
	const auto document = MinimalManifest(3);
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a manifest without min_supported must still parse as Ok");
	check(out.min_supported == 0, "min_supported must default to 0 (absent) when the manifest omits it");
}

void test_min_supported_above_version_is_schema_violation() {
	// A manifest that requires more than it itself publishes is a
	// contradiction, not a floor to enforce.
	const std::string document =
		"{\"version\":3,\"min_supported\":4,\"files\":["
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'c') + "\"}"
		"]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "min_supported > version must be a schema violation");
}

void test_valid_min_supported_round_trips() {
	const std::string document =
		"{\"version\":5,\"min_supported\":2,\"files\":["
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'd') + "\"}"
		"]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a valid min_supported <= version must parse as Ok");
	check(out.min_supported == 2, "min_supported must round-trip verbatim");
}

// --- IsClientSupported: pure, no network, no window, no globals ---

void test_is_client_supported_true_when_above_min_supported() {
	check(IsClientSupported(/*min_supported=*/2, /*client_version=*/CLIENT_UPDATE_VERSION),
		 "a client at or above min_supported must be supported");
}

void test_is_client_supported_false_when_below_min_supported() {
	check(!IsClientSupported(/*min_supported=*/CLIENT_UPDATE_VERSION + 1, /*client_version=*/CLIENT_UPDATE_VERSION),
		 "a client strictly below min_supported must NOT be supported");
}

void test_is_client_supported_true_when_no_floor_declared() {
	check(IsClientSupported(/*min_supported=*/0, /*client_version=*/1),
		 "min_supported == 0 (absent) must never close anything, however low client_version is");
}

void test_json_is_never_parsed_before_the_signature_verifies() {
	// Same shape as the equivalent banlist test: bytes that are BOTH badly
	// signed AND not valid JSON must come back BadSignature, never
	// MalformedJson — MalformedJson here would mean the parser ran on bytes
	// nobody had authenticated.
	const std::string not_json = "this is not json at all {{{";
	const std::string bogus_signature(ED25519_SIGNATURE_SIZE, '\0');
	Manifest out;
	std::string error;
	const auto status = VerifyAndParse(not_json, bogus_signature, out, error);
	check(status == VerifyStatus::BadSignature,
		 "signature-then-parse must reject before ever attempting to parse");
}

// --- FASE 37, cancello 1/2: a manifest actually signed by the Python side ---
//
// Every case above signs IN THIS BINARY, with SignAsUpdateManifest/
// SignDetachedRaw reimplementing "what the domain prefix looks like" in
// C++. That is enough to test update_verify.cpp against itself, but it can
// never catch a divergence between this file's UPDATE_DOMAIN and the
// literal string banlist/scripts/sign_update_manifest.py concatenates in
// the vault repo — two copies of "fedelex-update-v1" agreeing is exactly
// the thing a same-source reimplementation cannot verify. These two cases
// read fixtures signed by actually RUNNING that script (see
// tests/fixtures/README.md for the exact commands), so the only way they
// pass is if the Python message-construction and this module's agree
// byte-for-byte.

void test_python_signed_manifest_verifies_in_cpp() {
	const auto document = ReadFixture("update_manifest.json");
	const auto signature = ReadFixture("update_manifest.json.sig");
	if(document.empty() || signature.empty()) {
		check(false, "python_signed_manifest_verifies_in_cpp: fixtures missing, cannot run");
		return;
	}
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "python_signed_manifest_verifies_in_cpp: test key fixture missing");
		return;
	}
	const uint8_t* keys[] = { key.pub };
	check(VerifySignatureWith(document, signature, keys, 1),
		 "a manifest signed by sign_update_manifest.py (vault) must verify against the same "
		 "test key in this C++ module — this is the cross-language cancello 1, not a string compare");

	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "the Python-signed fixture must also be a well-formed manifest");
	check(out.version == 5, "version must round-trip from the Python-built fixture");
	check(out.min_supported == 3, "min_supported must round-trip from the Python-built fixture");
	check(out.files.size() == 2, "both files[] entries must round-trip from the Python-built fixture");
}

void test_python_banlist_style_signature_does_not_verify_as_update() {
	// Cancello 2: the SAME document, signed by sign_banlist.py (raw bytes,
	// no domain prefix) instead of sign_update_manifest.py, with the SAME
	// test key — must NOT verify as an update manifest. If this ever
	// passed, UPDATE_DOMAIN would not be doing anything on the Python side.
	const auto document = ReadFixture("update_manifest.json");
	const auto signature = ReadFixture("update_manifest.banlist_style.sig");
	if(document.empty() || signature.empty()) {
		check(false, "python_banlist_style_signature_does_not_verify_as_update: fixtures missing, cannot run");
		return;
	}
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "python_banlist_style_signature_does_not_verify_as_update: test key fixture missing");
		return;
	}
	const uint8_t* keys[] = { key.pub };
	check(!VerifySignatureWith(document, signature, keys, 1),
		 "a signature produced by sign_banlist.py (no domain) must NOT verify as an update manifest, "
		 "even for the exact same document and key sign_update_manifest.py used");
}

// --- cross-domain: the requirement design/client-update.md calls out explicitly ---

void test_update_signature_does_not_verify_as_banlist() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "update_signature_does_not_verify_as_banlist: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, key.sec);
	// banlist::VerifySignatureWith checks the raw document bytes with no
	// domain prefix at all — the bytes it hashes are simply different from
	// UPDATE_DOMAIN + "\n" + document, so this must fail.
	const uint8_t* keys[] = { key.pub };
	check(!ygo::banlist::VerifySignatureWith(document, signature, keys, 1),
		 "a manifest signed under fedelex-update-v1 must NOT verify as a banlist artifact");
}

void test_banlist_style_signature_does_not_verify_as_update() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "banlist_style_signature_does_not_verify_as_update: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	// Sign the raw document with no domain prefix at all — exactly how
	// banlist_verify.cpp signs an artifact.
	const auto signature = SignDetachedRaw(document, key.sec);
	const uint8_t* keys[] = { key.pub };
	check(!VerifySignatureWith(document, signature, keys, 1),
		 "a document signed banlist-style (no domain) must NOT verify as an update manifest");
}

void test_title_domain_signature_does_not_verify_as_update() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "title_domain_signature_does_not_verify_as_update: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	// Sign as the title module would: TITLE_DOMAIN + "\n" + document,
	// instead of UPDATE_DOMAIN + "\n" + document.
	const std::string message = std::string(ygo::title::TITLE_DOMAIN) + "\n" + document;
	const auto signature = SignDetachedRaw(message, key.sec);
	const uint8_t* keys[] = { key.pub };
	check(!VerifySignatureWith(document, signature, keys, 1),
		 "a document signed under fedelex-title-v1 must NOT verify as an update manifest");
}

void test_update_domain_signature_does_not_verify_under_title_domain() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "update_domain_signature_does_not_verify_under_title_domain: test key fixture missing");
		return;
	}
	const auto document = MinimalManifest(3);
	const auto signature = SignAsUpdateManifest(document, key.sec);
	const uint8_t* keys[] = { key.pub };
	// title::VerifyWithDomainUsing takes the base64 form of the signature
	// (it verifies title/revocation responses, whose wire format carries a
	// base64 "signature" field) — reject on shape alone is still a valid
	// negative here, since an update-domain signature is never valid base64
	// input for that call in the first place, but the real guarantee is the
	// byte-level one below.
	check(!ygo::title::VerifyWithDomainUsing(ygo::title::TITLE_DOMAIN, document,
											 std::string(reinterpret_cast<const char*>(signature.data()), signature.size()),
											 keys, 1),
		 "a signature made under fedelex-update-v1 must NOT verify under fedelex-title-v1, even reusing the same document and key");
	const std::string update_message = std::string(UPDATE_DOMAIN) + "\n" + document;
	const std::string title_message = std::string(ygo::title::TITLE_DOMAIN) + "\n" + document;
	check(update_message != title_message,
		 "UPDATE_DOMAIN and TITLE_DOMAIN must never produce the same signed bytes for the same document");
}

}

int RunUpdateTests() {
	test_valid_manifest_signature_verifies();
	test_one_flipped_byte_is_rejected();
	test_signature_from_an_untrusted_key_is_rejected();
	test_signature_from_the_reserve_key_is_accepted();
	test_all_zero_placeholder_key_never_verifies();
	test_any_trusted_key_configured_is_true_since_2026_09_26();
	test_no_trusted_key_entry_is_all_zero();
	test_operational_and_reserve_keys_are_distinct();
	test_equal_version_is_no_action();
	test_lower_version_is_rollback();
	test_higher_version_is_accept();
	test_missing_sha256_is_schema_violation();
	test_malformed_sha256_is_schema_violation();
	test_duplicate_file_name_is_schema_violation();
	test_manifest_without_min_supported_is_valid();
	test_min_supported_above_version_is_schema_violation();
	test_valid_min_supported_round_trips();
	test_is_client_supported_true_when_above_min_supported();
	test_is_client_supported_false_when_below_min_supported();
	test_is_client_supported_true_when_no_floor_declared();
	test_json_is_never_parsed_before_the_signature_verifies();
	test_python_signed_manifest_verifies_in_cpp();
	test_python_banlist_style_signature_does_not_verify_as_update();
	test_update_signature_does_not_verify_as_banlist();
	test_banlist_style_signature_does_not_verify_as_update();
	test_title_domain_signature_does_not_verify_as_update();
	test_update_domain_signature_does_not_verify_under_title_domain();

	std::printf("update_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
