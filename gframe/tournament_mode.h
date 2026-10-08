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

// FASE 76b, design/server-duelli.md §13.6 punto 5: once a tournament-room
// connection drops mid-duel (duel proper, side deck, rock-paper-scissors —
// every state the server's own reconnect window covers, §13.6 punto 2), the
// client opens a blocking overlay and tries to rejoin on its own instead of
// ending the duel the way a drop does today. The four functions below are
// every yes/no DECISION that flow needs; gframe/duelclient.cpp (the
// connection-lost handler, the per-frame retry tick in Game::MainLoop, the
// STOC_CATCHUP and JOINERROR handlers, and ClientField::OnEvent) only calls
// these and acts — same split as the rest of this file.

// Does a connection loss, right now, open the blocking overlay instead of
// today's "duel ended" cleanup? False outside a tournament room, and false
// for a loss that happens anywhere other than mid-duel/mid-match (the lobby,
// deck building outside a duel, a replay...) — those keep doing exactly what
// they do today. "isInDuel" is the same flag gframe/duelclient.cpp already
// uses to tell the two apart (DuelInfo::isInDuel stays true across side
// decking and rock-paper-scissors between games of a match, only going
// false at true DUEL_END or at today's disconnect cleanup).
bool ReconnectShouldActivate(bool isTournamentRoom, bool isInDuel);

// Is it time for gframe/duelclient.cpp's per-frame tick to fire another
// StartClient() attempt? A plain cadence gate on the caller's own clock —
// nothing here reads real time. `intervalMs` is an implementation choice
// (FASE 76b picks 2000 in duelclient.cpp), not a number D251 or the server
// decided: same footing as FASE 64's 1500ms startup probe (CLAUDE.md §6.6,
// "i valori che si decidono guardando il risultato... li decide chi
// implementa, con mano libera dentro il criterio").
bool ReconnectAttemptDue(uint32_t msSinceLastAttempt, uint32_t intervalMs);

// Does an explicit rejection of a reconnect ATTEMPT mean giving up for
// good, rather than waiting for the next tick to try again? Always true:
// every STOC_ERROR_MSG/JOINERROR the server can send (JERR_REFUSED/
// JERR_PASSWORD/JERR_UNABLE) means the exact same CTOS_JOIN_GAME — same
// password, same name, gframe/duelclient.cpp never varies it between
// attempts — was just rejected, and repeating an unchanged request against
// an unchanged table can only repeat the same answer. A named function
// (not an inlined "true" at the call site) so this policy lives in one
// place: if a future server version ever needs a transient-vs-final
// distinction among JoinError variants, only this function's body and its
// test change, not every call site.
bool ReconnectGivesUp(bool receivedExplicitJoinError);

// Should this input event (any keyboard or mouse event, whatever widget it
// would otherwise reach) be swallowed outright? True exactly while a
// tournament-room reconnect is in progress — design/server-duelli.md §13.6
// punto 5 is explicit that the overlay is blocking, not advisory ("NON
// chiudere il simulatore", "nessun altro comando accettato").
bool ReconnectBlocksInput(bool isAwaitingReconnect);

}

#endif //TOURNAMENT_MODE_H
