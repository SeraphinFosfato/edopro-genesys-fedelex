#include "tournament_mode.h"

namespace ygo::tournament_mode {

bool ChainAutoPasses(bool isTournamentRoom, bool chainForced, bool selectTrigger,
                      uint32_t count, uint32_t specount,
                      bool ignoreChain, bool alwaysChain, bool chainWhenAvail) {
	if(isTournamentRoom && !chainForced)
		return false;
	return !selectTrigger && !chainForced
		&& (ignoreChain || ((count == 0 || specount == 0) && !alwaysChain))
		&& (count == 0 || !chainWhenAvail);
}

bool AutoChainOrderApplies(bool isTournamentRoom, bool chainForced, bool autoChainOrderSetting) {
	if(isTournamentRoom && chainForced)
		return false;
	return autoChainOrderSetting && chainForced;
}

int ZoneMenuFlags(bool isTournamentRoom, bool pileNonEmpty, int flagsOutsideTournament, int commandListFlag) {
	if(isTournamentRoom && pileNonEmpty)
		return commandListFlag;
	return flagsOutsideTournament;
}

bool HoverClosesCmdMenu(bool isTournamentRoom, bool clickedCardOnField, bool hoveredIsClickedCard) {
	if(hoveredIsClickedCard)
		return false;
	return !(isTournamentRoom && !clickedCardOnField);
}

bool EffectYNIsConcealed(bool isTournamentRoom, bool inChainResolution) {
	return isTournamentRoom && !inChainResolution;
}

bool ReconnectShouldActivate(bool isTournamentRoom, bool isInDuel) {
	return isTournamentRoom && isInDuel;
}

bool ReconnectAttemptDue(uint32_t msSinceLastAttempt, uint32_t intervalMs) {
	return msSinceLastAttempt >= intervalMs;
}

bool ReconnectGivesUp(bool receivedExplicitJoinError) {
	return receivedExplicitJoinError;
}

bool ReconnectBlocksInput(bool isAwaitingReconnect) {
	return isAwaitingReconnect;
}

bool ZoneShownInFull(bool isTournamentRoom, bool isOwn, uint32_t location, bool deckZoneReceived) {
	if(!isTournamentRoom)
		return false;
	switch(location) {
	case 0x10: // LOCATION_GRAVE
	case 0x20: // LOCATION_REMOVED
		return true;
	case 0x40: // LOCATION_EXTRA
		return isOwn;
	case 0x01: // LOCATION_DECK
		return isOwn && deckZoneReceived;
	default:
		return false;
	}
}

std::vector<int> ZoneDisplayPlan(const std::vector<uint32_t>& zoneCodes,
                                  const std::vector<uint32_t>& validCodes) {
	std::vector<bool> used(validCodes.size(), false);
	std::vector<int> plan;
	plan.reserve(zoneCodes.size());
	for(const auto code : zoneCodes) {
		int found = -1;
		for(size_t i = 0; i < validCodes.size(); ++i) {
			if(!used[i] && validCodes[i] == code) {
				found = static_cast<int>(i);
				used[i] = true;
				break;
			}
		}
		plan.push_back(found);
	}
	return plan;
}

bool ZoneClickAccepted(bool isTournamentRoom, bool isValidCandidate) {
	return !isTournamentRoom || isValidCandidate;
}

bool RestartAllowed(const RestartContext& c) {
	return !(c.inDuel || c.inLobby || c.siding || c.roomListVisible || c.lanVisible
			 || c.createHostVisible || c.hostPrepareVisible);
}

}
