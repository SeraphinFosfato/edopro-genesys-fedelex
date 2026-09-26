#ifndef GAME_DATA_READY_H
#define GAME_DATA_READY_H

#include <cstddef>

namespace ygo {

// FASE 38 — PHASES.md, "Il client non gioca finche' i suoi dati non sono a
// posto". Two independently-observed faces of the same root cause (a duel
// could be started while game data was still incomplete, silently):
//
//  - Linux (ygoprodll): Game::LoadCoreFromRepos() (game.cpp) swaps in a
//    freshly-downloaded core, but the OLD core already on disk from a
//    previous install had been loaded and had already enabled the
//    duel-starting buttons at startup — before any repo had a chance to
//    finish syncing. A duel could be played with stale scripts already
//    updated on disk but not yet loaded into the running core.
//  - Windows (ygopro, static core): the core is linked in, so
//    `coreloaded` is true from the moment the binary starts, regardless of
//    whether any repository (scripts, pics, database) has finished
//    syncing yet. `mainGame->coreloaded` alone was therefore never a
//    correct gate on either platform: on Linux it lagged a live core swap,
//    on Windows it said nothing about repo state at all.
//
// This is the single predicate both call sites now share: "is this
// client's game data in its final state, right now". It takes its inputs
// as plain parameters and reads no globals, so it can be exercised here
// with no irrlicht — same reasoning as IsOverPointsBudget (points_budget.h)
// and ShouldWarnAboutListSubstitution (room_list_notice.h): the real
// callers (game.cpp, menu_handler.cpp, duelclient.cpp, server_lobby.cpp)
// need Game/RepoManager state that has no place in the standalone test
// binary, but the decision itself needs none of that.
//
// `core_loaded` — Game::coreloaded: an OCG core is currently linked in
// (statically, or dynamically loaded from disk).
// `updating_repos_count` — RepoManager::GetUpdatingReposNumber(): how many
// repositories (scripts/database/pics/strings) have not yet finished their
// clone-or-update pass, success or failure alike (RepoManager::SyncRepo
// sets `internal_ready = true` after the catch block, so a repo that
// FAILED to sync still counts as done here — this predicate must never be
// able to wedge itself open forever waiting on a repo that will never
// succeed; a failed repo just means play with whatever is already on
// disk, not "wait forever").
// `core_swap_pending` — on ygoprodll builds only, whether
// Game::cores_to_load still has an entry waiting to be swapped in
// (LoadCoreFromRepos hasn't run yet, or is itself blocked because a repo
// is still updating). Always false on the static ygopro build, where
// cores_to_load is never populated in the first place — the same
// predicate call site works unmodified in both `#ifdef` branches.
inline bool IsGameDataReady(bool core_loaded, std::size_t updating_repos_count, bool core_swap_pending) {
	return core_loaded && updating_repos_count == 0 && !core_swap_pending;
}

}

#endif //GAME_DATA_READY_H
