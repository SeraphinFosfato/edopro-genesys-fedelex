// Tests for the "game data ready" gate (FASE 38 — PHASES.md, "Il client non
// gioca finche' i suoi dati non sono a posto").
//
// No network, no window, no game, same reasoning as banlist_tests.cpp: the
// decision itself (game_data_ready.h) has zero gframe dependency and links
// here unmodified; the real call sites (game.cpp, menu_handler.cpp,
// duelclient.cpp, server_lobby.cpp) need Game/RepoManager/irrlicht state
// that has no place in this binary, and are instead checked the way
// test_editor_never_calls_check_deck_content and
// test_host_info_layout_is_unchanged already check theirs in
// banlist_tests.cpp: as structural assertions scanning the real source.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <string>
#include "game_data_ready.h"

// Called from banlist_tests.cpp's main() so the whole suite stays one
// binary with one summary line, per tests/premake5.lua's single ConsoleApp
// target.
int RunGameDataReadyTests();

using namespace ygo;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
	++checks;
	if(!condition) {
		++failures;
		std::printf("  FAIL  %s\n", what);
	}
}

std::string ReadSourceFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if(!f) {
		std::printf("  SOURCE MISSING: %s (run the binary from the repository root)\n", path.c_str());
		return {};
	}
	return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// Cancello 1 — the predicate in its pure form, exercised on every
// combination of its three inputs.

void test_predicate_requires_all_three_conditions() {
	check(IsGameDataReady(true, 0, false), "core loaded, no repo updating, no swap pending: must be ready");

	check(!IsGameDataReady(false, 0, false), "core NOT loaded must never be ready, regardless of repos");
	check(!IsGameDataReady(true, 1, false), "one repo still updating must never be ready");
	check(!IsGameDataReady(true, 5, false), "several repos still updating must never be ready");
	check(!IsGameDataReady(true, 0, true), "a core swap still pending (cores_to_load non-empty) must never be ready");

	// Every way of NOT being ready at once must still not be ready.
	check(!IsGameDataReady(false, 3, true), "core not loaded, repos updating, AND swap pending: must not be ready");
	check(!IsGameDataReady(false, 0, true), "core not loaded and swap pending, no repos updating: must not be ready");
	check(!IsGameDataReady(false, 1, false), "core not loaded and a repo updating: must not be ready");
}

void test_core_swap_pending_is_irrelevant_on_the_static_build() {
	// Game::IsGameDataReady() (game.cpp) passes `core_swap_pending = false`
	// unconditionally on the static ygopro build, since cores_to_load is
	// never populated there — this is the property that makes the same
	// predicate call site correct in both #ifdef branches (see
	// game_data_ready.h). At this level that just means: with
	// core_swap_pending fixed to false, readiness depends only on the other
	// two inputs.
	check(IsGameDataReady(true, 0, false), "static build shape (swap pending always false): ready when core loaded and no repo updating");
	check(!IsGameDataReady(true, 2, false), "static build shape: still not ready while repos are updating");
}

// ---------------------------------------------------------------------------
// Cancello 2 — "il caso che non deve incastrarsi": a repository that FAILS
// to sync must still open the gate, not hold it closed forever.
//
// RepoManager::GetUpdatingReposNumber() (repo_manager.cpp) is just
// `available_repos.size()`, and a repo leaves available_repos only once
// RepoManager::GetReadyRepos() sees its `internal_ready` flag set — so the
// property this cancello asks for reduces to: RepoManager::CloneOrUpdateTask
// sets `_repo.internal_ready = true` unconditionally after the sync attempt,
// not only on the success path inside (or guarded by) the try block. That
// function needs libgit2 and real threads and has no standalone target here
// (same scope boundary as everywhere else in this suite), so it is checked
// structurally: read the real source, and assert the assignment sits after
// the catch block closes, never only inside the try. This is the same style
// as test_host_info_layout_is_unchanged in banlist_tests.cpp, and it is what
// was actually read and confirmed by hand while designing this gate
// (repo_manager.cpp:379-386, 2026-09-26): the assignment follows the closing
// brace of `catch(const std::exception& e)`, unconditionally.
void test_a_failed_repo_still_releases_the_gate() {
	const auto source = ReadSourceFile("gframe/repo_manager.cpp");
	if(source.empty()) {
		check(false, "a_failed_repo_still_releases_the_gate: gframe/repo_manager.cpp unreadable, cannot run");
		return;
	}
	const auto fn_pos = source.find("void RepoManager::CloneOrUpdateTask()");
	if(fn_pos == std::string::npos) {
		check(false, "a_failed_repo_still_releases_the_gate: RepoManager::CloneOrUpdateTask not found — has it been renamed?");
		return;
	}
	// Bound the scan to exactly this function's body: find its own opening
	// brace, then the matching closing one by tracking nesting depth. The
	// function itself references "RepoManager::FetchCb"/"RepoManager::
	// CheckoutCb" internally (as callback pointers), so simply searching
	// for the next "RepoManager::" substring would cut the body off early —
	// brace matching is the only bound that isn't fooled by that.
	const auto fn_brace_open = source.find('{', fn_pos);
	check(fn_brace_open != std::string::npos, "CloneOrUpdateTask must be followed by a brace-delimited body");
	if(fn_brace_open == std::string::npos)
		return;
	size_t fn_depth = 0;
	size_t fn_brace_close = std::string::npos;
	for(size_t i = fn_brace_open; i < source.size(); ++i) {
		if(source[i] == '{')
			++fn_depth;
		else if(source[i] == '}') {
			--fn_depth;
			if(fn_depth == 0) {
				fn_brace_close = i;
				break;
			}
		}
	}
	if(fn_brace_close == std::string::npos) {
		check(false, "CloneOrUpdateTask's closing brace must be found");
		return;
	}
	const auto body = source.substr(fn_pos, fn_brace_close - fn_pos);

	const auto catch_pos = body.find("catch(");
	check(catch_pos != std::string::npos, "CloneOrUpdateTask must have a catch block guarding the sync attempt");
	if(catch_pos == std::string::npos)
		return;

	// Find the catch block's own opening brace, then its matching closing
	// brace by tracking nesting depth — the body can contain other braces
	// (ErrorLog(...) calls, string literals with no braces here).
	const auto catch_brace_open = body.find('{', catch_pos);
	check(catch_brace_open != std::string::npos, "catch(...) must be followed by a brace-delimited block");
	if(catch_brace_open == std::string::npos)
		return;
	size_t depth = 0;
	size_t catch_brace_close = std::string::npos;
	for(size_t i = catch_brace_open; i < body.size(); ++i) {
		if(body[i] == '{')
			++depth;
		else if(body[i] == '}') {
			--depth;
			if(depth == 0) {
				catch_brace_close = i;
				break;
			}
		}
	}
	check(catch_brace_close != std::string::npos, "the catch block's closing brace must be found");
	if(catch_brace_close == std::string::npos)
		return;

	const auto ready_pos = body.find("internal_ready = true", catch_brace_close);
	check(ready_pos != std::string::npos,
		 "internal_ready = true must appear AFTER the catch block closes — a failed repo (exception caught) must still be marked ready, "
		 "or GetUpdatingReposNumber() would never reach 0 and IsGameDataReady() would stay closed forever");

	// And the mirror check: it must not appear only inside the try portion
	// (before the catch), which would mean the failure path skips it.
	const auto ready_in_try = body.find("internal_ready = true", fn_pos);
	check(ready_in_try == std::string::npos || ready_in_try >= catch_brace_close,
		 "internal_ready = true must not be set only on the success path inside the try block — the failure path must reach it too");
}

// ---------------------------------------------------------------------------
// Cancello 3 — no duel-starting button gate is left reading `coreloaded` on
// its own. Scans every gframe/*.cpp, not a fixed list, same reasoning as
// test_editor_never_calls_check_deck_content (banlist_tests.cpp): a future
// file nobody remembered to add here can't silently go unchecked.
//
// Deliberately narrow to `setEnabled(coreloaded)` / `setEnabled(mainGame->
// coreloaded)`: those are the button-gating call sites this FASE set out to
// fix. Other bare reads of `coreloaded` still exist on purpose and are out
// of scope — e.g. RefreshUICoreVersion()'s `if (coreloaded)` in game.cpp
// only decides whether to show the core's version STRING and starts no
// duel.
void test_no_button_gate_still_reads_bare_coreloaded() {
	int offending_matches = 0;
	std::string offending_detail;
	std::error_code walk_error;
	for(const auto& dirent : std::filesystem::directory_iterator("gframe", walk_error)) {
		if(dirent.path().extension() != ".cpp")
			continue;
		const auto path_str = dirent.path().string();
		const auto source = ReadSourceFile(path_str);
		if(source.empty())
			continue;
		size_t pos = 0;
		while((pos = source.find("setEnabled(", pos)) != std::string::npos) {
			const auto arg_start = pos + std::strlen("setEnabled(");
			const auto arg_end = source.find(')', arg_start);
			pos = arg_start;
			if(arg_end == std::string::npos)
				continue;
			const auto arg = source.substr(arg_start, arg_end - arg_start);
			// A bare gate on core state alone: the whole argument is just
			// `coreloaded` or `mainGame->coreloaded`, with no other term
			// (repo count, swap-pending) combined in. IsGameDataReady()
			// itself is exempt (it's the replacement, not an instance of
			// the problem), matched by name so the substring "coreloaded"
			// inside it (the parameter name in game_data_ready.h, unrelated
			// here) never confuses this scan.
			if(arg == "coreloaded" || arg == "mainGame->coreloaded") {
				++offending_matches;
				offending_detail = path_str + ": setEnabled(" + arg + ")";
			}
		}
	}
	check(!walk_error, "test_no_button_gate_still_reads_bare_coreloaded: could not list gframe/ (run the binary from the repository root)");
	check(offending_matches == 0,
		 ("no setEnabled(...) call may gate on bare coreloaded alone anymore — found: " + offending_detail).c_str());
}

// ---------------------------------------------------------------------------
// Cancello 4 — the deck editor stays ungated. FASE 38 must not be the thing
// that makes deck_con.cpp start checking IsGameDataReady() or coreloaded:
// "L'editor non blocca niente, ed e' voluto" (banlist_tests.cpp, cancello 5
// of FASE 34) holds in both directions now.
void test_editor_is_not_gated_by_game_data_ready() {
	const auto source = ReadSourceFile("gframe/deck_con.cpp");
	if(source.empty()) {
		check(false, "test_editor_is_not_gated_by_game_data_ready: gframe/deck_con.cpp unreadable, cannot run");
		return;
	}
	check(source.find("IsGameDataReady") == std::string::npos,
		 "the deck editor (deck_con.cpp) must never call IsGameDataReady() — the editor is not a duel-starting surface");
	check(source.find("coreloaded") == std::string::npos,
		 "the deck editor (deck_con.cpp) must never read coreloaded either — same rule, same reason");
}

}

int RunGameDataReadyTests() {
	test_predicate_requires_all_three_conditions();
	test_core_swap_pending_is_irrelevant_on_the_static_build();
	test_a_failed_repo_still_releases_the_gate();
	test_no_button_gate_still_reads_bare_coreloaded();
	test_editor_is_not_gated_by_game_data_ready();
	std::printf("game_data_ready_tests: %d checks, %d failures\n", checks, failures);
	return failures;
}
