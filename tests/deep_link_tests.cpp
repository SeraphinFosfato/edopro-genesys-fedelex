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


// FASE 75d. On Windows the link reached the game but was discarded as
// BadScheme (2026-10-10, build 16). The OS or the browser may rewrite a
// custom-scheme URL — a '/' after the host, upper/lower case — without
// changing what it means; these variants must give the same fields as the
// canonical form. Anything that WOULD change the meaning stays BadScheme.
const char* const kCanonicalQuery = "host=duelli.example.org&port=443&pass=abc123&nome=Judge";

void check_same_as_canonical(const std::string& link, const char* what) {
	TableLink out;
	auto err = ParseTableLink(link, out);
	check(err == ParseError::Ok, what);
	check(out.host == "duelli.example.org" && out.port == 443 && out.pass == L"abc123" && out.nome == L"Judge",
		 what);
}

void test_harmless_os_variants_give_the_canonical_fields() {
	const std::string q = kCanonicalQuery;
	check_same_as_canonical("fedelex://tavolo?" + q, "canonical form");
	check_same_as_canonical("fedelex://tavolo/?" + q, "a '/' between tavolo and '?' (Windows/browser normalisation)");
	check_same_as_canonical("FEDELEX://TAVOLO?" + q, "scheme and host in upper case");
	check_same_as_canonical("Fedelex://Tavolo/?" + q, "scheme and host in mixed case, together with the '/'");
	check_same_as_canonical("fedelex://tavolo?" + q + "/", "one literal '/' at the very end of the link");
	check_same_as_canonical("fedelex://tavolo/?" + q + "/", "the '/' after tavolo and the one at the end, together");
}

void test_variants_that_change_the_meaning_stay_bad_scheme() {
	const std::string q = std::string("?") + kCanonicalQuery;
	const char* const rejected[] = {
		"fedelex://altro",          // another host
		"fedelex://tavolo/x",       // a path under tavolo
		"fedelex://tavolo//",       // two slashes, not one
		"fedelex://tavolox",        // tavolo is not a prefix match
		"fedelex://tavolo.",        // nor a host with a suffix
		"fedelex:/tavolo",          // one slash after the scheme
		"fedelex:tavolo",           // none
		"fedelex:///tavolo",        // three
		"http://tavolo",            // another scheme
		"xfedelex://tavolo",        // scheme with a prefix
	};
	for(const char* head : rejected) {
		TableLink out;
		auto err = ParseTableLink(std::string(head) + q, out);
		check(err == ParseError::BadScheme, head);
		check(out.host.empty() && out.port == 0 && out.pass.empty() && out.nome.empty(),
			 "a BadScheme variant must leave `out` empty");
	}
	TableLink out;
	// no '?' at all: nothing to read, and "tavolo/" alone is not a table link
	check(ParseTableLink("fedelex://tavolo", out) == ParseError::BadScheme, "no '?' must be BadScheme");
	check(ParseTableLink("fedelex://tavolo/", out) == ParseError::BadScheme, "tavolo/ with no '?' must be BadScheme");
	// two trailing slashes: only ONE is tolerated, the second would become part of the last value
	check(ParseTableLink("fedelex://tavolo" + q + "//", out) == ParseError::BadScheme,
		 "two '/' at the end must be BadScheme, never silently kept inside the last value");
	// a '/' in the middle of a '?'-less route is not a slash before '?'
	check(ParseTableLink("fedelex://tavolo/x/?" + std::string(kCanonicalQuery), out) == ParseError::BadScheme,
		 "tavolo/x/ must be BadScheme");
}

void test_slash_tolerance_does_not_touch_the_values() {
	// '/' travels percent-encoded inside a value (encodeURIComponent), so the
	// trailing-'/' rule must not eat the end of a value that merely ENDS
	// with an encoded slash.
	TableLink out;
	auto err = ParseTableLink("fedelex://tavolo/?host=h&port=1&pass=a%2Fb&nome=n%2F", out);
	check(err == ParseError::Ok, "encoded slashes inside values must parse");
	check(out.pass == L"a/b" && out.nome == L"n/", "%2F must stay a '/' inside the value, not be stripped");
}

void test_other_errors_are_unchanged_on_a_variant() {
	TableLink out;
	check(ParseTableLink("FEDELEX://tavolo/?host=h&port=1&pass=p", out) == ParseError::MissingField,
		 "a variant missing nome must still be MissingField");
	check(ParseTableLink("fedelex://tavolo/?host=h&port=99999&pass=p&nome=n", out) == ParseError::BadPort,
		 "a variant with a bad port must still be BadPort");
	check(ParseTableLink("fedelex://tavolo/?host=&port=1&pass=p&nome=n/", out) == ParseError::EmptyHost,
		 "a variant with an empty host must still be EmptyHost");
}

// The log line for a discarded link (gframe/duelclient.cpp) is the SHAPE of
// what arrived, never its values: the password is in the link.
bool contains(const std::string& haystack, const char* needle) {
	return haystack.find(needle) != std::string::npos;
}

void test_shape_of_a_link_has_no_values() {
	const std::string link = "fedelex://tavolo/?host=duelli.example.org&port=57911&pass=Hunter2xyz&nome=NomeSegreto";
	const std::string shape = DescribeShape(link);
	check(shape == "schema=fedelex route=tavolo/ chiavi=host,port,pass,nome altre=0 lunghezza=" + std::to_string(link.size()),
		 "the shape of a Windows-style link: scheme, route, keys present, length");
	check(!contains(shape, "Hunter2xyz") && !contains(shape, "NomeSegreto") && !contains(shape, "duelli.example.org") &&
		  !contains(shape, "57911"),
		 "the shape must not contain any value (password, name, host, port)");
}

void test_shape_never_leaks_a_password_whatever_the_form() {
	// Every malformed form where the password could land somewhere the
	// shape would print: no scheme, another scheme, no '?', the password
	// as a key, a piece with no '=', percent-encoded, very long.
	const char* const links[] = {
		"host=h&port=1&pass=Hunter2xyz&nome=n",
		"Hunter2xyz",
		"Hunter2xyz://tavolo?host=h&pass=Hunter2xyz",
		"fedelex://tavolo&pass=Hunter2xyz",
		"fedelex://pass=Hunter2xyz",
		"fedelex://tavolo#pass=Hunter2xyz",
		"fedelex://tavolo?Hunter2xyz=1&pass=Hunter2xyz",
		"fedelex://tavolo?host=h&Hunter2xyz&nome=n",
		"fedelex://tavolo?host=h&port=1&pass=Hunter%32xyz&nome=n",
		"fedelex://tavolo/Hunter2xyz/Hunter2xyz/Hunter2xyz/Hunter2xyz?pass=Hunter2xyz",
		"fedelex://tavolo?pass=Hunter2xyz&pass=Hunter2xyz&pass=Hunter2xyz",
		"fedelex://tavolo?host=h&port=1&pass=Hunter2xyz&nome=n&x=Hunter2xyz&y=Hunter2xyz",
		"fedelex://Hunter2xyz?pass=Hunter2xyz",
		"Hunter2xyz://Hunter2xyz?host=h",
	};
	for(const char* link : links) {
		const std::string shape = DescribeShape(link);
		check(!contains(shape, "Hunter") && !contains(shape, "xyz"), link);
	}
}

void test_shape_of_unreadable_forms() {
	check(DescribeShape("not-a-link-at-all") == "schema=(assente) route=(nessun ?) chiavi=- altre=0 lunghezza=17",
		 "no '://' at all: the whole text is neither scheme nor route");
	check(DescribeShape("http://tavolo?a=1").find("schema=altro") == 0, "a scheme that is not fedelex is only named altro");
	check(DescribeShape("FEDELEX://x?host=1").find("schema=FEDELEX ") == 0, "the scheme of ours is shown as received, case included");
	check(DescribeShape("fedelex://tavolo/?host=1&Host=2&port=3&zz=4&=5&q").find("chiavi=host,port altre=4 ") != std::string::npos,
		 "only the four known keys are named, the others are counted (case differences show up as 'altre')");
	check(DescribeShape("fedelex://altro?host=1").find("route=(illeggibile, 5 caratteri)") != std::string::npos,
		 "a route that does not start with tavolo is not echoed, only its length");
	check(DescribeShape("fedelex://tavolo?").find("route=tavolo chiavi=- altre=0") != std::string::npos, "an empty query has no keys");
	std::string long_route(100, 'a');
	check(DescribeShape("fedelex://" + long_route + "?host=1").find("route=(illeggibile, 100 caratteri)") != std::string::npos,
		 "a long route is not echoed");
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
	test_harmless_os_variants_give_the_canonical_fields();
	test_variants_that_change_the_meaning_stay_bad_scheme();
	test_slash_tolerance_does_not_touch_the_values();
	test_other_errors_are_unchanged_on_a_variant();
	test_shape_of_a_link_has_no_values();
	test_shape_never_leaks_a_password_whatever_the_form();
	test_shape_of_unreadable_forms();

	std::printf("deep_link_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
