#ifndef POINTS_BUDGET_H
#define POINTS_BUDGET_H

#include <cstdint>
#include <limits>
#include <string>

namespace ygo {

// FASE 32 — design/banlist-distribution.md, "Il tetto di punti è un dato
// della lista, non del binario". The pass/fail decision behind
// DeckError::TOOMANYPOINTS in DeckManager::CheckDeckContent
// (deck_manager.cpp), pulled out as its own pure function with no gframe
// dependency — same reasoning as ShouldWarnAboutListSubstitution
// (room_list_notice.h): the real caller needs irrlicht/game state and has
// no place in the standalone test binary (tests/premake5.lua), but the
// decision itself needs none of that, so it lives here where both the real
// caller and the test share the exact same code.
//
// `points_budget <= 0` means "no budget" — the same "zero or absent" rule
// applies whether this is called with the list's own declared default
// (LFList::points_budget) or, since FASE 34, with the ROOM's resolved
// override (GenericDuel::room_points_budget) — the caller decides which,
// this function only ever sees a plain int and doesn't care where it came
// from. Either way a deck of any size passes when it's 0.
inline bool IsOverPointsBudget(int64_t total_points, int points_budget) {
	return points_budget > 0 && total_points > points_budget;
}


// Campo "tetto punti" dell'host. Vuoto = 0 (nessun tetto, scelta esplicita).
// Un numero troppo grande per un int NON diventa 0 (cioe' "nessun tetto"):
// viene riportato al massimo, cioe' un tetto che nessun mazzo supera ma che
// resta un tetto. Il campo e' EDITBOX_NUMERIC, quindi contiene solo cifre.
inline int ParseHostPointsBudget(const std::wstring& text) {
	int64_t value = 0;
	for(const wchar_t c : text) {
		if(c < L'0' || c > L'9')
			return 0;
		value = value * 10 + (c - L'0');
		if(value > std::numeric_limits<int>::max())
			value = std::numeric_limits<int>::max();
	}
	return static_cast<int>(value);
}

}

#endif //POINTS_BUDGET_H
