// Tests for the online gate, the local-connection filter and the grace
// period state machine (FASE 59 — PHASES.md, "Il blocco dell'online per
// client o core vecchio", design/blocco-online.md). No network, no window,
// no game — same standalone-binary discipline as game_data_ready_tests.cpp.

#include <cstdio>
#include "online_gate.h"
#include "local_connection.h"
#include "grace_period.h"

// Called from banlist_tests.cpp's main() so the whole suite stays one
// binary with one summary line, per tests/premake5.lua's single ConsoleApp
// target.
int RunOnlineGateTests();

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

// ---------------------------------------------------------------------------
// Cancello 2 — the predicate, every combination.

void test_no_reason_is_open() {
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/true, /*syncs_finished=*/true) == OnlineGateReason::Open,
		 "supported client, separate-core build, core IS from the repository: must be Open");
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/false, /*core_from_repository=*/false, /*syncs_finished=*/true) == OnlineGateReason::Open,
		 "supported client, STATIC build: must be Open regardless of core_from_repository");
}

void test_r1_closes() {
	check(EvaluateOnlineGate(/*client_supported=*/false, /*core_separate_build=*/true, /*core_from_repository=*/true, /*syncs_finished=*/true) == OnlineGateReason::R1BuildBelowThreshold,
		 "unsupported client must close with R1, even if the core would otherwise be fine");
	check(EvaluateOnlineGate(/*client_supported=*/false, /*core_separate_build=*/false, /*core_from_repository=*/false, /*syncs_finished=*/true) == OnlineGateReason::R1BuildBelowThreshold,
		 "unsupported client on a STATIC build: R1 is the only possible signal there, and it must still close");
}

void test_r2_closes_on_separate_core_build_only() {
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/false, /*syncs_finished=*/true) == OnlineGateReason::R2CoreNotFromRepository,
		 "supported client, separate-core build, core NOT from the repository (any of the three D240 cases): must close with R2");
}

void test_r2_never_closes_on_static_build() {
	// design/blocco-online.md §1 / FASE 59 "Windows, in questa fase": a
	// static build has no separate core to mismatch, so the predicate must
	// ignore core_from_repository entirely when core_separate_build is
	// false — this is the single property that lets Windows adopt a
	// separate core later (FASE 60) with this call site unchanged.
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/false, /*core_from_repository=*/true, /*syncs_finished=*/true) == OnlineGateReason::Open,
		 "static build, core_from_repository=true: Open (the expected value)");
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/false, /*core_from_repository=*/false, /*syncs_finished=*/true) == OnlineGateReason::Open,
		 "static build, core_from_repository=false: must STILL be Open — R2 cannot exist on a static build, so this input must be ignored, not read as a mismatch");
}

// FASE 59 — appendice del 2026-10-02, cancello 1. Walks the REAL startup
// order on a separate-core build (game.cpp: LoadCoreFromRepos() runs to
// completion, synchronously, every frame it is called at all — it returns
// immediately while GetUpdatingReposNumber()>0 and otherwise attempts the
// swap and sets core_loaded_from_repo before RefreshOnlineGate() ever reads
// it; there is no observable frame where syncs_finished is true and the
// swap is merely "not yet" attempted). So the real sequence collapses to
// two frames: at least one repository still syncing, then — once every
// repository's pass is done — the SAME call already attempted the swap and
// it succeeded. This must end Open throughout.
//
// Before the fix the predicate had no "syncs_finished" input at all and
// treated the first frame (core_from_repository=false, nothing has synced
// yet) exactly like a settled "not from the repository" — closing the
// gate on every single session, forever, because the latch never reopens
// (§4). Written BEFORE the fix and run against it: it showed FAIL on all
// four checks below, confirming the bug, before the predicate was
// corrected — per the brief's own rule that a test written after the fix
// proves nothing.
void test_startup_sequence_ends_open() {
	OnlineGateLatch latch;
	// t0: at least one repository is still syncing. core_from_repository is
	// necessarily false here (nothing has swapped in yet) — the predicate
	// must still answer Open, because syncs_finished is false.
	check(latch.Update(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/false, /*syncs_finished=*/false)) == OnlineGateReason::Open,
		 "startup sequence, t0 (syncing in progress): must be Open, not R2 — nothing has been decided yet");
	// t1: every repository's pass is now done and (same call) the swap
	// succeeded. The gate must end Open, and the latch (having never
	// closed at t0) must allow it.
	check(latch.Update(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/true, /*syncs_finished=*/true)) == OnlineGateReason::Open,
		 "startup sequence, t1 (syncs finished, swap succeeded): must be Open");
	check(latch.Current() == OnlineGateReason::Open, "end of startup sequence: the session latch must read Open, not latched closed from an earlier frame");
}

// FASE 59 — appendice, cancello 3. D240 still closes the gate: a repository
// that DID finish syncing and did NOT end up providing the core in use
// (sync failed, or swap failed, or nobody declares a core) must still
// close with R2. This is the case the fix must NOT remove.
void test_finished_but_not_synced_still_closes() {
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/false, /*syncs_finished=*/true) == OnlineGateReason::R2CoreNotFromRepository,
		 "D240: syncs finished and the core in use is still not the repository's — must close with R2, same as before the fix");
}

void test_unverified_manifest_never_closes_r1() {
	// "Manifesto irraggiungibile o non valido... non chiude mai la porta"
	// (design/blocco-online.md §2). This predicate has no "unverified"
	// input by design: a caller that never reached a verified manifest
	// simply never calls EvaluateOnlineGate with client_supported=false in
	// the first place (it has no opinion to pass), so client_supported
	// here always means "a VERIFIED manifest said so". This test pins that
	// contract at the boundary the predicate actually owns: the only way
	// to get R1BuildBelowThreshold is to explicitly claim client_supported
	// is false — there is no "absent" state that the function invents on
	// its own that could look like closing.
	check(EvaluateOnlineGate(/*client_supported=*/true, /*core_separate_build=*/true, /*core_from_repository=*/true, /*syncs_finished=*/true) != OnlineGateReason::R1BuildBelowThreshold,
		 "client_supported=true (the only representation of 'no verified opinion says otherwise') must never produce R1");
}

// ---------------------------------------------------------------------------
// Cancello 3 — monotonicity: once closed, a later Open evaluation must not
// reopen it.

void test_latch_is_monotonic() {
	OnlineGateLatch latch;
	check(latch.Update(OnlineGateReason::Open) == OnlineGateReason::Open, "first evaluation Open: latch reports Open");
	check(latch.Update(OnlineGateReason::R1BuildBelowThreshold) == OnlineGateReason::R1BuildBelowThreshold,
		 "a closing evaluation latches to that reason");
	check(latch.Update(OnlineGateReason::Open) == OnlineGateReason::R1BuildBelowThreshold,
		 "a LATER Open evaluation must NOT reopen the latch — it must keep reporting the reason it closed with");
	check(latch.Update(OnlineGateReason::R2CoreNotFromRepository) == OnlineGateReason::R1BuildBelowThreshold,
		 "a later evaluation with a DIFFERENT closing reason must not override the first one either — once latched, it stays latched to the first reason");
	check(latch.Current() == OnlineGateReason::R1BuildBelowThreshold, "Current() must agree with what Update() has been returning");
}

void test_latch_starts_open() {
	OnlineGateLatch latch;
	check(latch.Current() == OnlineGateReason::Open, "a fresh latch (start of session) must read Open before any evaluation at all");
}

// ---------------------------------------------------------------------------
// Cancello 2b — "chi si collega", not "dove si ascolta".

void test_ipv4_loopback_is_local() {
	const uint8_t loopback[4] = { 127, 0, 0, 1 };
	check(IsLocalCallerAddress(loopback, AddressFamily::IPv4), "127.0.0.1 must be recognized as the local caller");
}

void test_ipv6_loopback_is_local() {
	const uint8_t loopback[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
	check(IsLocalCallerAddress(loopback, AddressFamily::IPv6), "::1 must be recognized as the local caller");
}

void test_ipv4_mapped_ipv6_loopback_is_local() {
	const uint8_t mapped[16] = { 0,0,0,0, 0,0,0,0, 0,0, 0xff,0xff, 127,0,0,1 };
	check(IsLocalCallerAddress(mapped, AddressFamily::IPv6), "::ffff:127.0.0.1 must be recognized as the local caller");
}

void test_lan_address_is_not_local() {
	const uint8_t lan[4] = { 192, 168, 1, 42 };
	check(!IsLocalCallerAddress(lan, AddressFamily::IPv4), "a LAN address (192.168.1.42) must NOT be treated as the local caller");
}

void test_public_ipv4_is_not_local() {
	const uint8_t pub[4] = { 8, 8, 8, 8 };
	check(!IsLocalCallerAddress(pub, AddressFamily::IPv4), "a public IPv4 address must NOT be treated as the local caller");
}

void test_public_ipv6_is_not_local() {
	// 2001:4860:4860::8888 (a real public IPv6 address), chosen only to not
	// be a loopback or mapped-loopback form.
	const uint8_t pub[16] = { 0x20,0x01, 0x48,0x60, 0x48,0x60, 0,0, 0,0, 0,0, 0,0, 0x88,0x88 };
	check(!IsLocalCallerAddress(pub, AddressFamily::IPv6), "a public IPv6 address must NOT be treated as the local caller");
}

void test_lan_ipv6_is_not_local() {
	// fe80::1, a link-local IPv6 address — local to the SEGMENT, not to
	// this machine, and must not be confused with ::1.
	const uint8_t linklocal[16] = { 0xfe,0x80, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
	check(!IsLocalCallerAddress(linklocal, AddressFamily::IPv6), "a link-local IPv6 address (fe80::1) must NOT be treated as the local caller");
}

// ---------------------------------------------------------------------------
// Cancello 4 — the grace state machine.

void test_grace_gate_open_is_unaffected() {
	check(EvaluateGrace(/*gate_closed=*/false, /*room_started=*/true, /*elapsed=*/0) == GraceDecision::GateOpen,
		 "gate open: must be GateOpen regardless of room/elapsed state");
	check(EvaluateGrace(/*gate_closed=*/false, /*room_started=*/false, /*elapsed=*/999) == GraceDecision::GateOpen,
		 "gate open: must be GateOpen even with absurd elapsed/room values");
}

void test_grace_room_not_started_exits_immediately() {
	check(EvaluateGrace(/*gate_closed=*/true, /*room_started=*/false, /*elapsed=*/0) == GraceDecision::ExitRoomNotStarted,
		 "gate closed, room not started: must exit immediately, even at elapsed==0");
}

void test_grace_in_match_at_minute_zero_warns() {
	check(EvaluateGrace(/*gate_closed=*/true, /*room_started=*/true, /*elapsed=*/0) == GraceDecision::InGrace,
		 "gate closed, in match, elapsed==0 (\"in partita al minuto 0\"): must be InGrace, i.e. the warn-once case");
}

void test_grace_expires_at_sixty_minutes() {
	check(EvaluateGrace(/*gate_closed=*/true, /*room_started=*/true, /*elapsed=*/60) == GraceDecision::GraceExpired,
		 "60 minutes elapsed, still in match: must be GraceExpired (exit now)");
	check(EvaluateGrace(/*gate_closed=*/true, /*room_started=*/true, /*elapsed=*/59) == GraceDecision::InGrace,
		 "59 minutes elapsed: must still be InGrace, not expired yet");
}

void test_grace_side_deck_counts_as_in_match() {
	// design/blocco-online.md §6: side deck between two duels of the same
	// match is "ancora in partita" — the caller folds this into
	// room_started=true (this function takes no separate "is side deck"
	// input, by design: the two situations are handled identically, see
	// the header comment).
	check(EvaluateGrace(/*gate_closed=*/true, /*room_started=*/true, /*elapsed=*/30) == GraceDecision::InGrace,
		 "side deck (room_started=true, mid-grace): must be InGrace, exactly like an ongoing duel");
}

void test_warning_latch_fires_exactly_once() {
	GraceWarningLatch latch;
	check(latch.ShouldWarnNow(GraceDecision::InGrace), "first InGrace poll: must warn");
	check(!latch.ShouldWarnNow(GraceDecision::InGrace), "second InGrace poll in the same grace window: must NOT warn again");
	check(!latch.ShouldWarnNow(GraceDecision::InGrace), "a third poll: still must not warn — \"una sola volta\" means once, not once per minute");
}

void test_warning_latch_never_fires_outside_in_grace() {
	GraceWarningLatch latch;
	check(!latch.ShouldWarnNow(GraceDecision::GateOpen), "GateOpen must never produce a warning");
	check(!latch.ShouldWarnNow(GraceDecision::ExitRoomNotStarted), "ExitRoomNotStarted must never produce a non-blocking warning — that path exits immediately instead");
	check(!latch.ShouldWarnNow(GraceDecision::GraceExpired), "GraceExpired must never produce this warning either — that path disconnects instead");
}

}

int RunOnlineGateTests() {
	test_no_reason_is_open();
	test_r1_closes();
	test_r2_closes_on_separate_core_build_only();
	test_r2_never_closes_on_static_build();
	test_startup_sequence_ends_open();
	test_finished_but_not_synced_still_closes();
	test_unverified_manifest_never_closes_r1();
	test_latch_is_monotonic();
	test_latch_starts_open();
	test_ipv4_loopback_is_local();
	test_ipv6_loopback_is_local();
	test_ipv4_mapped_ipv6_loopback_is_local();
	test_lan_address_is_not_local();
	test_public_ipv4_is_not_local();
	test_public_ipv6_is_not_local();
	test_lan_ipv6_is_not_local();
	test_grace_gate_open_is_unaffected();
	test_grace_room_not_started_exits_immediately();
	test_grace_in_match_at_minute_zero_warns();
	test_grace_expires_at_sixty_minutes();
	test_grace_side_deck_counts_as_in_match();
	test_warning_latch_fires_exactly_once();
	test_warning_latch_never_fires_outside_in_grace();
	std::printf("online_gate_tests: %d checks, %d failures\n", checks, failures);
	return failures;
}
