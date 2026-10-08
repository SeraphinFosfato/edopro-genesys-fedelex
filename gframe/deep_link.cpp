#include "deep_link.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "bufferio.h"

namespace ygo::deep_link {

namespace {

constexpr epro::stringview kPrefix = "fedelex://tavolo?"sv;

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

ParseError ParseTableLink(const std::string& uri, TableLink& out) {
	out = TableLink{};
	epro::stringview view(uri);
	if(!starts_with(view, kPrefix))
		return ParseError::BadScheme;
	auto fields = ParseQuery(view.substr(kPrefix.size()));

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
