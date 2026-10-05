// Tests for the fedelex:// table-link parser (FASE 75, design/decisioni.md
// D251, design/server-duelli.md §7/§13). No network, no gframe, no
// irrlicht — same standalone-binary discipline as launcher_logic_tests.cpp:
// everything here is a plain string in, a struct or an error code out.

#include <cstdio>
#include <string>
#include "deep_link.h"

// Called from banlist_tests.cpp's main(), same pattern as every other
// *_tests.cpp in this directory.
int RunDeepLinkTests();

using namespace ygo::deep_link;

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

void test_well_formed_link_parses() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=duelli.example.org&port=57911&pass=abc123&nome=Judge", out);
	check(err == ParseError::Ok, "a well-formed link with all four fields must parse as Ok");
	check(out.host == "duelli.example.org", "host must be decoded verbatim when it has nothing to percent-decode");
	check(out.port == 57911, "port must parse to the numeric value in the query string");
	check(out.pass == L"abc123", "pass must decode to the plain ASCII value");
	check(out.nome == L"Judge", "nome must decode to the plain ASCII value");
}

void test_percent_encoded_name_decodes() {
	// "Pap\xc3\xa0" (UTF-8 for "Papà") percent-encoded, the kind of name a
	// real Telegram display name can contain.
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=p&nome=Pap%C3%A0", out);
	check(err == ParseError::Ok, "a percent-encoded UTF-8 nome must still parse as Ok");
	check(out.nome == L"Papà", "nome must decode the percent-escaped UTF-8 bytes into the right wide characters");
}

void test_plus_is_not_decoded_to_space() {
	// D251/deep_link.h's own design choice: '+' is never x-www-form-urlencoded
	// space here, only %XX is a real escape.
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=p&nome=A+B", out);
	check(err == ParseError::Ok, "a literal '+' must not make the link invalid");
	check(out.nome == L"A+B", "'+' in a query value must stay a literal '+', never decoded to a space");
}

void test_wrong_scheme_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("https://tavolo?host=h&port=1&pass=p&nome=n", out);
	check(err == ParseError::BadScheme, "a link with any scheme other than fedelex:// must be BadScheme");
}

void test_wrong_route_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://qualcosaltro?host=h&port=1&pass=p&nome=n", out);
	check(err == ParseError::BadScheme, "a fedelex:// link whose route is not exactly \"tavolo\" must be BadScheme");
}

void test_missing_field_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=p", out);
	check(err == ParseError::MissingField, "a link missing nome must be MissingField");
}

void test_empty_host_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=&port=1&pass=p&nome=n", out);
	check(err == ParseError::EmptyHost, "an empty host value must be EmptyHost, not accepted as a literal empty string");
}

void test_non_numeric_port_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=abc&pass=p&nome=n", out);
	check(err == ParseError::BadPort, "a non-numeric port must be BadPort");
}

void test_port_zero_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=0&pass=p&nome=n", out);
	check(err == ParseError::BadPort, "port 0 is never a real listening socket, must be BadPort");
}

void test_port_over_65535_is_rejected() {
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=70000&pass=p&nome=n", out);
	check(err == ParseError::BadPort, "a port above 65535 must be BadPort");
}

void test_pass_at_the_limit_is_accepted() {
	// gframe/network.h CTOS_JoinGame::pass[20] via BufferIO::EncodeUTF16:
	// the usable content is 19 code units (one reserved for the null
	// terminator) — see deep_link.cpp's kMaxFieldCodeUnits.
	TableLink out;
	std::string pass19(19, 'x');
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=" + pass19 + "&nome=n", out);
	check(err == ParseError::Ok, "a 19-character pass (the real usable limit of pass[20]) must be accepted");
}

void test_pass_over_the_limit_is_rejected() {
	TableLink out;
	std::string pass20(20, 'x');
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=" + pass20 + "&nome=n", out);
	check(err == ParseError::PasswordTooLong,
		 "a 20-character pass must be rejected outright, never silently truncated to 19 by EncodeUTF16 downstream");
}

void test_nome_over_the_limit_is_rejected() {
	TableLink out;
	std::string nome20(20, 'y');
	auto err = ParseTableLink("fedelex://tavolo?host=h&port=1&pass=p&nome=" + nome20, out);
	check(err == ParseError::NameTooLong, "a 20-character nome must be rejected outright, same reasoning as pass");
}

void test_rejected_link_leaves_out_empty() {
	TableLink out;
	out.host = "stale";
	out.port = 1234;
	out.pass = L"stale";
	out.nome = L"stale";
	auto err = ParseTableLink("not-a-link-at-all", out);
	check(err == ParseError::BadScheme, "sanity check: this input must actually fail to parse");
	check(out.host.empty() && out.port == 0 && out.pass.empty() && out.nome.empty(),
		 "a rejected link must reset `out` to default, never leave a stale struct a careless caller could act on");
}

}

int RunDeepLinkTests() {
	test_well_formed_link_parses();
	test_percent_encoded_name_decodes();
	test_plus_is_not_decoded_to_space();
	test_wrong_scheme_is_rejected();
	test_wrong_route_is_rejected();
	test_missing_field_is_rejected();
	test_empty_host_is_rejected();
	test_non_numeric_port_is_rejected();
	test_port_zero_is_rejected();
	test_port_over_65535_is_rejected();
	test_pass_at_the_limit_is_accepted();
	test_pass_over_the_limit_is_rejected();
	test_nome_over_the_limit_is_rejected();
	test_rejected_link_leaves_out_empty();

	std::printf("deep_link_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
