#ifndef TITLE_STATE_H
#define TITLE_STATE_H

// The access decision, as pure logic over already-verified fields — no
// disk, no clock read, no network. Same reason as title_verify.h: testable
// standalone, and it is what the standalone test binary actually exercises
// for D103's supersede rule (design/fork-edopro/access-control.md §14).
//
// title_store.h wraps this with persistence and re-verification; nothing
// here trusts a byte on its own — every StoredTitle/StoredBlock this module
// is handed must already have come out of title_verify.h's VerifyAndParse*.

#include <ctime>
#include <string>

namespace ygo::title {

struct StoredTitle {
	bool present = false;
	std::string user_ref;
	std::string issued_at;
	std::string expires_at;
};

struct StoredBlock {
	enum class Kind {
		None,
		Revocation,
		Suspension,
	};
	Kind kind = Kind::None;
	std::string user_ref;
	std::string at; // revoked_at or suspended_at, whichever `kind` says
	// Revocation-only, display purposes (§8). Meaningless when kind is not
	// Revocation, and when has_banned_until is false the ban has no end date.
	std::string banned_until;
	bool has_banned_until = false;
};

enum class AccessState {
	Active,
	Revoked,
	Suspended,
	TitleExpired,
	NoTitle,
};

// D103's rule in isolation, exposed on its own because the design doc's own
// test list names it directly: "titolo con issued_at successivo... sblocca",
// "titolo con issued_at precedente... non sblocca". A tie (equal timestamps)
// does NOT supersede — fails closed, the same direction as everything else
// in this module when a case is not clearly decidable.
//
// Comparison is LEXICOGRAPHIC, not a parsed date compare, and that is
// correct rather than a shortcut: both timestamps come from the same
// producer in the same fixed format (JavaScript's Date.toISOString(), e.g.
// "2026-09-16T12:00:00.000Z" — always UTC, always zero-padded, always three
// fractional digits, always the literal "Z"). For that specific fixed
// width/zero-padded ISO-8601 shape, string order and chronological order
// coincide exactly (RFC 3339's own ordering property). This is deliberately
// NOT used for expiry below, which compares against a value this client
// itself produces (the wall clock) rather than two values from the same
// producer — see IsExpired.
bool TitleSupersedes(const StoredTitle& title, const StoredBlock& block);

// Whether `expires_at` (an ISO-8601 UTC timestamp in the format above) is in
// the past relative to `now`. An unparseable timestamp returns false — never
// expired — on purpose: `expires_at` only ever reaches this function inside
// a message whose signature has ALREADY verified (title_store.h), so a
// parse failure here means a bug in this client's own date handling, not a
// forged or tampered value; punishing the player for that would be exactly
// the kind of guess design/fork-edopro/access-control.md §3 forbids.
bool IsExpired(const std::string& expires_at, std::time_t now);

// The aggregate decision (§4's table): NoTitle if there has never been one;
// Revoked/Suspended if there is an unsuperseded block; TitleExpired if the
// title itself has run out; Active otherwise. Order matters — a real block
// is reported even when the title has also expired, because "you are
// banned" is the more useful thing to tell a player than "please reconnect".
AccessState ComputeAccess(const StoredTitle& title, const StoredBlock& block, std::time_t now);

// D104: whether a message carrying `incoming_user_ref` may be applied to a
// client currently in the state described by `has_bound_user_ref`/
// `bound_user_ref`. True when there is no binding yet (this message
// establishes it) or the incoming ref matches the existing one; false for
// any mismatch, however valid the message's own signature — an intercepted
// revocation is valid forever, for anyone who possesses it, so validity
// alone cannot be the gate. Pure decision only: title_store.h is what
// actually persists a first-time binding once this says yes.
bool UserRefAccepted(bool has_bound_user_ref, const std::string& bound_user_ref,
					 const std::string& incoming_user_ref);

}

#endif //TITLE_STATE_H
