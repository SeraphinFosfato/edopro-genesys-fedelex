#include "title_state.h"

#include <cstdio>
// Pure platform-detection macros only (EDOPRO_WINDOWS) — no irrlicht, no
// curl, nothing that would break this module's zero-gframe-dependency
// contract. Needed for TimegmPortable below, same reason banlist_updater.cpp
// pulls it in.
#include "compiler_features.h"

namespace ygo::title {

namespace {

std::time_t TimegmPortable(std::tm& tm) {
#if EDOPRO_WINDOWS
	return _mkgmtime(&tm);
#else
	return timegm(&tm);
#endif
}

}

bool TitleSupersedes(const StoredTitle& title, const StoredBlock& block) {
	if(!title.present)
		return false;
	if(block.kind == StoredBlock::Kind::None)
		return true;
	return title.issued_at > block.at;
}

bool IsExpired(const std::string& expires_at, std::time_t now) {
	if(expires_at.empty())
		return false;
	// Same shape and same parser as BanlistUpdater::ActiveExpired
	// (banlist_updater.cpp): "%d-%d-%dT%d:%d:%d" matches the date/time
	// fields and stops there, so it works whether or not the producer
	// appends fractional seconds/"Z" — both title's and the banlist's
	// timestamps are ISO-8601 UTC from the same kind of source
	// (Date.toISOString() on the bot side).
	std::tm tm{};
	if(std::sscanf(expires_at.c_str(), "%d-%d-%dT%d:%d:%d",
				  &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return false; // unparseable: never punish the player for our own parsing gap
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	const std::time_t expires = TimegmPortable(tm);
	if(expires == static_cast<std::time_t>(-1))
		return false;
	return now > expires;
}

AccessState ComputeAccess(const StoredTitle& title, const StoredBlock& block, std::time_t now) {
	if(!title.present)
		return AccessState::NoTitle;
	if(!TitleSupersedes(title, block))
		return block.kind == StoredBlock::Kind::Revocation ? AccessState::Revoked : AccessState::Suspended;
	if(IsExpired(title.expires_at, now))
		return AccessState::TitleExpired;
	return AccessState::Active;
}

bool UserRefAccepted(bool has_bound_user_ref, const std::string& bound_user_ref,
					 const std::string& incoming_user_ref) {
	if(!has_bound_user_ref)
		return true;
	return incoming_user_ref == bound_user_ref;
}

// D217 — see the header for why these say the state and never the cause.
// Each one still points somewhere specific, because "access denied" with no
// next step is its own kind of dead end: the difference is that the
// destination is a place that KNOWS the cause, not a cause this client
// guessed.
std::wstring NotificationText(AccessState state) {
	switch(state) {
	case AccessState::Revoked:
		// Revocation is the one case the client may still route directly:
		// this state is only ever lifted by a human decision, so naming a
		// judge is a fact about the mechanism, not a guess about the reason.
		return L"Your access to the custom point list has been revoked. Contact a judge for details.";
	case AccessState::Suspended:
		return L"Your access to the custom point list is suspended. "
			   L"Open the bot to see why and what to do.";
	case AccessState::TitleExpired:
		// Not a sanction at all: the title simply ran out. Nothing to
		// explain, so nothing to ask anyone.
		return L"Your access to the custom point list could not be renewed. Reconnect to the bot to refresh it.";
	case AccessState::Active:
	case AccessState::NoTitle:
	default:
		return L"";
	}
}

}
