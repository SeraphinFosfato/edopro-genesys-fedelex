#ifndef DEEP_LINK_H
#define DEEP_LINK_H

// FASE 75, design/decisioni.md D251, PHASES.md "il link del tavolo":
// parsing of the fedelex:// deep link a tournament's Telegram app hands the
// player, of the form
//   fedelex://tavolo?host=<host>&port=<port>&pass=<password>&nome=<name>
//
// Same discipline as gframe/launcher_logic.h: no gframe dependency beyond
// plain headers (bufferio.h is header-only and has no irrlicht/curl
// dependency of its own, same reasoning tests/premake5.lua already applies
// to launcher_logic.h), no I/O, no network — this module only turns bytes
// into a validated struct or a named reason it refused to. What happens
// with a successfully parsed link (resolving the host, checking it against
// ServerLobby::IsKnownHost, actually connecting) is gframe/duelclient.cpp's
// job (DuelClient::JoinFromDeepLink), same split as ParseTableLink here
// vs. JoinFromDiscord there.
//
// Design choices made here, where design/server-duelli.md §7/§13 and
// PHASES.md FASE 75 left the exact form open ("non e' risalita: come si
// analizza il link"):
//   - The scheme is matched literally as "fedelex://tavolo?" — there is
//     only one route today, so no general-purpose URI routing is built.
//   - Query values are percent-decoded (RFC 3986 %XX only; a literal '+'
//     stays a '+', never decoded to a space, since nothing on the
//     generating side — the bot/app building this link — promises
//     x-www-form-urlencoded '+' semantics, and guessing wrong would accept
//     a password with a silently wrong character).
//   - host/port/pass/nome are all required; any missing or malformed field
//     rejects the WHOLE link rather than partially acting on it.
//   - pass/nome are validated against the wire protocol's own limits
//     (CTOS_JoinGame::pass[20], CTOS_PlayerInfo::name[20], gframe/network.h)
//     BEFORE they ever reach BufferIO::EncodeUTF16 — that function silently
//     truncates on overflow (by design, for other callers), which would
//     mean sending a truncated password that happens to still parse as
//     well-formed, instead of refusing a link that cannot possibly be
//     honoured. Rejecting beats silently mangling a credential.

#include <cstdint>
#include <string>

namespace ygo::deep_link {

struct TableLink {
	std::string host;
	uint16_t port{ 0 };
	std::wstring pass;
	std::wstring nome;
};

enum class ParseError {
	Ok,
	BadScheme,     // does not start with "fedelex://tavolo?"
	MissingField,  // host, port, pass or nome absent from the query string
	EmptyHost,     // host present but empty after decoding
	BadPort,       // port is not a base-10 integer in [1, 65535]
	PasswordTooLong, // decoded pass would not fit CTOS_JoinGame::pass[20]
	NameTooLong,     // decoded nome would not fit CTOS_PlayerInfo::name[20]
};

const char* ToString(ParseError error);

// On ParseError::Ok, `out` is fully populated and every other ParseError
// case leaves `out` default-constructed (empty host, 0 port, empty
// pass/nome) — never a half-filled struct a careless caller could act on
// by accident.
ParseError ParseTableLink(const std::string& uri, TableLink& out);

}

#endif //DEEP_LINK_H
