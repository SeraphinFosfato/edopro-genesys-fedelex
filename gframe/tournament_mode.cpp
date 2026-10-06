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

bool EffectYNIsConcealed(bool isTournamentRoom, bool inChainResolution) {
	return isTournamentRoom && !inChainResolution;
}

}
