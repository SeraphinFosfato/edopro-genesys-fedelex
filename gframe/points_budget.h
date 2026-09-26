#ifndef POINTS_BUDGET_H
#define POINTS_BUDGET_H

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
// `points_budget <= 0` means "no budget" — LFList::points_budget defaults
// to 0 when neither the .conf file nor the signed artifact carries one
// (FoldLFListBudget in lflist_hash.h treats that the same way) — so a deck
// of any size passes.
inline bool IsOverPointsBudget(int total_points, int points_budget) {
	return points_budget > 0 && total_points > points_budget;
}

}

#endif //POINTS_BUDGET_H
