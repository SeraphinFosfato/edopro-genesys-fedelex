// Tests for the title/revocation/suspension verification and access-decision
// modules (FASE 4f, D102-D104).
// No network, no window, no game: title_verify.cpp and title_state.cpp have
// zero gframe dependency by design (see their own header comments) so they
// link into this binary the same way banlist_verify.cpp does.
//
// Scope note: title_store.h (disk persistence) has no standalone target here
// — it needs FileStream/Utils/logging, same reason BanlistUpdater has none.
// What this file CAN and does cover is everything that decides what a
// message means or whether it applies: D103's supersede rule and D104's
// binding rule both live as pure functions in title_state.h specifically so
// they are testable here without gframe.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "title_keys.h"
#include "title_state.h"
#include "title_verify.h"
// See the matching comment in banlist_verify.cpp: tweetnacl.h has no
// extern "C" guard of its own.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

// Called from banlist_tests.cpp's main() so the whole suite is still one
// binary with one summary line, per tests/premake5.lua's single ConsoleApp
// target.
int RunTitleTests();

using namespace ygo::title;

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

// Same throwaway keypair as banlist_tests.cpp, same reason (D25): the real
// title private keys have never existed on this machine and never will.
// Loaded from the SAME fixture files banlist_tests.cpp already reads —
// there is nothing banlist-specific about an Ed25519 keypair, and a second
// committed copy would just be the first one with a different filename.
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
	uint8_t pub[32]{};
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

// Standard base64 (padded, standard alphabet) — matches what
// title_verify.cpp's own decoder expects on the wire, and what
// Buffer#toString("base64") produces on the bot.
std::string Base64Encode(const uint8_t* data, size_t len) {
	static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	size_t i = 0;
	for(; i + 3 <= len; i += 3) {
		const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
							   (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
		out += alphabet[(chunk >> 18) & 0x3f];
		out += alphabet[(chunk >> 12) & 0x3f];
		out += alphabet[(chunk >> 6) & 0x3f];
		out += alphabet[chunk & 0x3f];
	}
	const size_t remaining = len - i;
	if(remaining == 1) {
		const uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
		out += alphabet[(chunk >> 18) & 0x3f];
		out += alphabet[(chunk >> 12) & 0x3f];
		out += "==";
	} else if(remaining == 2) {
		const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
		out += alphabet[(chunk >> 18) & 0x3f];
		out += alphabet[(chunk >> 12) & 0x3f];
		out += alphabet[(chunk >> 6) & 0x3f];
		out += '=';
	}
	return out;
}

// Signs `domain + "\n" + canonical` exactly like signWithDomain on the bot
// (src/util/titleKey.ts) and title_verify.cpp's own VerifyWithDomainUsing,
// and returns the base64 signature ready to drop into a "signature" field.
std::string SignCanonical(const std::string& domain, const std::string& canonical, const unsigned char sk[64]) {
	std::string message = domain + "\n" + canonical;
	std::vector<unsigned char> sm(message.size() + 64);
	unsigned long long smlen = 0;
	crypto_sign(sm.data(), &smlen, reinterpret_cast<const unsigned char*>(message.data()), message.size(), sk);
	return Base64Encode(sm.data(), 64);
}

// Builds a title/revocation/suspension JSON document with a real signature
// from the throwaway key, exercising the SAME canonical-reconstruction code
// VerifyAndParseTitleWith etc. use internally (this function only builds the
// inputs; it never reconstructs the canonical string a second time itself,
// which is the whole point — see the header comment on *With above).
std::string MakeTitleDocument(const std::string& user_ref, const std::string& issued_at,
							  const std::string& expires_at, const unsigned char sk[64]) {
	const std::string canonical = user_ref + "\n" + issued_at + "\n" + expires_at;
	const std::string sig = SignCanonical(TITLE_DOMAIN, canonical, sk);
	return "{\"type\":\"title\",\"user_ref\":\"" + user_ref + "\",\"issued_at\":\"" + issued_at +
		  "\",\"expires_at\":\"" + expires_at + "\",\"signature\":\"" + sig + "\"}";
}

std::string MakeRevocationDocument(const std::string& user_ref, const std::string& revoked_at,
								   const std::string& banned_until_json_literal, const unsigned char sk[64]) {
	const std::string banned_field = banned_until_json_literal == "null" ? std::string() : banned_until_json_literal;
	const std::string canonical = user_ref + "\n" + revoked_at + "\n" + banned_field;
	const std::string sig = SignCanonical(REVOCATION_DOMAIN, canonical, sk);
	const std::string banned_json_value =
		banned_until_json_literal == "null" ? std::string("null") : ("\"" + banned_until_json_literal + "\"");
	return "{\"type\":\"revocation\",\"user_ref\":\"" + user_ref + "\",\"revoked_at\":\"" + revoked_at +
		  "\",\"banned_until\":" + banned_json_value + ",\"signature\":\"" + sig + "\"}";
}

std::string MakeSuspensionDocument(const std::string& user_ref, const std::string& suspended_at,
								   const unsigned char sk[64]) {
	const std::string canonical = user_ref + "\n" + suspended_at;
	const std::string sig = SignCanonical(SUSPENSION_DOMAIN, canonical, sk);
	return "{\"type\":\"suspension\",\"user_ref\":\"" + user_ref + "\",\"suspended_at\":\"" + suspended_at +
		  "\",\"signature\":\"" + sig + "\"}";
}

// ---------------------------------------------------------------------------
// Canonical reconstruction — byte-identical to what the bot signs.

void test_title_canonical_reconstruction_round_trips() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "title_canonical_reconstruction_round_trips: test key fixtures missing");
		return;
	}
	const auto document = MakeTitleDocument("user-123", "2026-09-16T12:00:00.000Z", "2026-09-30T12:00:00.000Z", key.sec);
	TitleFields out;
	std::string error;
	const uint8_t* keys[] = { key.pub };
	const auto status = VerifyAndParseTitleWith(document, out, error, keys, 1);
	check(status == VerifyStatus::Ok, "a title signed over the exact canonical string must verify");
	check(out.user_ref == "user-123", "user_ref must round-trip");
	check(out.issued_at == "2026-09-16T12:00:00.000Z", "issued_at must round-trip");
	check(out.expires_at == "2026-09-30T12:00:00.000Z", "expires_at must round-trip");
}

void test_revocation_canonical_reconstruction_with_banned_until_string() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "revocation_canonical_reconstruction_with_banned_until_string: test key fixtures missing");
		return;
	}
	const auto document = MakeRevocationDocument("user-123", "2026-09-16T12:00:00.000Z", "2026-10-01T00:00:00.000Z", key.sec);
	RevocationFields out;
	std::string error;
	const uint8_t* keys[] = { key.pub };
	const auto status = VerifyAndParseRevocationWith(document, out, error, keys, 1);
	check(status == VerifyStatus::Ok, "a revocation with a string banned_until must verify");
	check(out.has_banned_until, "has_banned_until must be true when the wire value is a string");
	check(out.banned_until == "2026-10-01T00:00:00.000Z", "banned_until must round-trip");
}

void test_revocation_canonical_reconstruction_with_null_banned_until() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "revocation_canonical_reconstruction_with_null_banned_until: test key fixtures missing");
		return;
	}
	// JSON null folds to "" BEFORE the newline join (same as the bot's own
	// `banned_until ?? ""`) — MakeRevocationDocument signs over "" when
	// asked for the literal "null", matching that fold exactly.
	const auto document = MakeRevocationDocument("user-123", "2026-09-16T12:00:00.000Z", "null", key.sec);
	RevocationFields out;
	std::string error;
	const uint8_t* keys[] = { key.pub };
	const auto status = VerifyAndParseRevocationWith(document, out, error, keys, 1);
	check(status == VerifyStatus::Ok, "a revocation with a null banned_until must verify");
	check(!out.has_banned_until, "has_banned_until must be false when the wire value is JSON null");
	check(out.banned_until.empty(), "banned_until must be empty when the wire value is JSON null");
}

void test_revocation_missing_banned_until_key_is_schema_violation() {
	// No signature can make this valid: the key must be PRESENT (as string
	// or null), never absent — see title_verify.cpp's own comment on this.
	const std::string document = "{\"type\":\"revocation\",\"user_ref\":\"u\",\"revoked_at\":\"2026-09-16T00:00:00.000Z\","
								 "\"signature\":\"AA==\"}";
	RevocationFields out;
	std::string error;
	const auto status = VerifyAndParseRevocation(document, out, error);
	check(status == VerifyStatus::SchemaViolation, "an absent banned_until key must be a schema violation, not defaulted");
}

void test_suspension_canonical_reconstruction_round_trips() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "suspension_canonical_reconstruction_round_trips: test key fixtures missing");
		return;
	}
	const auto document = MakeSuspensionDocument("user-123", "2026-09-16T12:00:00.000Z", key.sec);
	SuspensionFields out;
	std::string error;
	const uint8_t* keys[] = { key.pub };
	const auto status = VerifyAndParseSuspensionWith(document, out, error, keys, 1);
	check(status == VerifyStatus::Ok, "a suspension signed over the exact canonical string must verify (D90 provisional shape)");
	check(out.suspended_at == "2026-09-16T12:00:00.000Z", "suspended_at must round-trip");
}

// ---------------------------------------------------------------------------
// Domain separation — the same bytes must never verify under the wrong type.

void test_title_signature_does_not_verify_as_a_revocation() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "title_signature_does_not_verify_as_a_revocation: test key fixtures missing");
		return;
	}
	// Same fields, same values — only the "type"/domain differs. Take a
	// validly-signed TITLE document and re-tag it as a revocation with the
	// SAME signature: it must not verify, because REVOCATION_DOMAIN was
	// never part of what was actually signed.
	const std::string user_ref = "user-123", at = "2026-09-16T12:00:00.000Z";
	const std::string title_canonical = user_ref + "\n" + at + "\n" + "2026-09-30T12:00:00.000Z";
	const std::string sig = SignCanonical(TITLE_DOMAIN, title_canonical, key.sec);
	const uint8_t* keys[] = { key.pub };
	// Re-verify those exact signed bytes under the revocation domain with a
	// matching (user_ref, at, "") canonical: must fail.
	const std::string revocation_canonical = user_ref + "\n" + at + "\n";
	check(!VerifyWithDomainUsing(REVOCATION_DOMAIN, revocation_canonical, sig, keys, 1),
		 "a title signature must not verify under the revocation domain");
}

void test_revocation_signature_does_not_verify_as_a_suspension() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "revocation_signature_does_not_verify_as_a_suspension: test key fixtures missing");
		return;
	}
	const std::string user_ref = "user-123", at = "2026-09-16T12:00:00.000Z";
	const std::string revocation_canonical = user_ref + "\n" + at + "\n";
	const std::string sig = SignCanonical(REVOCATION_DOMAIN, revocation_canonical, key.sec);
	const uint8_t* keys[] = { key.pub };
	const std::string suspension_canonical = user_ref + "\n" + at;
	check(!VerifyWithDomainUsing(SUSPENSION_DOMAIN, suspension_canonical, sig, keys, 1),
		 "a revocation signature must not verify under the suspension domain");
}

// ---------------------------------------------------------------------------
// D102: bad signature / untrusted key / malformed body never produce Ok, and
// never touch `out`.

void test_signature_from_an_untrusted_key_is_rejected() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "signature_from_an_untrusted_key_is_rejected: test key fixtures missing");
		return;
	}
	const auto document = MakeTitleDocument("user-123", "2026-09-16T12:00:00.000Z", "2026-09-30T12:00:00.000Z", key.sec);
	// VerifyAndParseTitle (no "With") is hardwired to the real
	// title::TRUSTED_KEYS, which this test key is not a member of.
	TitleFields out;
	std::string error;
	const auto status = VerifyAndParseTitle(document, out, error);
	check(status == VerifyStatus::BadSignature, "a title signed by a key outside TRUSTED_KEYS must be rejected");
}

void test_bad_signature_never_touches_out() {
	const auto key = LoadTestKey();
	if(!key.loaded) {
		check(false, "bad_signature_never_touches_out: test key fixtures missing");
		return;
	}
	auto document = MakeTitleDocument("user-123", "2026-09-16T12:00:00.000Z", "2026-09-30T12:00:00.000Z", key.sec);
	// Document ends with ..."<sig-last-char>"} — index size()-3 is the last
	// character of the base64 signature itself, not the surrounding
	// JSON syntax (back() would be '}', which would corrupt the JSON
	// instead of the signature and misreport as MalformedJson).
	auto& sig_last_char = document[document.size() - 3];
	sig_last_char = sig_last_char == 'A' ? 'B' : 'A';
	TitleFields out;
	out.user_ref = "sentinel";
	std::string error;
	const uint8_t* keys[] = { key.pub };
	const auto status = VerifyAndParseTitleWith(document, out, error, keys, 1);
	check(status == VerifyStatus::BadSignature, "a flipped signature byte must be rejected");
	check(out.user_ref == "sentinel", "out must be untouched on a rejected verification");
}

void test_malformed_json_is_never_parsed_before_verification_would_matter() {
	TitleFields out;
	std::string error;
	const auto status = VerifyAndParseTitle("not json at all {{{", out, error);
	check(status == VerifyStatus::MalformedJson, "unparseable bytes must report MalformedJson, not crash or default");
}

void test_peek_type_reports_unknown_for_missing_or_unrecognized_type() {
	check(PeekType("{\"user_ref\":\"u\"}") == MessageType::Unknown, "a document with no \"type\" field is Unknown");
	check(PeekType("{\"type\":\"something_else\"}") == MessageType::Unknown, "an unrecognized \"type\" value is Unknown");
	check(PeekType("not json") == MessageType::Unknown, "unparseable bytes are Unknown, not a crash");
	check(PeekType("{\"type\":\"title\"}") == MessageType::Title, "\"type\":\"title\" is recognized");
	check(PeekType("{\"type\":\"revocation\"}") == MessageType::Revocation, "\"type\":\"revocation\" is recognized");
	check(PeekType("{\"type\":\"suspension\"}") == MessageType::Suspension, "\"type\":\"suspension\" is recognized (D90)");
}

// ---------------------------------------------------------------------------
// D103: a title supersedes a block only when issued strictly after it.

StoredTitle MakeTitle(const std::string& issued_at) {
	StoredTitle t;
	t.present = true;
	t.user_ref = "user-123";
	t.issued_at = issued_at;
	t.expires_at = "2099-01-01T00:00:00.000Z";
	return t;
}

StoredBlock MakeRevocationBlock(const std::string& at) {
	StoredBlock b;
	b.kind = StoredBlock::Kind::Revocation;
	b.user_ref = "user-123";
	b.at = at;
	return b;
}

void test_title_issued_after_revocation_supersedes_it() {
	const auto title = MakeTitle("2026-09-17T00:00:00.000Z");
	const auto block = MakeRevocationBlock("2026-09-16T00:00:00.000Z");
	check(TitleSupersedes(title, block), "a title issued strictly after the revocation must supersede it");
	check(ComputeAccess(title, block, std::time(nullptr)) == AccessState::Active,
		 "superseded revocation: access must be Active (this is the entire /unban mechanism, D103)");
}

void test_title_issued_before_revocation_does_not_supersede_it() {
	const auto title = MakeTitle("2026-09-15T00:00:00.000Z");
	const auto block = MakeRevocationBlock("2026-09-16T00:00:00.000Z");
	check(!TitleSupersedes(title, block), "a title issued before the revocation must not supersede it");
	check(ComputeAccess(title, block, std::time(nullptr)) == AccessState::Revoked,
		 "not superseded: access must stay Revoked");
}

void test_equal_timestamps_do_not_supersede() {
	const auto title = MakeTitle("2026-09-16T00:00:00.000Z");
	const auto block = MakeRevocationBlock("2026-09-16T00:00:00.000Z");
	check(!TitleSupersedes(title, block), "a tie must fail closed — stay blocked, not supersede");
}

void test_replayed_old_title_cannot_unblock_a_later_revocation() {
	// The replay scenario named in the design doc: an attacker captures a
	// VALID title from before a ban and replays it after the ban lands. Its
	// issued_at is still whatever it always was — earlier than the
	// revocation — so it can never supersede it, no matter how many times
	// it is replayed.
	const auto old_title = MakeTitle("2026-09-01T00:00:00.000Z");
	const auto block = MakeRevocationBlock("2026-09-16T00:00:00.000Z");
	check(ComputeAccess(old_title, block, std::time(nullptr)) == AccessState::Revoked,
		 "a replayed pre-ban title must not grant access after a later revocation");
}

void test_no_block_present_means_title_supersedes_trivially() {
	const auto title = MakeTitle("2026-09-16T00:00:00.000Z");
	const StoredBlock no_block; // Kind::None by default
	check(TitleSupersedes(title, no_block), "with nothing to supersede, a present title always wins");
}

void test_suspension_blocks_the_same_way_a_revocation_does() {
	const auto title = MakeTitle("2026-09-15T00:00:00.000Z");
	StoredBlock suspension;
	suspension.kind = StoredBlock::Kind::Suspension;
	suspension.at = "2026-09-16T00:00:00.000Z";
	check(ComputeAccess(title, suspension, std::time(nullptr)) == AccessState::Suspended,
		 "an unsuperseded suspension must report Suspended, not Revoked (§8 distinct messaging)");
}

// ---------------------------------------------------------------------------
// Access states beyond the block/supersede axis: no title at all, expiry.

void test_no_title_at_all_is_no_title() {
	const StoredTitle no_title;
	const StoredBlock no_block;
	check(ComputeAccess(no_title, no_block, std::time(nullptr)) == AccessState::NoTitle,
		 "a fresh install with nothing cached must report NoTitle");
}

void test_expired_title_with_no_block_is_title_expired() {
	StoredTitle title;
	title.present = true;
	title.issued_at = "2020-01-01T00:00:00.000Z";
	// Long past relative to real wall-clock time — avoids this test file
	// needing its own portable UTC-parsing helper just to construct a
	// synthetic "now"; title_state.cpp's own TimegmPortable (already
	// platform-guarded) does that parsing internally.
	title.expires_at = "2020-01-15T00:00:00.000Z";
	const StoredBlock no_block;
	check(ComputeAccess(title, no_block, std::time(nullptr)) == AccessState::TitleExpired,
		 "a title past its expires_at with no block must report TitleExpired");
}

void test_unparseable_expiry_never_blocks_play() {
	check(!IsExpired("not a date", std::time(nullptr)),
		 "an unparseable expires_at must never be treated as expired — never punish the player for our own parsing gap");
	check(!IsExpired("", std::time(nullptr)), "an empty expires_at must never be treated as expired");
}

void test_not_yet_expired_title_is_active() {
	StoredTitle title;
	title.present = true;
	title.issued_at = "2026-01-01T00:00:00.000Z";
	title.expires_at = "2099-01-01T00:00:00.000Z";
	const StoredBlock no_block;
	check(ComputeAccess(title, no_block, std::time(nullptr)) == AccessState::Active,
		 "a title well within its validity window with no block must be Active");
}

// ---------------------------------------------------------------------------
// D104: user_ref binding.

void test_first_message_establishes_the_binding() {
	check(UserRefAccepted(false, "", "user-123"), "with no binding yet, any user_ref is accepted (it becomes the binding)");
}

void test_matching_user_ref_is_accepted_once_bound() {
	check(UserRefAccepted(true, "user-123", "user-123"), "a user_ref matching the existing binding must be accepted");
}

void test_mismatched_user_ref_is_rejected_even_if_it_would_otherwise_be_valid() {
	// This is the attack D104 exists to stop: someone else's — even a
	// legitimately, validly signed — revocation grafted into this client's
	// cache must not apply, because it was never issued for this user_ref.
	check(!UserRefAccepted(true, "user-123", "someone-elses-ref"),
		 "a user_ref that does not match the bound one must be rejected regardless of signature validity");
}

}

int RunTitleTests() {
	test_title_canonical_reconstruction_round_trips();
	test_revocation_canonical_reconstruction_with_banned_until_string();
	test_revocation_canonical_reconstruction_with_null_banned_until();
	test_revocation_missing_banned_until_key_is_schema_violation();
	test_suspension_canonical_reconstruction_round_trips();
	test_title_signature_does_not_verify_as_a_revocation();
	test_revocation_signature_does_not_verify_as_a_suspension();
	test_signature_from_an_untrusted_key_is_rejected();
	test_bad_signature_never_touches_out();
	test_malformed_json_is_never_parsed_before_verification_would_matter();
	test_peek_type_reports_unknown_for_missing_or_unrecognized_type();
	test_title_issued_after_revocation_supersedes_it();
	test_title_issued_before_revocation_does_not_supersede_it();
	test_equal_timestamps_do_not_supersede();
	test_replayed_old_title_cannot_unblock_a_later_revocation();
	test_no_block_present_means_title_supersedes_trivially();
	test_suspension_blocks_the_same_way_a_revocation_does();
	test_no_title_at_all_is_no_title();
	test_expired_title_with_no_block_is_title_expired();
	test_unparseable_expiry_never_blocks_play();
	test_not_yet_expired_title_is_active();
	test_first_message_establishes_the_binding();
	test_matching_user_ref_is_accepted_once_bound();
	test_mismatched_user_ref_is_rejected_even_if_it_would_otherwise_be_valid();

	std::printf("title_tests: %d checks, %d failures\n", checks, failures);
	return failures;
}
