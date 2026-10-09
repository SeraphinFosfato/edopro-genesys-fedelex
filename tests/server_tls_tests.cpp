// Tests for FASE 83's pure server-identity helpers (gframe/server_tls.h):
// parsing of the optional `"tls"` field of a configs.json server entry, and
// the by-NAME comparison used for servers that declare it. No network, no
// gframe beyond that header — everything is a string/JSON in, a bool or an
// enum out.

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include "server_tls.h"
#include "tls_roots.h"

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

// FASE 83b (point 7): the roots embedded for Windows machines whose "ROOT"
// store lacks them. The fingerprints below are written here independently of
// tls_roots.cpp, from Mozilla's CCADB "Included CA Certificate Report" of
// 2026-10-09: if someone edits the PEM, or the hex next to it, this fails.
void test_bundled_roots_are_the_published_ones() {
	struct Pinned { const char* cn; const char* sha256; };
	const Pinned pinned[] = {
		{ "ISRG Root X1", "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6" },
		{ "ISRG Root X2", "69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470" },
	};
	check(ygo::tls::kBundledRootCount == 2, "exactly ISRG Root X1 and X2 are embedded");
	for(size_t i = 0; i < ygo::tls::kBundledRootCount && i < 2; ++i) {
		const auto& root = ygo::tls::kBundledRoots[i];
		check(std::string(root.name) == pinned[i].cn, "embedded root names are in the pinned order");
		BIO* bio = BIO_new_mem_buf(root.pem, -1);
		X509* x = bio ? PEM_read_bio_X509(bio, nullptr, nullptr, nullptr) : nullptr;
		BIO_free(bio);
		check(x != nullptr, "an embedded root must parse as a PEM certificate");
		if(!x)
			continue;
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned int len = 0;
		X509_digest(x, EVP_sha256(), md, &len);
		std::string hex;
		for(unsigned int j = 0; j < len; ++j) {
			char b[3];
			std::snprintf(b, sizeof(b), "%02x", md[j]);
			hex += b;
		}
		check(hex == pinned[i].sha256, "the DER of an embedded root must hash to the published SHA-256");
		check(std::string(root.sha256) == pinned[i].sha256, "the fingerprint stored next to the PEM must be the published one");
		check(X509_check_issued(x, x) == X509_V_OK, "an embedded root must be self-signed (a root, not a leaf)");
		check(X509_cmp_current_time(X509_get0_notAfter(x)) > 0, "an embedded root must not be expired");
		X509_free(x);
	}
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
	test_bundled_roots_are_the_published_ones();

	std::printf("server_tls_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
