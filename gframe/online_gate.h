#ifndef ONLINE_GATE_H
#define ONLINE_GATE_H

#include "game_data_ready.h"

namespace ygo {

// FASE 59 — PHASES.md, "Il blocco dell'online per client o core vecchio"
// (design/blocco-online.md, D237/D239/D240). Same discipline as
// game_data_ready.h and points_budget.h: a header with no gframe
// dependency, inputs as plain parameters, no globals, linkable into the
// standalone test binary with no irrlicht, no curl, no network.
//
// Two closed reasons, and only two (design/blocco-online.md §2):
//
//  - R1 — build sotto soglia. The signed update manifest declares
//    `min_supported` and this build is below it
//    (update::IsClientSupported(), update_verify.h). Works identically on
//    both platforms, because on both of them a core below the floor is
//    still THIS build's own core — static (Windows, until FASE 60) or
//    separate (Linux, and Windows after FASE 60).
//  - R2 — il core in uso non e' quello del repository. Only meaningful on
//    a separate-core build: on a static build the core IS the build, so a
//    mismatch between "repository" and "in use" cannot exist, and the
//    caller must pass core_separate_build=false, which makes this
//    predicate ignore core_from_repository entirely (always false result
//    for R2) — see IsOnlineGateClosedByCoreMismatch below for the three
//    cases this covers on the platforms where it applies.
//
// A manifest that is unreachable or fails to verify closes NOTHING: the
// caller simply never has a `client_supported` opinion to pass in that
// case and keeps whatever this predicate last answered (see
// OnlineGateLatch below for why that matters more than it looks).
enum class OnlineGateReason {
	Open,
	R1BuildBelowThreshold,
	R2CoreNotFromRepository,
};

inline OnlineGateReason EvaluateOnlineGate(bool client_supported, bool core_separate_build, bool core_from_repository, bool game_data_ready) {
	if(!client_supported)
		return OnlineGateReason::R1BuildBelowThreshold;
	// On a static-core build core_from_repository is meaningless and must
	// never be allowed to close the gate: R2 is always false there
	// (design/blocco-online.md §1, "Windows, in questa fase").
	// FASE 59 — seconda appendice del 2026-10-02: R2 is a question about the
	// core the repository pass SETTLED on, not about the core in use at an
	// arbitrary instant — and "settled" is NOT just "no repository is
	// mid-sync" (that was the first appendice's fix, and it was still
	// wrong): a repo sync can finish while a core swap is deliberately
	// DEFERRED (Game::LoadCoreFromRepos only runs `if(!dInfo.isStarted)` —
	// a duel or a streamed replay in progress skips it on purpose), which
	// looks identical to a genuine "not from the repository" if you only
	// check the repo-sync count. `game_data_ready` must be exactly
	// `IsGameDataReady()` (game_data_ready.h, FASE 38) — core loaded, no
	// repo syncing, AND no swap still queued — passed whole, not
	// reconstructed from its parts here: that reconstruction (repo count
	// alone) is precisely the bug design/blocco-online.md §2 documents.
	// Before game data is in that final state this predicate has no
	// opinion yet, same as an unreachable manifest: it must NOT answer R2
	// just because the swap hasn't happened YET or is on purpose waiting
	// for a duel/replay to end. Without this, a replay watched while repos
	// finish syncing mid-stream closes the gate forever via the latch's own
	// monotonicity — a closure the swap, still queued, could never catch
	// up to.
	if(core_separate_build && game_data_ready && !core_from_repository)
		return OnlineGateReason::R2CoreNotFromRepository;
	return OnlineGateReason::Open;
}

// The three cases D240 folds into the single R2 signal, purely for what
// gets written to error.log (design/blocco-online.md §2, FASE 59 point 3)
// — this is NOT a fourth gate input, it only explains an R2 that the
// predicate above already decided. Game::LoadCoreFromRepos (game.cpp)
// knows which of these three happened; this enum just gives that reason a
// name shared with the test.
enum class CoreMismatchKind {
	// A repository declared a core and finished syncing, but swapping it in
	// failed — today a silent `continue` in LoadCoreFromRepos. Scripts from
	// the repository now run against the OLD core already on disk.
	SwapFailed,
	// A repository declares a core but has not finished syncing (or failed
	// to sync) by the time this is evaluated. Chosen to close by D240
	// ("online solo col core del repository" in one sentence, not two),
	// not because it is technically unsafe to proceed otherwise.
	RepoNotSynced,
	// No repository in the current configuration declares a core at all.
	// Doesn't happen with the configuration we ship, but a predicate that
	// silently assumed it can't happen is exactly the kind of thing that
	// bites later.
	NoRepoDeclaresCore,
};

// FASE 59 point 4: "Chiuso resta chiuso fino al riavvio: un controllo
// successivo non lo riapre." A manifest fetched 15 minutes later that would
// evaluate as Open (because min_supported was lowered, or because a later
// repo sync fixed R2) must NOT undo a closure already in effect — "un
// cancello che si apre e si chiude da solo insegna a non fidarsene"
// (design/blocco-online.md §4).
//
// Deliberately a tiny stateful class rather than a free function: the
// state it holds (which reason latched, if any) belongs to one running
// session and is exactly the kind of thing game_data_ready.h's own
// discipline says to keep OUT of a global and IN whatever the caller
// already owns — here, that's whatever object polls the manifest every 15
// minutes (not specified by this header, which only has to be right about
// the monotonicity rule itself).
class OnlineGateLatch {
public:
	// Folds in a freshly-evaluated reason and returns the reason now in
	// effect. Once latched to anything other than Open, every subsequent
	// call returns that same reason forever, regardless of what
	// `newly_evaluated` says — this is the entire monotonicity rule.
	OnlineGateReason Update(OnlineGateReason newly_evaluated) {
		if(latched == OnlineGateReason::Open)
			latched = newly_evaluated;
		return latched;
	}
	OnlineGateReason Current() const {
		return latched;
	}
private:
	OnlineGateReason latched = OnlineGateReason::Open;
};

}

#endif //ONLINE_GATE_H
