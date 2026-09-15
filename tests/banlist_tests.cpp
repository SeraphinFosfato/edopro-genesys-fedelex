// Tests for the signed-banlist verification module (FASE 4b).
// No network, no window, no game: everything here runs off bytes in memory and
// fixtures on disk. See design/banlist-distribution.md.
//
// SKELETON — the cases below are the required list. Each one is named and
// empty; filling them is part of the FASE 4b implementation pass.

#include <cstdio>
#include <string>
#include <vector>
#include "banlist_verify.h"
#include "lflist_hash.h"

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
std::string ReadFixture(const char* name);

// ---------------------------------------------------------------------------

void test_valid_signature_newer_version_is_staged();
void test_one_flipped_byte_is_rejected();          // and nothing is written
void test_signature_from_an_untrusted_key_is_rejected();
void test_signature_from_the_reserve_key_is_accepted();   // rotation
void test_equal_version_is_no_action_and_no_notification();
void test_lower_version_is_refused_as_rollback();
void test_unreachable_endpoint_leaves_the_active_list_alone();
void test_interrupted_staging_leaves_no_half_written_pair();
void test_points_change_the_hash();                // regression of FASE 4a
void test_malformed_format_version_is_refused_like_a_bad_signature();
void test_unknown_macro_value_is_a_schema_violation();
void test_reason_absent_and_reason_empty_are_not_the_same_thing();
// Observable without a hook: feed VerifyAndParse bytes that are BOTH badly
// signed AND not valid JSON. The answer must be BadSignature. If it ever comes
// back MalformedJson, the parser ran on bytes nobody had authenticated.
void test_json_is_never_parsed_before_the_signature_verifies();

}

int main() {
	std::printf("banlist_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
