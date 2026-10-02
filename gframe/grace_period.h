#ifndef GRACE_PERIOD_H
#define GRACE_PERIOD_H

namespace ygo {

// FASE 59 — design/blocco-online.md §6, "Chi E' in partita quando il
// cancello si chiude". Pure state machine, same discipline as
// online_gate.h: no globals, inputs as plain parameters, no irrlicht, no
// clock of its own — the caller supplies elapsed time from whatever
// monotonic clock it already owns (design/blocco-online.md is explicit:
// "misurati con un orologio monotono, non l'ora di sistema, che si puo'
// spostare" — this header does not pick the clock, it only consumes a
// duration from it).
//
// "In partita" (design/blocco-online.md §6) means: duel started, or side
// deck between two duels of the SAME match. A room not yet started has
// nothing to finish. `room_started` below is that single boolean — the
// caller folds "duel in progress OR side-deck" into it before calling,
// because distinguishing those two cases is not a further branch in this
// decision: both are treated identically ("ancora in partita").
enum class GraceDecision {
	// The gate is not closed: this machine has nothing to do. Never
	// confuse this with GraceExpired at elapsed==0 — they are opposite ends
	// of the same axis and both are reachable only through `gate_closed`.
	GateOpen,
	// Gate closed, room never started (no duel, no side deck yet): nothing
	// to finish, exit immediately. design/blocco-online.md §6: "Una stanza
	// non ancora iniziata non ha niente da finire: si esce subito."
	ExitRoomNotStarted,
	// Gate closed, in a match, still within the 60-minute grace window.
	// The call site is responsible for the "una sola volta" part (see
	// GraceWarningLatch below) — this function alone cannot know whether
	// it has already been called before for this closure, because it holds
	// no state of its own.
	InGrace,
	// Gate closed, in a match, grace window elapsed: exit now. This is the
	// ONLY case that forces an exit out of a match in progress. A match
	// that finishes naturally before 60 minutes is NOT a case this function
	// ever sees reached as "exit at match end" — that happens at
	// STOC_DUEL_END (duelclient.cpp) checking the gate directly, which
	// fires regardless of how much of the grace window is left. This
	// function's only job is the 60-minute ceiling for a match that is
	// still running.
	GraceExpired,
};

inline GraceDecision EvaluateGrace(bool gate_closed, bool room_started, int elapsed_minutes_since_gate_closed) {
	if(!gate_closed)
		return GraceDecision::GateOpen;
	if(!room_started)
		return GraceDecision::ExitRoomNotStarted;
	if(elapsed_minutes_since_gate_closed >= 60)
		return GraceDecision::GraceExpired;
	return GraceDecision::InGrace;
}

// "Un avviso non bloccante e una sola volta, con l'ora di scadenza" — the
// "una sola volta" half of InGrace, factored out exactly like
// OnlineGateLatch (online_gate.h) factors out "chiuso resta chiuso": a tiny
// stateful class the caller owns one instance of per room/session, not a
// condition re-derived from scratch at every poll.
class GraceWarningLatch {
public:
	// Returns true the FIRST time this is called while InGrace is in
	// effect, false every time after — regardless of how many times the
	// caller polls during the same grace window.
	bool ShouldWarnNow(GraceDecision decision) {
		if(decision != GraceDecision::InGrace)
			return false;
		if(already_warned)
			return false;
		already_warned = true;
		return true;
	}
private:
	bool already_warned = false;
};

}

#endif //GRACE_PERIOD_H
