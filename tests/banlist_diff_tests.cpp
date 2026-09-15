// Tests for the client-side banlist diff (FASE 4c).
// design/banlist-distribution.md, sections "Il diff", "La notifica", "Test"
// (the five "Diff e notifica" bullets are the cases below, one each).
//
// Same no-network, no-window scope as banlist_tests.cpp: ComputeDiff only
// ever compares two already-parsed Payloads in memory.

#include <cstdio>
#include "banlist_diff.h"
#include "banlist_verify.h"

// RunBanlistDiffTests() is called from banlist_tests.cpp's main() so the
// whole suite is still one binary with one summary line, per
// tests/premake5.lua's single ConsoleApp target.
int RunBanlistDiffTests();

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

banlist::Entry MakeEntry(uint32_t id, int limit, int points, const char* name = "Card",
						 bool has_reason = false, const char* reason = "") {
	banlist::Entry e;
	e.id = id;
	e.limit = limit;
	e.points = points;
	e.name = name;
	e.macro = "Magia";
	e.source = "custom";
	e.has_reason = has_reason;
	if(has_reason)
		e.reason = reason;
	return e;
}

const banlist::DiffEntry* FindById(const banlist::DiffGroup& group, uint32_t id) {
	for(const auto& entry : group.entries) {
		if(entry.id == id)
			return &entry;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------

void test_multi_version_skip_shows_net_not_sum() {
	// ComputeDiff only ever sees the last-seen active snapshot and the
	// newly staged one — whatever happened in any version the client
	// skipped over never exists as a separate delta to sum. A card that
	// went 20 -> 80 -> 30 across three releases, of which this client only
	// ever saw the first and the last, must show 20 -> 30 (net -- still a
	// rise), never 20 -> 80 -> 30 concatenated and never the naive sum of
	// per-hop deltas (+60 and -50 -> +10, which would coincidentally look
	// plausible here, so the real assertion is on the exact old/new values,
	// not just the direction).
	banlist::Payload active;
	active.entries.push_back(MakeEntry(1, 3, 20));
	banlist::Payload staged;
	staged.entries.push_back(MakeEntry(1, 3, 30));

	const auto diff = banlist::ComputeDiff(active, staged);
	const auto* entry = FindById(diff.more_expensive, 1);
	check(entry != nullptr, "a net points increase must appear in Più care");
	if(entry) {
		check(entry->old_points == 20, "old_points must be the last-seen active value, not an intermediate one");
		check(entry->new_points == 30, "new_points must be the staged value");
	}
	check(diff.cheaper.entries.empty(), "no Cheaper entry for a net increase");
	check(diff.new_entries.entries.empty() && diff.removed_entries.entries.empty(),
		 "an id present on both sides is never New or Removed");
}

void test_card_banned_at_unchanged_points_appears_in_diff() {
	// "Un cambio di limit è un cambiamento anche a punti invariati" —
	// points stay at 10 on both sides, only limit drops from 3 to 0.
	banlist::Payload active;
	active.entries.push_back(MakeEntry(2, 3, 10));
	banlist::Payload staged;
	staged.entries.push_back(MakeEntry(2, 0, 10));

	const auto diff = banlist::ComputeDiff(active, staged);
	const auto* entry = FindById(diff.more_expensive, 2);
	check(entry != nullptr, "a ban at unchanged points must still appear in the diff (as Più care)");
	if(entry) {
		check(entry->old_limit == 3 && entry->new_limit == 0, "old/new limit must reflect the ban");
		check(entry->old_points == entry->new_points, "points are unchanged in this case, by construction");
	}
}

void test_release_moving_more_entries_than_the_cap_is_truncated() {
	// "Lista troncata a ~15 voci con 'e altre N'".
	banlist::Payload active; // nothing active: every staged entry is New
	banlist::Payload staged;
	const size_t total = banlist::DIFF_GROUP_TRUNCATE + 7;
	for(size_t i = 0; i < total; ++i)
		staged.entries.push_back(MakeEntry(static_cast<uint32_t>(i + 1), 3, static_cast<int>(i)));

	const auto diff = banlist::ComputeDiff(active, staged);
	check(diff.new_entries.entries.size() == banlist::DIFF_GROUP_TRUNCATE,
		 "a group must never carry more than the truncation cap");
	check(diff.new_entries.omitted == total - banlist::DIFF_GROUP_TRUNCATE,
		 "omitted must be exactly the count left out, for the \"e altre N\" line");
}

void test_entry_without_reason_shows_only_numbers() {
	// "Se manca, si mostrano solo i numeri: il client non inventa mai un
	// perché." Two entries: one whose staged side carries a reason, one
	// that doesn't — has_reason must track the JSON exactly, never default
	// to true nor get invented for the reason-less one.
	banlist::Payload active;
	active.entries.push_back(MakeEntry(3, 3, 5));
	active.entries.push_back(MakeEntry(4, 3, 5));
	banlist::Payload staged;
	staged.entries.push_back(MakeEntry(3, 3, 40)); // no reason
	staged.entries.push_back(MakeEntry(4, 3, 40, "reasoned", true, "engine meta splashabile"));

	const auto diff = banlist::ComputeDiff(active, staged);
	const auto* without_reason = FindById(diff.more_expensive, 3);
	const auto* with_reason = FindById(diff.more_expensive, 4);
	check(without_reason != nullptr && !without_reason->has_reason,
		 "an entry whose staged JSON has no reason must come out with has_reason=false");
	check(with_reason != nullptr && with_reason->has_reason && with_reason->reason == "engine meta splashabile",
		 "an entry whose staged JSON carries a reason must come out with it verbatim");
}

void test_already_seen_notification_for_that_format_version_does_not_reappear() {
	// "Ha uno stato 'già vista' persistente per format_version, così non
	// ricompare a ogni lancio." The persistence itself is a GUI-layer
	// concern; what this module owns is the gate the persisted value feeds.
	check(banlist::ShouldNotifyForUpdate(7, 0), "a staged version never seen before must notify");
	check(!banlist::ShouldNotifyForUpdate(7, 7), "a staged version already marked seen must not notify again");
	check(banlist::ShouldNotifyForUpdate(8, 7), "a newer staged version than the last one seen must notify");
}

}

int RunBanlistDiffTests() {
	test_multi_version_skip_shows_net_not_sum();
	test_card_banned_at_unchanged_points_appears_in_diff();
	test_release_moving_more_entries_than_the_cap_is_truncated();
	test_entry_without_reason_shows_only_numbers();
	test_already_seen_notification_for_that_format_version_does_not_reappear();

	std::printf("banlist_diff_tests: %d checks, %d failures\n", checks, failures);
	return failures;
}
