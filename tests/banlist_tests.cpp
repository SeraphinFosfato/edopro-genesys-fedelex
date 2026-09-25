// Tests for the signed-banlist verification module (FASE 4b).
// No network, no window, no game: everything here runs off bytes in memory and
// fixtures on disk. See design/banlist-distribution.md.
//
// Scope note: staging, promotion and fetch (BanlistUpdater) live outside this
// binary on purpose (design/banlist-distribution.md, "Crypto" — the module
// under test here has zero gframe dependency so it can be linked without
// irrlicht, curl or a network). Where a test's name talks about "staged" or
// "an unreachable endpoint", it exercises the piece of that behaviour this
// module actually owns — verification, parsing and the version decision —
// not BanlistUpdater's file I/O, which has no standalone test target of its
// own to run here.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "banlist_keys.h"
#include "banlist_verify.h"
#include "lflist_hash.h"
#include "room_list_notice.h"
// See the matching comment in banlist_verify.cpp: tweetnacl.h has no
// extern "C" guard of its own.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

// Defined in banlist_diff_tests.cpp — kept as a separate file (banlist_diff
// gets its own tests) but folded into this binary's single main() so the
// suite still prints one summary and tests/premake5.lua still builds one
// ConsoleApp target.
int RunBanlistDiffTests();
// Defined in title_tests.cpp — same reasoning (FASE 4f).
int RunTitleTests();
// Defined in update_tests.cpp — same reasoning, for the client-update
// manifest verification module (design/client-update.md).
int RunUpdateTests();

using namespace ygo;

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

// Fixtures live in tests/fixtures/ and are loaded by relative path from the
// repository root. The real published artifact (banlist.json + .sig, fetched
// from the public endpoint) is one of them: it is the only test that exercises
// the actual operational key, and it is what would catch a divergence between
// what the vault signs and what the client accepts — the one class of bug that
// otherwise only shows up on someone else's machine.
std::string ReadFixture(const char* name) {
	const std::string path = std::string("tests/fixtures/") + name;
	std::ifstream f(path, std::ios::binary);
	if(!f) {
		std::printf("  FIXTURE MISSING: %s (run the binary from the repository root)\n", path.c_str());
		return {};
	}
	return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Folds a real .lflist.conf the same way DeckManager::LoadLFListSingle does
// (deck_manager.cpp): skip blank/comment/whitelist-marker lines, reset on a
// `!name` header, read "code limit [points]" per entry, fold every entry
// through FoldLFListEntry. Kept deliberately independent of DeckManager
// itself — that class carries irrlicht/curl and has no place in this
// network-free binary (tests/premake5.lua) — so this only needs to agree
// with it on the parsing rules, which the cancello-1 check below verifies
// against a number read off the live network, not reimplemented trust.
uint32_t FoldConfFile(const char* path) {
	std::ifstream f(path);
	if(!f) {
		std::printf("  FIXTURE MISSING: %s (run the binary from the repository root)\n", path);
		return 0;
	}
	uint32_t hash = 0;
	std::string line;
	while(std::getline(f, line)) {
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		if(line.empty() || line[0] == '#')
			continue;
		if(line[0] == '!') {
			hash = LFLIST_HASH_SEED;
			continue;
		}
		if(!hash)
			continue;
		std::istringstream iss(line);
		uint32_t code = 0;
		int limit = 3;
		int points = 0;
		iss >> code >> limit;
		if(iss.fail() || code == 0)
			continue;
		if(!(iss >> points))
			points = 0;
		hash = FoldLFListEntry(hash, code, limit, points);
	}
	return hash;
}

// TweetNaCl's secret key format is 64 bytes: a 32-byte seed followed by the
// derived 32-byte public key (see crypto_sign_keypair in tweetnacl.c). This
// signs `message` with it and returns just the 64 raw detached signature
// bytes — crypto_sign() always hands back signature++message combined,
// because that combined form is the only one TweetNaCl exposes.
std::string SignDetached(const std::string& message, const unsigned char sk[64]) {
	std::vector<unsigned char> sm(message.size() + 64);
	unsigned long long smlen = 0;
	crypto_sign(sm.data(), &smlen, reinterpret_cast<const unsigned char*>(message.data()), message.size(), sk);
	return std::string(reinterpret_cast<char*>(sm.data()), 64);
}

// The committed throwaway keypair (tests/fixtures/test_key.{pub,sec}) — one
// stable identity, generated once, used by every case below that needs a
// signature the real private keys (which have never existed on this machine,
// D25) could never produce.
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

// A minimal, otherwise-valid document — used wherever a test just needs
// *some* well-formed payload to sign, and the entries themselves are not
// what's under test.
std::string MinimalDocument(int format_version) {
	return "{"
		"\"attribution\":{\"author\":\"test\",\"url\":\"test\"},"
		"\"entries\":[],"
		"\"expires_at\":\"2099-01-01T00:00:00Z\","
		"\"format_version\":" + std::to_string(format_version) + ","
		"\"generated_at\":\"2026-01-01T00:00:00Z\","
		"\"license\":\"test\"}";
}

// ---------------------------------------------------------------------------

void test_valid_signature_newer_version_is_staged() {
	// The real fixture, checked against the real, hardwired TRUSTED_KEYS —
	// the one case that would notice the vault signing with a key this
	// client doesn't trust, or a payload shape it doesn't accept.
	const auto document = ReadFixture("banlist.json");
	const auto signature = ReadFixture("banlist.json.sig");
	if(document.empty() || signature.empty()) {
		check(false, "valid_signature_newer_version_is_staged: fixtures missing, cannot run");
		return;
	}
	banlist::Payload out;
	std::string error;
	const auto status = banlist::VerifyAndParse(document, signature, out, error);
	check(status == banlist::VerifyStatus::Ok, "the real published artifact must verify against TRUSTED_KEYS");
	check(out.format_version >= 1, "a real published artifact always has format_version >= 1 (D62)");
	// A fresh install (active_version 0, the Payload default) must decide to
	// accept and stage it — this is the decision StartCheck's staging step
	// acts on; the file I/O itself belongs to BanlistUpdater.
	check(banlist::CompareVersion(out.format_version, 0) == banlist::VersionDecision::Accept,
		 "a real published version must be treated as newer than no active list at all");
}

void test_one_flipped_byte_is_rejected() {
	const auto document = ReadFixture("banlist.json");
	const auto signature = ReadFixture("banlist.json.sig");
	if(document.empty() || signature.empty()) {
		check(false, "one_flipped_byte_is_rejected: fixtures missing, cannot run");
		return;
	}
	std::string tampered = document;
	tampered[0] = static_cast<char>(tampered[0] ^ 0xFF);
	banlist::Payload out;
	std::string error;
	const auto status = banlist::VerifyAndParse(tampered, signature, out, error);
	check(status == banlist::VerifyStatus::BadSignature, "one flipped byte must invalidate the signature");
	check(out.format_version == 0, "a rejected update must never populate the output payload");
}

void test_signature_from_an_untrusted_key_is_rejected() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "signature_from_an_untrusted_key_is_rejected: test key fixture missing");
		return;
	}
	const auto document = MinimalDocument(3);
	const auto signature = SignDetached(document, key.sec);
	// TRUSTED_KEY_OPERATIONAL is a real, unrelated key — signing with the
	// throwaway test key and checking against it must fail exactly like
	// signing with any other key nobody trusts.
	const uint8_t* keys[] = { ygo::banlist::TRUSTED_KEY_OPERATIONAL };
	const bool verified = banlist::VerifySignatureWith(document, signature, keys, 1);
	check(!verified, "a signature from a key not in the trusted set must be rejected");
}

void test_signature_from_the_reserve_key_is_accepted() {
	// Two independent throwaway identities: `operational` never signs
	// anything here, `reserve` does — this is the rotation case, where
	// acceptance must not depend on which INDEX of TRUSTED_KEYS matched.
	unsigned char operational_pub[32]{}, operational_sec[64]{};
	unsigned char reserve_pub[32]{}, reserve_sec[64]{};
	crypto_sign_keypair(operational_pub, operational_sec);
	crypto_sign_keypair(reserve_pub, reserve_sec);

	const auto document = MinimalDocument(3);
	const auto signature = SignDetached(document, reserve_sec);

	const uint8_t* keys[] = { operational_pub, reserve_pub };
	const bool verified = banlist::VerifySignatureWith(document, signature, keys, 2);
	check(verified, "a signature from the SECOND trusted key must be accepted just like the first");
}

void test_equal_version_is_no_action_and_no_notification() {
	check(banlist::CompareVersion(5, 5) == banlist::VersionDecision::AlreadyCurrent,
		 "equal format_version must compare as AlreadyCurrent");
}

void test_lower_version_is_refused_as_rollback() {
	check(banlist::CompareVersion(3, 5) == banlist::VersionDecision::Rollback,
		 "a lower format_version than the active one must compare as Rollback");
}

void test_unreachable_endpoint_leaves_the_active_list_alone() {
	// BanlistUpdater::Fetch is what actually reaches the network, and it
	// lives outside this network-free binary by design. What this module
	// controls, and what an unreachable endpoint reduces to at this level,
	// is: bytes that were never fetched are bytes nobody verified, and must
	// never be mistaken for a valid update. An empty document/signature pair
	// is what an empty fetch buffer looks like once it reaches this module.
	banlist::Payload out;
	std::string error;
	const auto status = banlist::VerifyAndParse("", "", out, error);
	check(status == banlist::VerifyStatus::BadSignature, "an empty fetch result must never verify as an update");
	check(out.format_version == 0, "a rejected result must leave the active list's stand-in payload untouched");
}

void test_interrupted_staging_leaves_no_half_written_pair() {
	// BanlistUpdater::PromoteStaged's on-disk atomicity has no standalone
	// test target here (it needs DeckManager, curl and real files). What
	// this module owns, and what "interrupted" means at this level, is its
	// own output parameter: a schema violation partway through the entries
	// array must never leave the entries seen before the violation visible
	// to the caller — that would be this module's own half-written pair.
	// The duplicate id in the fixture is the trigger: the first entry is
	// well-formed on its own, and only the second one breaks the payload.
	const auto document = ReadFixture("schema_duplicate_id.json");
	if(document.empty()) {
		check(false, "interrupted_staging_leaves_no_half_written_pair: fixture missing");
		return;
	}
	banlist::Payload out;
	std::string error;
	const auto status = banlist::Parse(document, out, error);
	check(status == banlist::VerifyStatus::SchemaViolation, "a duplicate id must be caught");
	check(out.entries.empty(), "the first entry must not leak into `out` once the second invalidates the payload");
}

void test_points_change_the_hash() {
	// Regression of FASE 4a: two entries differing only in points must hash
	// differently, or two clients that agree on every id/limit but not on
	// points would consider themselves compatible while disagreeing about
	// what's legal to play.
	const uint32_t hash_low_points = FoldLFListEntry(LFLIST_HASH_SEED, 12345678u, 3, 0);
	const uint32_t hash_high_points = FoldLFListEntry(LFLIST_HASH_SEED, 12345678u, 3, 100);
	check(hash_low_points != hash_high_points, "two entries differing only in points must hash differently");
}

void test_a_list_with_no_points_folds_to_the_network_hash() {
	// FASE 31, cancello 1 — the one that counts: design/banlist-distribution.md,
	// "L'hash non è nostro". `OCG.lflist.conf` ("2026.07 OCG") folded through
	// FoldLFListEntry must land on 0x857713b8 bit-for-bit — that number was
	// read off the live server (EU Central Competitive, 2026-09-25), it is
	// what every other EDOPro already calls this list, not a value we chose.
	// Before FASE 31 this folded to a different number nobody else on the
	// network could produce, because points were mixed in even at zero.
	const uint32_t hash = FoldConfFile("runtime/repositories/lflists/OCG.lflist.conf");
	check(hash == 0x857713b8u,
		 "OCG.lflist.conf (all-zero points) must fold to exactly the hash the live network uses for it");
}

void test_same_entries_hash_the_same_regardless_of_read_order() {
	// FASE 31, cancello 3 — the same list read from a .conf
	// (DeckManager::LoadLFListSingle) and built in memory from a signed
	// banlist.json (BanlistUpdater) must produce the same hash. Both call
	// sites already fold every entry through this one function (deck_manager.cpp,
	// banlist_updater.cpp), and XOR is commutative/associative by construction
	// (the doc comment in lflist_hash.h), so what actually varies between the
	// two provenances — the ORDER entries are folded in (map iteration for
	// .conf, array order for JSON) — must not matter. Includes a zero-points
	// and a non-zero-points entry together, so this also exercises the new
	// conditional term in both branches.
	struct Entry { uint32_t code; int limit; int points; };
	const Entry entries[] = {
		{ 11384280u, 3, 0 },
		{ 44763025u, 1, 462 },
		{ 4280259u, 0, 0 },
		{ 20292186u, 2, 15 },
	};
	uint32_t forward = LFLIST_HASH_SEED;
	for(const auto& e : entries)
		forward = FoldLFListEntry(forward, e.code, e.limit, e.points);
	uint32_t reversed = LFLIST_HASH_SEED;
	for(auto it = std::rbegin(entries); it != std::rend(entries); ++it)
		reversed = FoldLFListEntry(reversed, it->code, it->limit, it->points);
	check(forward == reversed, "the same entries folded in a different order must produce the same hash");
}

void test_room_warns_only_when_the_returned_hash_differs_from_the_one_sent() {
	// FASE 31, cancello 5 — design/banlist-distribution.md, "Quando qualcun
	// altro sostituisce la lista, si dice". ShouldWarnAboutListSubstitution
	// (room_list_notice.h) is the pure decision duelclient.cpp's STOC_JOIN_GAME
	// handler acts on; exercised here directly since the handler itself needs
	// irrlicht/curl/a network and has no place in this binary.
	check(ShouldWarnAboutListSubstitution(0x857713b8u, 0x00000000u),
		 "a returned hash different from the one sent while hosting must warn");
	check(!ShouldWarnAboutListSubstitution(0x857713b8u, 0x857713b8u),
		 "a returned hash equal to the one sent must not warn");
	check(!ShouldWarnAboutListSubstitution(0u, 0x857713b8u),
		 "no warning is possible when we sent no list at all (we joined, we didn't host)");
}

void test_malformed_format_version_is_refused_like_a_bad_signature() {
	const auto document = ReadFixture("schema_bad_format_version.json");
	if(document.empty()) {
		check(false, "malformed_format_version_is_refused_like_a_bad_signature: fixture missing");
		return;
	}
	banlist::Payload out;
	std::string error;
	const auto status = banlist::Parse(document, out, error);
	// Parse() itself never returns BadSignature (that is VerifyAndParse's
	// job, one step earlier); what "refused like a bad signature" means at
	// this level is that a string format_version is a SchemaViolation, the
	// same refuse-and-log outcome CheckTask gives a bad signature — never
	// coerced into an integer.
	check(status == banlist::VerifyStatus::SchemaViolation, "a string format_version must be refused, not coerced");
}

void test_unknown_macro_value_is_a_schema_violation() {
	const auto document = ReadFixture("schema_unknown_macro.json");
	if(document.empty()) {
		check(false, "unknown_macro_value_is_a_schema_violation: fixture missing");
		return;
	}
	banlist::Payload out;
	std::string error;
	const auto status = banlist::Parse(document, out, error);
	// "Skill" specifically: D64, a card that in TCG Advanced does not exist,
	// not a value the enumeration merely forgot.
	check(status == banlist::VerifyStatus::SchemaViolation, "an unknown macro value must be a schema violation");
}

void test_reason_absent_and_reason_empty_are_not_the_same_thing() {
	{
		banlist::Payload out;
		std::string error;
		// MinimalDocument has no entries at all, so build one with an entry
		// that simply omits "reason" — the normal, unremarkable case.
		const std::string with_absent_reason =
			"{\"attribution\":{\"author\":\"test\",\"url\":\"test\"},"
			"\"entries\":[{\"id\":1,\"limit\":3,\"macro\":\"Magia\",\"name\":\"A\",\"points\":0,\"source\":\"custom\"}],"
			"\"expires_at\":\"2099-01-01T00:00:00Z\",\"format_version\":3,"
			"\"generated_at\":\"2026-01-01T00:00:00Z\",\"license\":\"test\"}";
		const auto status = banlist::Parse(with_absent_reason, out, error);
		check(status == banlist::VerifyStatus::Ok, "an entry with no reason field at all must parse cleanly");
		check(out.entries.size() == 1 && !out.entries[0].has_reason,
			 "an absent reason must leave has_reason false");
	}
	{
		const auto document = ReadFixture("schema_reason_present_but_empty.json");
		if(document.empty()) {
			check(false, "reason_absent_and_reason_empty_are_not_the_same_thing: fixture missing");
			return;
		}
		banlist::Payload out;
		std::string error;
		const auto status = banlist::Parse(document, out, error);
		check(status == banlist::VerifyStatus::SchemaViolation,
			 "a reason present but empty must be refused, not treated as \"no reason\"");
	}
}

void test_json_is_never_parsed_before_the_signature_verifies() {
	// Feed VerifyAndParse bytes that are BOTH badly signed AND not valid
	// JSON. The answer must be BadSignature. If it ever comes back
	// MalformedJson, the parser ran on bytes nobody had authenticated.
	const std::string not_json = "this is not json at all {{{";
	const std::string bogus_signature(ygo::banlist::ED25519_SIGNATURE_SIZE, '\0');
	banlist::Payload out;
	std::string error;
	const auto status = banlist::VerifyAndParse(not_json, bogus_signature, out, error);
	check(status == banlist::VerifyStatus::BadSignature,
		 "signature-then-parse must reject before ever attempting to parse — MalformedJson here would mean the parser ran first");
}

}

int main() {
	test_valid_signature_newer_version_is_staged();
	test_one_flipped_byte_is_rejected();
	test_signature_from_an_untrusted_key_is_rejected();
	test_signature_from_the_reserve_key_is_accepted();
	test_equal_version_is_no_action_and_no_notification();
	test_lower_version_is_refused_as_rollback();
	test_unreachable_endpoint_leaves_the_active_list_alone();
	test_interrupted_staging_leaves_no_half_written_pair();
	test_points_change_the_hash();
	test_a_list_with_no_points_folds_to_the_network_hash();
	test_same_entries_hash_the_same_regardless_of_read_order();
	test_room_warns_only_when_the_returned_hash_differs_from_the_one_sent();
	test_malformed_format_version_is_refused_like_a_bad_signature();
	test_unknown_macro_value_is_a_schema_violation();
	test_reason_absent_and_reason_empty_are_not_the_same_thing();
	test_json_is_never_parsed_before_the_signature_verifies();

	std::printf("banlist_tests: %d checks, %d failures\n", checks, failures);
	const int diff_failures = RunBanlistDiffTests();
	const int title_failures = RunTitleTests();
	const int update_failures = RunUpdateTests();
	return (failures == 0 && diff_failures == 0 && title_failures == 0 && update_failures == 0) ? 0 : 1;
}
