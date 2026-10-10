#include "deep_link.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "bufferio.h"

namespace ygo::deep_link {

namespace {

// FASE 75d: matched without regard to case, then an optional single '/',
// then '?' — see ParseTableLink(). On Windows the link reached the game but
// was discarded as BadScheme (2026-10-10, build 16), because the exact text
// "fedelex://tavolo?" is not what the OS / browser always hands over.
constexpr epro::stringview kHead = "fedelex://tavolo"sv;
constexpr epro::stringview kRoute = "tavolo"sv; // the tail of kHead, for DescribeShape()

// gframe/network.h: CTOS_JoinGame::pass[20] / CTOS_PlayerInfo::name[20] are
// uint16_t buffers that BufferIO::EncodeUTF16 null-terminates — the usable
// content is one code unit shorter than the buffer (see this file's header
// comment and gframe/bufferio.h's EncodeUTF16).
constexpr size_t kMaxFieldCodeUnits = 19;

int HexDigit(char c) {
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// RFC 3986 percent-decoding only — a literal '+' is never turned into a
// space (see the header comment on why guessing x-www-form-urlencoded
// semantics would be the wrong kind of leniency for a password field). A
// malformed escape (a trailing '%', or non-hex digits after it) is passed
// through verbatim rather than rejecting the whole link over one stray
// byte that a stricter parser further down (BadPort, length checks) will
// catch anyway if it actually corrupts a required field.
std::string PercentDecode(epro::stringview input) {
	std::string out;
	out.reserve(input.size());
	for(size_t i = 0; i < input.size(); ++i) {
		if(input[i] == '%' && i + 2 < input.size()) {
			int hi = HexDigit(input[i + 1]);
			int lo = HexDigit(input[i + 2]);
			if(hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(input[i]);
	}
	return out;
}

std::unordered_map<std::string, std::string> ParseQuery(epro::stringview query) {
	std::unordered_map<std::string, std::string> fields;
	size_t pos = 0;
	while(pos <= query.size()) {
		auto amp = query.find('&', pos);
		auto piece = (amp == epro::stringview::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
		auto eq = piece.find('=');
		if(eq != epro::stringview::npos) {
			auto key = PercentDecode(piece.substr(0, eq));
			auto value = PercentDecode(piece.substr(eq + 1));
			fields.emplace(std::move(key), std::move(value));
		}
		if(amp == epro::stringview::npos)
			break;
		pos = amp + 1;
	}
	return fields;
}

// std::stoi/strtoul would also accept "+80", leading whitespace, or stop
// silently at the first non-digit — none of which should be treated as a
// valid port in a link nobody is meant to type by hand. Every character
// must be an ASCII digit, and the whole field (1-5 digits, no leading '+')
// must fit in uint16_t's range.
bool ParsePort(const std::string& text, uint16_t& out) {
	if(text.empty() || text.size() > 5)
		return false;
	unsigned long value = 0;
	for(char c : text) {
		if(c < '0' || c > '9')
			return false;
		value = value * 10 + static_cast<unsigned long>(c - '0');
		if(value > 0xffffu)
			return false;
	}
	if(value == 0)
		return false; // port 0 is never a real listening socket
	out = static_cast<uint16_t>(value);
	return true;
}

}

const char* ToString(ParseError error) {
	switch(error) {
	case ParseError::Ok: return "Ok";
	case ParseError::BadScheme: return "BadScheme";
	case ParseError::MissingField: return "MissingField";
	case ParseError::EmptyHost: return "EmptyHost";
	case ParseError::BadPort: return "BadPort";
	case ParseError::PasswordTooLong: return "PasswordTooLong";
	case ParseError::NameTooLong: return "NameTooLong";
	}
	return "Unknown";
}

// `lower` must already be lower case.
bool EqualsIgnoreCase(epro::stringview text, epro::stringview lower) {
	if(text.size() != lower.size())
		return false;
	for(size_t i = 0; i < text.size(); ++i) {
		if(std::tolower(static_cast<unsigned char>(text[i])) != lower[i])
			return false;
	}
	return true;
}

bool IsRouteChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.' || c == '-' || c == '_';
}

std::string DescribeShape(const std::string& uri) {
	// Nothing taken from after the first '?' is ever copied into the result
	// except the four key NAMES this module knows (matched exactly, so they
	// are our own constants, not input). The scheme is echoed only if it IS
	// ours, the route only if it starts with "tavolo" and is made of
	// harmless characters.
	epro::stringview view(uri);
	std::string scheme = "(assente)";
	std::string route = "(nessun ?)";
	epro::stringview query;
	const auto sep = view.find("://"sv);
	if(sep != epro::stringview::npos) {
		const auto name = view.substr(0, sep);
		scheme = EqualsIgnoreCase(name, "fedelex"sv) ? std::string(name) : "altro";
		const auto rest = view.substr(sep + 3);
		const auto q = rest.find('?');
		if(q != epro::stringview::npos) {
			const auto path = rest.substr(0, q);
			bool printable = path.size() <= 24 && EqualsIgnoreCase(path.substr(0, kRoute.size()), kRoute);
			for(char c : path)
				printable = printable && IsRouteChar(c);
			route = printable ? std::string(path) : "(illeggibile, " + std::to_string(path.size()) + " caratteri)";
			query = rest.substr(q + 1);
		}
	}
	std::string keys;
	size_t others = 0;
	size_t pos = 0;
	while(pos <= query.size()) {
		const auto amp = query.find('&', pos);
		const auto piece = (amp == epro::stringview::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
		const auto key = piece.substr(0, piece.find('='));
		bool is_known = false;
		for(const auto known : { "host"sv, "port"sv, "pass"sv, "nome"sv }) {
			if(key == known) {
				keys += (keys.empty() ? "" : ",") + std::string(known);
				is_known = true;
			}
		}
		if(!is_known && !piece.empty())
			++others;
		if(amp == epro::stringview::npos)
			break;
		pos = amp + 1;
	}
	return "schema=" + scheme + " route=" + route + " chiavi=" + (keys.empty() ? "-" : keys) +
		" altre=" + std::to_string(others) + " lunghezza=" + std::to_string(uri.size());
}

ParseError ParseTableLink(const std::string& uri, TableLink& out) {
	out = TableLink{};
	epro::stringview view(uri);
	// FASE 75d: ONE literal '/' at the very end is tolerated (values are
	// encodeURIComponent'ed, so a literal '/' cannot belong to one — it can
	// only have been added after the link was built). A second one would end
	// up inside the last value, so it is refused instead.
	if(!view.empty() && view.back() == '/') {
		view.remove_suffix(1);
		if(!view.empty() && view.back() == '/')
			return ParseError::BadScheme;
	}
	if(view.size() < kHead.size() || !EqualsIgnoreCase(view.substr(0, kHead.size()), kHead))
		return ParseError::BadScheme;
	view.remove_prefix(kHead.size());
	// ...and an optional single '/' between "tavolo" and '?' (the form
	// Windows and browsers give to a URL with a host and no path).
	if(!view.empty() && view.front() == '/')
		view.remove_prefix(1);
	if(view.empty() || view.front() != '?')
		return ParseError::BadScheme;
	auto fields = ParseQuery(view.substr(1));

	auto host_it = fields.find("host");
	auto port_it = fields.find("port");
	auto pass_it = fields.find("pass");
	auto nome_it = fields.find("nome");
	if(host_it == fields.end() || port_it == fields.end() ||
	   pass_it == fields.end() || nome_it == fields.end())
		return ParseError::MissingField;

	if(host_it->second.empty())
		return ParseError::EmptyHost;

	uint16_t port = 0;
	if(!ParsePort(port_it->second, port))
		return ParseError::BadPort;

	std::wstring pass = BufferIO::DecodeUTF8(pass_it->second);
	if(pass.size() > kMaxFieldCodeUnits)
		return ParseError::PasswordTooLong;

	std::wstring nome = BufferIO::DecodeUTF8(nome_it->second);
	if(nome.size() > kMaxFieldCodeUnits)
		return ParseError::NameTooLong;

	out.host = host_it->second;
	out.port = port;
	out.pass = std::move(pass);
	out.nome = std::move(nome);
	return ParseError::Ok;
}

}
