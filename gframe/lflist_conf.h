#ifndef LFLIST_CONF_H
#define LFLIST_CONF_H

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace ygo {

// Parsing rules for one line of a .conf lflist file. Shared between the
// real loader (DeckManager::LoadLFListSingle, deck_manager.cpp — which
// pulls in irrlicht/game.h and has no place in the standalone test binary,
// tests/premake5.lua) and the test that checks a list hashes the same
// whether it comes from a .conf file or a signed banlist.json
// (tests/banlist_tests.cpp, FASE 32 cancello 3). Two independent
// reimplementations of "how to read a .conf line" is exactly how that
// cancello would end up tested by assertion — "these two call sites share
// a function" — instead of by actually running the code each call site
// runs.

// "code limit [points]" — one banlist entry line. Returns false for
// anything that isn't one (blank, comment, `!`/`$` directive, malformed):
// callers treat that as "skip this line", not an error — the same
// tolerance LoadLFListSingle has always had for a malformed points column.
inline bool ParseLFListEntryLine(const std::string& line, uint32_t& code, int& limit, int& points) {
	std::istringstream iss(line);
	code = 0;
	limit = 3;
	points = 0;
	iss >> code >> limit;
	if(iss.fail() || code == 0)
		return false;
	if(!(iss >> points))
		points = 0; // points is optional
	return true;
}

inline constexpr std::string_view LFLIST_BUDGET_DIRECTIVE = "$points_budget";

// "$points_budget N" — the list-level counterpart to the per-entry line
// above (FASE 32, design/banlist-distribution.md "Il tetto di punti è un
// dato della lista"). Same $-prefixed directive shape as the existing
// "$whitelist" marker DeckManager::LoadLFListSingle already recognizes.
// Returns false, leaving `budget` untouched, for any line that isn't this
// directive or whose N is missing/non-positive: a malformed list-level
// directive means "no budget", the same as an absent one — not an error.
inline bool ParseLFListBudgetLine(const std::string& line, int& budget) {
	if(line.rfind(LFLIST_BUDGET_DIRECTIVE, 0) != 0)
		return false;
	std::istringstream iss(line.substr(LFLIST_BUDGET_DIRECTIVE.size()));
	int value = 0;
	if(!(iss >> value) || value <= 0)
		return false;
	budget = value;
	return true;
}

}

#endif //LFLIST_CONF_H
