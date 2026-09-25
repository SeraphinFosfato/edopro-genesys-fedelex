#ifndef ROOM_LIST_NOTICE_H
#define ROOM_LIST_NOTICE_H

#include <cstdint>

namespace ygo {

// design/banlist-distribution.md, "Quando qualcun altro sostituisce la
// lista, si dice": whoever runs a room's server side looks up the hash a
// host sent among ITS OWN lists and, not finding it, swaps in one of its
// own without saying so (netserver.cpp, the `hash == 1` branch; Project
// Ignis's public servers swap in zero, i.e. no list at all). The client
// has to notice and say so — a silent "N/A" in a corner of the screen is
// not a warning.
//
// Pulled out as its own pure function, with no gframe dependency, so it can
// be exercised in the network-free test binary (tests/premake5.lua: "if a
// test ever needs irrlicht or curl to run, the thing it is testing is in
// the wrong file"). duelclient.cpp (STOC_JOIN_GAME) is only the glue that
// hands it the two hashes and shows PopupMessage when it returns true.
//
// `sent_hash` is the hash this client sent in CTOS_CREATE_GAME while
// hosting a room (DuelClient::hosted_lflist_hash); zero means "we did not
// just host", i.e. we sent no list to compare against, so no warning is
// possible.
inline bool ShouldWarnAboutListSubstitution(uint32_t sent_hash, uint32_t received_hash) {
	return sent_hash != 0 && sent_hash != received_hash;
}

}

#endif //ROOM_LIST_NOTICE_H
