// Tests for FASE 83's pure server-identity helpers (gframe/server_tls.h):
// parsing of the optional `"tls"` field of a configs.json server entry, and
// the by-NAME comparison used for servers that declare it. No network, no
// gframe beyond that header — everything is a string/JSON in, a bool or an
// enum out.

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
#include "server_tls.h"

// Called from banlist_tests.cpp's main(), same pattern as every other
// *_tests.cpp in this directory.
int RunServerTlsTests();

using namespace ygo::server_tls;

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

void test_tls_field_absent_is_as_before() {
	auto entry = nlohmann::json::parse(R"({"name":"EU","address":"eu.example.org","duelport":7911})");
	check(ParseTlsField(entry) == TlsField::Absent, "an entry without \"tls\" must be Absent (existing servers unchanged)");
}

void test_tls_field_true_and_false() {
	check(ParseTlsField(nlohmann::json::parse(R"({"tls":true})")) == TlsField::Enabled, "\"tls\": true must be Enabled");
	check(ParseTlsField(nlohmann::json::parse(R"({"tls":false})")) == TlsField::Disabled, "\"tls\": false must be Disabled");
}

void test_tls_field_non_boolean_is_invalid() {
	// Fail closed: a non-boolean must never read as "plain".
	check(ParseTlsField(nlohmann::json::parse(R"({"tls":"true"})")) == TlsField::Invalid, "the string \"true\" must be Invalid, not plain");
	check(ParseTlsField(nlohmann::json::parse(R"({"tls":1})")) == TlsField::Invalid, "the number 1 must be Invalid, not plain");
	check(ParseTlsField(nlohmann::json::parse(R"({"tls":null})")) == TlsField::Invalid, "null must be Invalid, not plain");
	check(ParseTlsField(nlohmann::json::parse(R"([true])")) == TlsField::Invalid, "a non-object entry must be Invalid");
}

void test_same_name_ignores_case_and_root_dot() {
	check(SameServerName("fedelex-duelli.quoll-ruffe.ts.net", "Fedelex-Duelli.Quoll-Ruffe.TS.net"), "names differing only in case must match");
	check(SameServerName("a.example.org", "a.example.org."), "a trailing root dot must be ignored");
	check(SameServerName("a.example.org.", "a.example.org"), "a trailing root dot must be ignored on either side");
}

void test_same_name_is_exact_otherwise() {
	check(!SameServerName("fedelex-duelli.quoll-ruffe.ts.net", "evil-fedelex-duelli.quoll-ruffe.ts.net"), "a longer name ending the same way must not match");
	check(!SameServerName("fedelex-duelli.quoll-ruffe.ts.net", "fedelex-duelli.quoll-ruffe.ts.net.evil.example"), "a name with extra labels after must not match");
	check(!SameServerName("fedelex-duelli.quoll-ruffe.ts.net", "quoll-ruffe.ts.net"), "a parent domain must not match");
	check(!SameServerName("a.example.org", "a.example.org.."), "only ONE trailing dot is the root label");
}

void test_same_name_empty_never_matches() {
	check(!SameServerName("", ""), "two empty names must not match (nothing identifies anything)");
	check(!SameServerName(".", "."), "a lone root dot normalizes to empty and must not match");
	check(!SameServerName("a.example.org", ""), "empty must not match a real name");
}

void test_tls_match_needs_name_and_port() {
	const std::string name = "fedelex-duelli.quoll-ruffe.ts.net";
	check(TlsServerMatches(name, 443, name, 443), "same name and port must match");
	check(!TlsServerMatches(name, 443, name, 7911), "same name on another port is another service");
	check(!TlsServerMatches(name, 443, "203.0.113.7", 443), "an address literal must never match a TLS server by name");
}

}

int RunServerTlsTests() {
	test_tls_field_absent_is_as_before();
	test_tls_field_true_and_false();
	test_tls_field_non_boolean_is_invalid();
	test_same_name_ignores_case_and_root_dot();
	test_same_name_is_exact_otherwise();
	test_same_name_empty_never_matches();
	test_tls_match_needs_name_and_port();

	std::printf("server_tls_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
