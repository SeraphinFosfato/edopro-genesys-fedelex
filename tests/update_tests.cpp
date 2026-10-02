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

// --- D238 (design/client-update.md §6quinquies): the installed version IS
// CLIENT_UPDATE_VERSION, never a file. ClientUpdater::GetInstalledVersion()
// now just returns that constant (client_updater.cpp), so the whole
// behaviour table this cancello asks for ("un manifesto con version uguale
// a CLIENT_UPDATE_VERSION risponde gia' aggiornato, uno con versione
// maggiore aggiorna, uno minore e' rifiutato, senza nessun file su disco")
// reduces to exercising CompareVersion() against that constant directly —
// client_updater.cpp itself cannot link into this standalone binary (it
// pulls in curl, MD5, irrlicht's file utilities under #if
// defined(UPDATE_URL)), but the decision it now delegates to is exactly
// this pure function, and no FileStream is ever opened to answer it.

void test_manifest_equal_to_client_update_version_is_already_current() {
	check(CompareVersion(CLIENT_UPDATE_VERSION, CLIENT_UPDATE_VERSION) == VersionDecision::AlreadyCurrent,
		 "a manifest version equal to CLIENT_UPDATE_VERSION (the installed version, post-D238) must be AlreadyCurrent");
}

void test_manifest_above_client_update_version_is_accept() {
	check(CompareVersion(CLIENT_UPDATE_VERSION + 1, CLIENT_UPDATE_VERSION) == VersionDecision::Accept,
		 "a manifest version above CLIENT_UPDATE_VERSION must be Accept");
}

void test_manifest_below_client_update_version_is_rollback() {
	check(CompareVersion(CLIENT_UPDATE_VERSION - 1, CLIENT_UPDATE_VERSION) == VersionDecision::Rollback,
		 "a manifest version below CLIENT_UPDATE_VERSION must be Rollback");
}

// Structural half of the same cancello: the file-based record (and the
// function that used to write it) must actually be gone from the source,
// not just unused from this test's point of view — same style as
// game_data_ready_tests.cpp's ReadSourceFile scans.
void test_no_installed_version_file_remains_in_source() {
	std::ifstream f("gframe/client_updater.cpp", std::ios::binary);
	if(!f) {
		check(false, "test_no_installed_version_file_remains_in_source: gframe/client_updater.cpp unreadable (run from the repository root)");
		return;
	}
	const std::string source((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	check(source.find(".edopro_update_version") == std::string::npos,
		 "D238: no on-disk .edopro_update_version file may remain — GetInstalledVersion() must return CLIENT_UPDATE_VERSION directly");
	check(source.find("SetInstalledVersion") == std::string::npos,
		 "D238: SetInstalledVersion must be gone entirely, not just unused");
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

// --- D243 (design/client-update.md §6quater): os parsing and the platform filter ---

void test_manifest_without_os_is_valid_and_os_defaults_empty() {
	// Older manifests, or a files[] entry a publisher simply forgot to tag —
	// neither is a schema violation. SelectFilesForPlatform() is what turns
	// an absent os into "never installed", not Parse().
	const auto document = MinimalManifest(3);
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a manifest without os must still parse as Ok");
	check(out.files.size() == 1 && out.files[0].os.empty(),
		 "files[].os must default to empty when the manifest omits it");
}

void test_os_round_trips_verbatim() {
	const std::string document =
		"{\"version\":1,\"files\":["
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'e') + "\",\"os\":\"linux\"}"
		"]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a manifest with a string os must parse as Ok");
	check(out.files.size() == 1 && out.files[0].os == "linux", "files[].os must round-trip verbatim");
}

void test_non_string_os_is_schema_violation() {
	const std::string document =
		"{\"version\":1,\"files\":["
		"{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'f') + "\",\"os\":42}"
		"]}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "a non-string files[].os must be a schema violation");
}

void test_select_files_for_platform_keeps_only_matching_os() {
	std::vector<ManifestFile> files;
	files.push_back(ManifestFile{"linux.zip", "https://example.invalid/l", std::string(64, 'a'), "", "linux"});
	files.push_back(ManifestFile{"windows.zip", "https://example.invalid/w", std::string(64, 'b'), "", "windows"});
	size_t discarded = 0;
	const auto selected = SelectFilesForPlatform(files, PlatformLinux, &discarded);
	check(selected.size() == 1 && selected[0].name == "linux.zip",
		 "linux must keep only the linux-tagged entry");
	check(discarded == 1, "exactly one entry (windows) must be counted as discarded");
}

void test_select_files_for_platform_discards_missing_os() {
	std::vector<ManifestFile> files;
	files.push_back(ManifestFile{"mystery.zip", "https://example.invalid/m", std::string(64, 'a'), "", ""});
	size_t discarded = 0;
	const auto selected = SelectFilesForPlatform(files, PlatformLinux, &discarded);
	check(selected.empty(), "an entry with empty os must never be installed, even on linux");
	check(discarded == 1, "an entry with empty os must be counted as discarded");
}

void test_select_files_for_platform_discards_unrecognized_os() {
	std::vector<ManifestFile> files;
	files.push_back(ManifestFile{"mac.zip", "https://example.invalid/m", std::string(64, 'a'), "", "macos"});
	size_t discarded = 0;
	const auto selected = SelectFilesForPlatform(files, PlatformLinux, &discarded);
	check(selected.empty(), "an unrecognized os (e.g. macos) must be discarded, not installed as a fallback");
	check(discarded == 1, "an unrecognized os must be counted as discarded");
}

void test_select_files_for_platform_empty_result_when_nothing_matches() {
	std::vector<ManifestFile> files;
	files.push_back(ManifestFile{"windows.zip", "https://example.invalid/w", std::string(64, 'b'), "", "windows"});
	const auto selected = SelectFilesForPlatform(files, PlatformLinux, nullptr);
	check(selected.empty(), "a linux client selecting from a windows-only manifest must get an empty list, not a fallback");
}

void test_select_files_for_platform_discarded_count_is_optional() {
	// The nullptr form must not crash — callers that only care about the
	// filtered list (none today, but the signature promises it) are allowed
	// to skip the count.
	std::vector<ManifestFile> files;
	files.push_back(ManifestFile{"linux.zip", "https://example.invalid/l", std::string(64, 'a'), "", "linux"});
	const auto selected = SelectFilesForPlatform(files, PlatformLinux, nullptr);
	check(selected.size() == 1, "passing nullptr for discarded_count must still filter correctly");
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

// --- D244: launcher_files parsing and selection -----------------------

std::string ManifestWithLauncherFiles(const std::string& launcher_files_json) {
	return "{"
		"\"version\":1,"
		"\"files\":["
		"{\"name\":\"ocgcore.so\",\"url\":\"https://example.invalid/ocgcore.so\","
		"\"sha256\":\"" + std::string(64, 'a') + "\"}"
		"],"
		"\"launcher_files\":[" + launcher_files_json + "]"
		"}";
}

void test_manifest_without_launcher_files_is_valid_and_empty() {
	const auto document = MinimalManifest(1);
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a manifest without launcher_files must still parse as Ok (older manifests, or nothing new for the launcher)");
	check(out.launcher_files.empty(), "launcher_files must default to empty when the manifest omits it");
}

void test_launcher_files_round_trip_simulator_role() {
	const auto document = ManifestWithLauncherFiles(
		"{\"name\":\"ygoprodll\",\"url\":\"https://example.invalid/ygoprodll\","
		"\"sha256\":\"" + std::string(64, 'b') + "\",\"os\":\"linux\",\"role\":\"simulator\"}");
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "a well-formed launcher_files entry must parse as Ok");
	check(out.launcher_files.size() == 1, "exactly one launcher_files entry must round-trip");
	if(out.launcher_files.size() == 1) {
		check(out.launcher_files[0].name == "ygoprodll", "launcher_files[].name must round-trip verbatim");
		check(out.launcher_files[0].os == "linux", "launcher_files[].os must round-trip verbatim");
		check(out.launcher_files[0].role == LauncherFileRole::Simulator, "role \"simulator\" must parse to LauncherFileRole::Simulator");
	}
}

void test_launcher_files_strings_role_round_trips() {
	check(LauncherFileRoleFromString("strings") == LauncherFileRole::Strings,
		 "role \"strings\" must parse to LauncherFileRole::Strings");
}

void test_launcher_files_unknown_role_is_not_schema_violation() {
	// D244 point 3: "un valore sconosciuto si scarta e si dice" — scarta,
	// not "rifiuta il manifesto". An unrecognized role must still parse
	// (the entry just carries LauncherFileRole::Unknown), so one bad entry
	// from a future role never breaks every client on an old build.
	const auto document = ManifestWithLauncherFiles(
		"{\"name\":\"mystery\",\"url\":\"https://example.invalid/mystery\","
		"\"sha256\":\"" + std::string(64, 'c') + "\",\"os\":\"linux\",\"role\":\"something-new\"}");
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::Ok, "an unrecognized role must not be a schema violation");
	check(out.launcher_files.size() == 1 && out.launcher_files[0].role == LauncherFileRole::Unknown,
		 "an unrecognized role must parse with LauncherFileRole::Unknown, raw string preserved");
	check(out.launcher_files.size() == 1 && out.launcher_files[0].role_raw == "something-new",
		 "role_raw must preserve the unrecognized wire value verbatim, for logging");
}

void test_launcher_files_missing_os_is_schema_violation() {
	// Unlike files[], launcher_files[] requires os from birth (D244, no
	// legacy manifest predates it) — see the .cpp comment.
	const auto document = ManifestWithLauncherFiles(
		"{\"name\":\"ygoprodll\",\"url\":\"https://example.invalid/ygoprodll\","
		"\"sha256\":\"" + std::string(64, 'd') + "\",\"role\":\"simulator\"}");
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "launcher_files[] missing os must be a schema violation");
}

void test_launcher_files_missing_role_is_schema_violation() {
	const auto document = ManifestWithLauncherFiles(
		"{\"name\":\"ygoprodll\",\"url\":\"https://example.invalid/ygoprodll\","
		"\"sha256\":\"" + std::string(64, 'e') + "\",\"os\":\"linux\"}");
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "launcher_files[] missing role must be a schema violation");
}

void test_launcher_files_duplicate_name_is_schema_violation() {
	const auto document = ManifestWithLauncherFiles(
		"{\"name\":\"dup\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'f') + "\",\"os\":\"linux\",\"role\":\"simulator\"},"
		"{\"name\":\"dup\",\"url\":\"https://example.invalid/b\",\"sha256\":\"" + std::string(64, 'f') + "\",\"os\":\"windows\",\"role\":\"simulator\"}");
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "a duplicate launcher_files[].name must be a schema violation");
}

void test_launcher_files_not_array_is_schema_violation() {
	const std::string document = "{\"version\":1,\"files\":[{\"name\":\"a\",\"url\":\"https://example.invalid/a\",\"sha256\":\"" + std::string(64, 'a') + "\"}],\"launcher_files\":\"nope\"}";
	Manifest out;
	std::string error;
	const auto status = Parse(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "launcher_files not being an array must be a schema violation");
}

void test_select_launcher_files_keeps_matching_os_and_known_role() {
	std::vector<LauncherFile> files;
	LauncherFile linux_sim; linux_sim.name = "ygoprodll"; linux_sim.os = "linux"; linux_sim.role = LauncherFileRole::Simulator; linux_sim.role_raw = "simulator";
	LauncherFile windows_sim; windows_sim.name = "ygoprodll.exe"; windows_sim.os = "windows"; windows_sim.role = LauncherFileRole::Simulator; windows_sim.role_raw = "simulator";
	LauncherFile linux_strings; linux_strings.name = "fedelex.conf"; linux_strings.os = "linux"; linux_strings.role = LauncherFileRole::Strings; linux_strings.role_raw = "strings";
	files = { linux_sim, windows_sim, linux_strings };
	size_t discarded = 0;
	const auto selected = SelectLauncherFilesForPlatform(files, "linux", &discarded);
	check(selected.size() == 2, "exactly the two linux entries must be kept");
	check(discarded == 1, "the windows entry must be counted as discarded");
}

void test_select_launcher_files_discards_unknown_role_even_with_matching_os() {
	LauncherFile mystery; mystery.name = "mystery"; mystery.os = "linux"; mystery.role = LauncherFileRole::Unknown; mystery.role_raw = "something-new";
	size_t discarded = 0;
	const auto selected = SelectLauncherFilesForPlatform({ mystery }, "linux", &discarded);
	check(selected.empty(), "an entry with the right os but an unrecognized role must never be installed");
	check(discarded == 1, "an unrecognized-role entry must be counted as discarded");
}

void test_select_launcher_files_empty_result_when_nothing_matches() {
	LauncherFile windows_sim; windows_sim.name = "ygoprodll.exe"; windows_sim.os = "windows"; windows_sim.role = LauncherFileRole::Simulator; windows_sim.role_raw = "simulator";
	const auto selected = SelectLauncherFilesForPlatform({ windows_sim }, "linux");
	check(selected.empty(), "no entry for the requesting platform must yield an empty vector, not an error");
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
	test_manifest_equal_to_client_update_version_is_already_current();
	test_manifest_above_client_update_version_is_accept();
	test_manifest_below_client_update_version_is_rollback();
	test_no_installed_version_file_remains_in_source();
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
	test_manifest_without_os_is_valid_and_os_defaults_empty();
	test_os_round_trips_verbatim();
	test_non_string_os_is_schema_violation();
	test_select_files_for_platform_keeps_only_matching_os();
	test_select_files_for_platform_discards_missing_os();
	test_select_files_for_platform_discards_unrecognized_os();
	test_select_files_for_platform_empty_result_when_nothing_matches();
	test_select_files_for_platform_discarded_count_is_optional();
	test_python_signed_manifest_verifies_in_cpp();
	test_python_banlist_style_signature_does_not_verify_as_update();
	test_update_signature_does_not_verify_as_banlist();
	test_banlist_style_signature_does_not_verify_as_update();
	test_title_domain_signature_does_not_verify_as_update();
	test_update_domain_signature_does_not_verify_under_title_domain();
	test_manifest_without_launcher_files_is_valid_and_empty();
	test_launcher_files_round_trip_simulator_role();
	test_launcher_files_strings_role_round_trips();
	test_launcher_files_unknown_role_is_not_schema_violation();
	test_launcher_files_missing_os_is_schema_violation();
	test_launcher_files_missing_role_is_schema_violation();
	test_launcher_files_duplicate_name_is_schema_violation();
	test_launcher_files_not_array_is_schema_violation();
	test_select_launcher_files_keeps_matching_os_and_known_role();
	test_select_launcher_files_discards_unknown_role_even_with_matching_os();
	test_select_launcher_files_empty_result_when_nothing_matches();

	std::printf("update_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
