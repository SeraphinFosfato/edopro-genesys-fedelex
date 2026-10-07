// Tests for the pure "what does the client show in a tournament room"
// decisions (FASE 75b, design/decisioni.md D252, design/server-duelli.md
// §13.5). No gframe, no irrlicht, no network — same standalone-binary
// discipline as deep_link_tests.cpp. Every test here either fixes
// isTournamentRoom == false and checks the result matches the exact
// pre-FASE-75b formula (cancello 1's "fuori dal torneo l'esito e' quello
// del codice di oggi"), or fixes it == true and checks D252's own rule.

#include <cstdio>
#include "tournament_mode.h"

// Called from banlist_tests.cpp's main(), same pattern as every other
// *_tests.cpp in this directory.
int RunTournamentModeTests();

using namespace ygo::tournament_mode;

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

// --- ChainAutoPasses (§13.5 point 1) ---------------------------------

void test_outside_tournament_auto_passes_on_empty_chain() {
	// Today's upstream behaviour: nothing to activate (count == 0) and no
	// button pressed -> respond -1 without ever showing a question.
	check(ChainAutoPasses(false, false, false, 0, 0, false, false, false),
		 "outside tournament, an empty non-forced chain window auto-passes, exactly like today");
}

void test_outside_tournament_does_not_auto_pass_when_something_is_there() {
	check(!ChainAutoPasses(false, false, false, 1, 1, false, false, false),
		 "outside tournament, a non-empty chain window with no button pressed must still ask (today's formula)");
}

void test_outside_tournament_always_chain_suppresses_the_autopass() {
	check(!ChainAutoPasses(false, false, false, 0, 0, false, true, false),
		 "outside tournament, 'always chain' pressed must stop the empty-chain autopass (today's formula)");
}

void test_outside_tournament_ignore_chain_forces_the_autopass() {
	check(ChainAutoPasses(false, false, false, 5, 5, true, false, false),
		 "outside tournament, 'ignore chain' pressed must autopass even with candidates available (today's formula)");
}

void test_outside_tournament_select_trigger_never_autopasses() {
	check(!ChainAutoPasses(false, false, true, 0, 0, true, false, false),
		 "outside tournament, a trigger-effect selection window never autopasses, even with every button pressed (today's formula)");
}

void test_outside_tournament_forced_chain_never_autopasses() {
	check(!ChainAutoPasses(false, true, false, 0, 0, true, false, false),
		 "outside tournament, a forced chain never autopasses via this path (today's formula; it resolves via AutoChainOrderApplies instead)");
}

void test_tournament_never_autopasses_on_empty_chain() {
	check(!ChainAutoPasses(true, false, false, 0, 0, false, false, false),
		 "D252 point 1: in tournament, an empty non-forced chain window must still ask -- the empty/non-empty difference is itself the leak");
}

void test_tournament_chain_buttons_have_no_effect() {
	check(!ChainAutoPasses(true, false, false, 0, 0, true, true, true),
		 "D252 point 1: in tournament, ignore/always/when-available pressed together still must not skip the question");
}

void test_tournament_select_trigger_still_never_autopasses() {
	check(!ChainAutoPasses(true, false, true, 3, 3, false, false, false),
		 "in tournament, a trigger-effect window behaves the same as any other: always asks");
}

void test_tournament_forced_chain_unaffected_by_this_function() {
	check(!ChainAutoPasses(true, true, false, 0, 0, false, false, false),
		 "a forced chain is untouched by point 1 in or out of tournament -- this path never applies to it");
}

// --- AutoChainOrderApplies (§13.5 point 5) ---------------------------

void test_outside_tournament_auto_chain_order_applies_when_set() {
	check(AutoChainOrderApplies(false, true, true),
		 "outside tournament, a forced chain with the auto-order setting on resolves without asking (today's behaviour)");
}

void test_outside_tournament_auto_chain_order_off_does_not_apply() {
	check(!AutoChainOrderApplies(false, true, false),
		 "outside tournament, a forced chain with the setting off must still ask for the order (today's behaviour)");
}

void test_outside_tournament_non_forced_chain_unaffected() {
	check(!AutoChainOrderApplies(false, false, true),
		 "the auto-order setting only ever applies to a FORCED chain, in or out of tournament");
}

void test_tournament_auto_chain_order_is_off_even_when_the_setting_is_on() {
	check(!AutoChainOrderApplies(true, true, true),
		 "D252 point 5: in tournament, auto chain order never applies to a forced chain -- the interface always opens, setting or not");
}

// --- ZoneMenuFlags (§13.5 point 2/3) ----------------------------------

namespace flags {
constexpr int kActivateAndSpsummon = 0x01 | 0x02; // arbitrary bits, stand-in for COMMAND_ACTIVATE|COMMAND_SPSUMMON
constexpr int kList = 0x40;                       // arbitrary bit, stand-in for COMMAND_LIST
}

void test_outside_tournament_zone_menu_is_unchanged() {
	check(ZoneMenuFlags(false, true, flags::kActivateAndSpsummon, flags::kList) == flags::kActivateAndSpsummon,
		 "outside tournament, a zone's menu is exactly whatever the caller already computed (today's behaviour)");
}

void test_outside_tournament_empty_pile_is_unchanged() {
	check(ZoneMenuFlags(false, false, 0, flags::kList) == 0,
		 "outside tournament, an empty pile offers nothing, same as today (0 in, 0 out)");
}

void test_tournament_nonempty_pile_offers_only_list() {
	check(ZoneMenuFlags(true, true, flags::kActivateAndSpsummon, flags::kList) == flags::kList,
		 "D252 point 2/3: in tournament, a non-empty pile offers ONLY 'Guarda' (COMMAND_LIST), never Activate/Special Summon at the zone level");
}

void test_tournament_nonempty_pile_with_nothing_activatable_still_offers_list() {
	// The zone must offer the identical "Guarda" menu whether or not it
	// actually contains something usable -- that symmetry is the whole
	// point: the zone itself must never signal "there's something here".
	check(ZoneMenuFlags(true, true, 0, flags::kList) == flags::kList,
		 "D252 point 2/3: in tournament, 'Guarda' is offered for a non-empty pile even when nothing in it is activatable, so the two cases look identical");
}

void test_tournament_empty_pile_offers_nothing() {
	check(ZoneMenuFlags(true, false, 0, flags::kList) == 0,
		 "in tournament, an empty pile still offers nothing at all -- there is no zone-level menu to speak of either way");
}

// --- EffectYNIsConcealed (§13.5 point 4) ------------------------------

void test_outside_tournament_effectyn_is_never_concealed() {
	check(!EffectYNIsConcealed(false, false), "outside tournament, MSG_SELECT_EFFECTYN keeps today's card-naming text");
	check(!EffectYNIsConcealed(false, true), "outside tournament, being mid-resolution changes nothing either -- it was never concealed to begin with");
}

void test_tournament_effectyn_is_concealed_outside_resolution() {
	check(EffectYNIsConcealed(true, false),
		 "D252 point 4: in tournament, an EFFECTYN prompt outside chain resolution must use the generic text and open the pause");
}

void test_tournament_effectyn_is_not_concealed_during_resolution() {
	check(!EffectYNIsConcealed(true, true),
		 "D252 point 4's own carve-out: 'durante la risoluzione... resta com'e' oggi' -- a sub-choice mid-resolution keeps naming the card");
}

// --- ReconnectShouldActivate (FASE 76b, §13.6 punto 5) ---------------

void test_outside_tournament_reconnect_never_activates() {
	check(!ReconnectShouldActivate(false, true),
		 "outside a tournament room, a connection loss mid-duel must keep doing exactly what it does today (cancello 'fuori dal torneo nulla cambia')");
}

void test_tournament_reconnect_activates_mid_duel() {
	check(ReconnectShouldActivate(true, true),
		 "in a tournament room, a connection loss while isInDuel opens the blocking overlay instead of ending the duel");
}

void test_tournament_reconnect_does_not_activate_outside_duel() {
	check(!ReconnectShouldActivate(true, false),
		 "a tournament room's lobby/pre-duel connection loss is not covered by the reconnect window (§13.6 punto 2 starts only 'dopo l'inizio del primo duello') -- today's cleanup still applies");
}

// --- ReconnectAttemptDue ----------------------------------------------

void test_reconnect_attempt_not_due_before_interval() {
	check(!ReconnectAttemptDue(1999, 2000),
		 "one millisecond short of the interval is not yet due");
}

void test_reconnect_attempt_due_exactly_at_interval() {
	check(ReconnectAttemptDue(2000, 2000),
		 "exactly at the interval, due (so a caller ticking every frame never waits a whole extra frame past the boundary)");
}

void test_reconnect_attempt_due_well_past_interval() {
	check(ReconnectAttemptDue(50000, 2000),
		 "a long gap since the last attempt (e.g. the main thread was itself stalled) is still simply 'due', never a missed/skipped state");
}

// --- ReconnectGivesUp ---------------------------------------------------

void test_reconnect_no_error_does_not_give_up() {
	check(!ReconnectGivesUp(false),
		 "no explicit JOINERROR yet -- keep retrying, not given up");
}

void test_reconnect_explicit_join_error_gives_up() {
	check(ReconnectGivesUp(true),
		 "any explicit JOINERROR on a reconnect attempt means the identical CTOS_JOIN_GAME was just rejected -- give up for good, don't loop");
}

// --- ReconnectBlocksInput -----------------------------------------------

void test_reconnect_inactive_does_not_block_input() {
	check(!ReconnectBlocksInput(false),
		 "not awaiting a reconnect -- input flows normally, same as any other moment outside this feature");
}

void test_reconnect_active_blocks_input() {
	check(ReconnectBlocksInput(true),
		 "design/server-duelli.md §13.6 punto 5: 'nessun altro comando accettato' while the overlay is up");
}

}

int RunTournamentModeTests() {
	test_outside_tournament_auto_passes_on_empty_chain();
	test_outside_tournament_does_not_auto_pass_when_something_is_there();
	test_outside_tournament_always_chain_suppresses_the_autopass();
	test_outside_tournament_ignore_chain_forces_the_autopass();
	test_outside_tournament_select_trigger_never_autopasses();
	test_outside_tournament_forced_chain_never_autopasses();
	test_tournament_never_autopasses_on_empty_chain();
	test_tournament_chain_buttons_have_no_effect();
	test_tournament_select_trigger_still_never_autopasses();
	test_tournament_forced_chain_unaffected_by_this_function();

	test_outside_tournament_auto_chain_order_applies_when_set();
	test_outside_tournament_auto_chain_order_off_does_not_apply();
	test_outside_tournament_non_forced_chain_unaffected();
	test_tournament_auto_chain_order_is_off_even_when_the_setting_is_on();

	test_outside_tournament_zone_menu_is_unchanged();
	test_outside_tournament_empty_pile_is_unchanged();
	test_tournament_nonempty_pile_offers_only_list();
	test_tournament_nonempty_pile_with_nothing_activatable_still_offers_list();
	test_tournament_empty_pile_offers_nothing();

	test_outside_tournament_effectyn_is_never_concealed();
	test_tournament_effectyn_is_concealed_outside_resolution();
	test_tournament_effectyn_is_not_concealed_during_resolution();

	test_outside_tournament_reconnect_never_activates();
	test_tournament_reconnect_activates_mid_duel();
	test_tournament_reconnect_does_not_activate_outside_duel();

	test_reconnect_attempt_not_due_before_interval();
	test_reconnect_attempt_due_exactly_at_interval();
	test_reconnect_attempt_due_well_past_interval();

	test_reconnect_no_error_does_not_give_up();
	test_reconnect_explicit_join_error_gives_up();

	test_reconnect_inactive_does_not_block_input();
	test_reconnect_active_blocks_input();

	std::printf("tournament_mode_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
