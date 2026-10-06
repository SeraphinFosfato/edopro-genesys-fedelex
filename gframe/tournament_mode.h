#ifndef TOURNAMENT_MODE_H
#define TOURNAMENT_MODE_H

// FASE 75b, design/decisioni.md D252, design/server-duelli.md §13.5:
// "indicatori nascosti" in a tournament room (one joined through the
// fedelex:// link, FASE 75) must never let the player learn, before they
// have clicked a specific card themselves, that *something* is activatable
// right now. FASE 75 (gframe/duelclient.cpp's MSG_SELECT_CHAIN handling)
// hid the on-field outline only; this module holds the handful of yes/no
// DECISIONS the rest of FASE 75b wires up — never GUI state, never network
// I/O, so every branch can be asserted directly in tests/tournament_mode_tests.cpp
// without a running duel. Same discipline as gframe/deep_link.h and
// gframe/launcher_logic.h (tests/premake5.lua only links modules with no
// gframe/irrlicht/curl dependency): plain types in, plain types out.
//
// Every function here takes "isTournamentRoom" explicitly rather than
// reading a global, and every function is written so that
// isTournamentRoom == false reproduces today's upstream behaviour at the
// call site, unchanged. That is the literal content of cancello 1
// ("fuori dal torneo l'esito e' quello del codice di oggi") — it is a
// property of these functions' bodies, not just of their call sites.

#include <cstdint>

namespace ygo::tournament_mode {

// §13.5 point 1: does a MSG_SELECT_CHAIN window answer -1 by itself,
// without ever showing a question? Mirrors the exact predicate
// gframe/duelclient.cpp used before FASE 75b existed:
//   !selectTrigger && !chainForced
//   && (ignoreChain || ((count == 0 || specount == 0) && !alwaysChain))
//   && (count == 0 || !chainWhenAvail)
// In a tournament room, with chainForced == false, this is always false:
// D252 point 1 ("mai la risposta automatica") turns off ignoreChain/
// alwaysChain/chainWhenAvail and the count == 0 / specount == 0 / select
// trigger shortcuts alike — every one of those differences between an
// empty and a non-empty chain window is itself the indicator D252 exists
// to remove.
bool ChainAutoPasses(bool isTournamentRoom, bool chainForced, bool selectTrigger,
                      uint32_t count, uint32_t specount,
                      bool ignoreChain, bool alwaysChain, bool chainWhenAvail);

// §13.5 point 5: does the "automatic chain order" setting
// (tabSettings.chkAutoChainOrder) resolve a FORCED chain without asking
// which link goes first? Unchanged outside a tournament room. D252 point 5
// turns it off inside one ("l'ordine automatico ... e' spento: l'interfaccia
// si apre sempre") — chainForced chains stay fully visible, but the player
// always picks the order themselves.
bool AutoChainOrderApplies(bool isTournamentRoom, bool chainForced, bool autoChainOrderSetting);

// §13.5 point 2/3: what a zone-level click (the deck, graveyard, banished
// pile or extra deck as a WHOLE, not a single card already on the field)
// offers. `flagsOutsideTournament` is whatever the caller already computed
// for the non-tournament case (every card's cmdFlag OR'd together, plus
// COMMAND_LIST where upstream already adds it) — outside a tournament room
// this is the identity function, so the call site's existing behaviour is
// untouched. Inside one, a non-empty pile offers only COMMAND_LIST
// ("Guarda"): never COMMAND_ACTIVATE/COMMAND_SPSUMMON/COMMAND_OPERATION at
// the zone level, regardless of what is actually inside — the zone itself
// must not say "something in here is usable". COMMAND_LIST's numeric value
// is gframe/event_handler.h's concern, not this module's: the caller passes
// it in as `commandListFlag`.
int ZoneMenuFlags(bool isTournamentRoom, bool pileNonEmpty, int flagsOutsideTournament, int commandListFlag);

// §13.5 point 4: is a MSG_SELECT_EFFECTYN prompt concealed (generic text,
// no card name, no highlight; "Si'" opens the same pause as point 1 instead
// of answering immediately)? False outside a tournament room, and false
// "durante la risoluzione" (between MSG_CHAIN_SOLVING and MSG_CHAIN_SOLVED,
// tracked by ClientField::in_chain_resolution) — that sub-choice stays
// exactly as visible as it is today, D252's own carve-out.
bool EffectYNIsConcealed(bool isTournamentRoom, bool inChainResolution);

}

#endif //TOURNAMENT_MODE_H
