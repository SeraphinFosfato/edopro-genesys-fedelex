// Tests for the launcher's pure decision functions (FASE 64, cancello 2,
// design/launcher.md + design/decisioni.md D244). No network, no window, no
// process spawned, no filesystem — same standalone-binary discipline as
// update_tests.cpp. Every fact a real run would have measured (a hash, a
// stat() result, whether a process came up) is a plain bool or string
// passed in by the test; what is under test is only the decision.

#include <cstdio>
#include <string>
#include "launcher_logic.h"

// Called from banlist_tests.cpp's main() so the whole suite stays one
// binary with one summary line, per tests/premake5.lua's single ConsoleApp
// target.
int RunLauncherLogicTests();

using namespace ygo::launcher;

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

// --- Cancello 2 of launcher.md: "il ciclo launcher->simulatore->launcher
// e' impossibile per test" --------------------------------------------

void test_simulator_without_mark_relaunches_launcher() {
	check(SimulatorShouldRelaunchLauncher(/*has_provenance_mark=*/false),
		 "a simulator started without the provenance mark must relaunch the launcher");
}

void test_simulator_with_mark_does_not_relaunch() {
	check(!SimulatorShouldRelaunchLauncher(/*has_provenance_mark=*/true),
		 "a simulator started WITH the provenance mark (i.e. by the launcher itself) must not relaunch it");
}

void test_the_cycle_is_structurally_impossible_after_one_handoff() {
	// Model the whole sequence described in launcher.md §4/§8: the launcher
	// always spawns the simulator WITH the mark set. Whatever
	// SimulatorShouldRelaunchLauncher() answers on that spawn is the one and
	// only time this question gets asked for that run — there is no second
	// call feeding back into itself, because a simulator that relaunches the
	// launcher does not re-ask this function, it hands control back entirely.
	// This test fixes that contract: the mark the launcher sets is exactly
	// the value this function needs to see "do not relaunch".
	const bool mark_launcher_always_sets = true;
	check(!SimulatorShouldRelaunchLauncher(mark_launcher_always_sets),
		 "the mark the launcher always passes must be exactly the value that stops a relaunch, closing the loop by construction");
}

// --- Cancello 2: manifest anti-rollback, launcher's own state ---------

void test_manifest_higher_than_stored_is_accept() {
	check(DecideManifestAcceptance(5, 3) == ManifestAcceptance::Accept,
		 "a manifest version higher than stored must be Accept");
}

void test_manifest_equal_to_stored_is_already_current() {
	check(DecideManifestAcceptance(5, 5) == ManifestAcceptance::AlreadyCurrent,
		 "a manifest version equal to stored must be AlreadyCurrent");
}

void test_manifest_lower_than_stored_is_reject_rollback() {
	check(DecideManifestAcceptance(2, 5) == ManifestAcceptance::RejectRollback,
		 "a manifest version lower than stored must be RejectRollback");
}

void test_first_run_with_no_stored_state_accepts() {
	check(DecideManifestAcceptance(1, /*stored_version=*/0) == ManifestAcceptance::Accept,
		 "a first run (no launcher-state.json yet, stored_version == 0) must accept any real manifest version");
}

// --- Cancello 2: replace-or-keep a single managed file -----------------

void test_matching_hash_is_keep() {
	const std::string hash(64, 'a');
	check(DecideReplace(hash, hash) == ReplaceDecision::Keep,
		 "an installed file whose hash already matches the manifest must be Keep");
}

void test_mismatched_hash_is_replace() {
	check(DecideReplace(std::string(64, 'a'), std::string(64, 'b')) == ReplaceDecision::Replace,
		 "an installed file whose hash differs from the manifest must be Replace");
}

void test_missing_installed_file_is_replace() {
	check(DecideReplace("", std::string(64, 'a')) == ReplaceDecision::Replace,
		 "an empty installed_sha256 (file missing or unreadable) must be Replace, never Keep on a question mark");
}

// --- Cancello 2 / cancello 4 of PHASES.md: verified swap ---------------

void test_swap_all_checks_pass_installs_new() {
	check(DecideSwap(/*hash_ok=*/true, /*exec_bit_ok=*/true, /*launches=*/true) == SwapOutcome::InstallNew,
		 "every check passing must install the new file");
}

void test_swap_bad_hash_rolls_back_without_touching_exec_bit() {
	check(DecideSwap(/*hash_ok=*/false, /*exec_bit_ok=*/true, /*launches=*/true) == SwapOutcome::RollbackToOld,
		 "a download whose hash does not match the manifest must roll back, regardless of the other two facts");
}

void test_swap_missing_exec_bit_asks_caller_to_fix_it() {
	check(DecideSwap(/*hash_ok=*/true, /*exec_bit_ok=*/false, /*launches=*/false) == SwapOutcome::FixExecutableBit,
		 "a verified download whose executable bit did not stick must ask the caller to chmod again, not roll back immediately");
}

void test_swap_verified_but_wont_launch_rolls_back() {
	check(DecideSwap(/*hash_ok=*/true, /*exec_bit_ok=*/true, /*launches=*/false) == SwapOutcome::RollbackToOld,
		 "a verified, executable file that still refuses to launch must roll back to .old (launcher.md cancello 5, \"un eseguibile senza l'eseguibile assente -> torna alla copia precedente\")");
}

}

int RunLauncherLogicTests() {
	test_simulator_without_mark_relaunches_launcher();
	test_simulator_with_mark_does_not_relaunch();
	test_the_cycle_is_structurally_impossible_after_one_handoff();
	test_manifest_higher_than_stored_is_accept();
	test_manifest_equal_to_stored_is_already_current();
	test_manifest_lower_than_stored_is_reject_rollback();
	test_first_run_with_no_stored_state_accepts();
	test_matching_hash_is_keep();
	test_mismatched_hash_is_replace();
	test_missing_installed_file_is_replace();
	test_swap_all_checks_pass_installs_new();
	test_swap_bad_hash_rolls_back_without_touching_exec_bit();
	test_swap_missing_exec_bit_asks_caller_to_fix_it();
	test_swap_verified_but_wont_launch_rolls_back();

	std::printf("launcher_logic_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
