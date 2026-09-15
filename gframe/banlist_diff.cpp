#include "banlist_diff.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace ygo::banlist {

namespace {

// Sort key for "entità del cambiamento, decrescente" (design doc, "Il
// diff"). The doc states explicitly that a limit change "conta più di
// qualunque swing di punti" — a ban outranks any points swing, however
// large. That is encoded literally here as a two-level key: whether limit
// changed at all comes first, then its magnitude, then the points swing —
// rather than folding both into one number, which would need an arbitrary
// weight and could be wrong for some future points range.
struct SortKey {
	bool limit_changed;
	int limit_magnitude;
	int points_magnitude;

	// true if this key sorts BEFORE other, for a descending ("biggest
	// change first") order.
	bool Before(const SortKey& other) const {
		if(limit_changed != other.limit_changed)
			return limit_changed > other.limit_changed;
		if(limit_magnitude != other.limit_magnitude)
			return limit_magnitude > other.limit_magnitude;
		return points_magnitude > other.points_magnitude;
	}
};

SortKey MakeSortKey(const DiffEntry& entry) {
	const int limit_delta = entry.new_limit - entry.old_limit;
	const int points_delta = entry.new_points - entry.old_points;
	return SortKey{ limit_delta != 0, std::abs(limit_delta), std::abs(points_delta) };
}

void SortAndTruncate(DiffGroup& group, size_t truncate_at) {
	std::sort(group.entries.begin(), group.entries.end(),
			 [](const DiffEntry& a, const DiffEntry& b) {
				 return MakeSortKey(a).Before(MakeSortKey(b));
			 });
	if(group.entries.size() > truncate_at) {
		group.omitted = group.entries.size() - truncate_at;
		group.entries.resize(truncate_at);
	} else {
		group.omitted = 0;
	}
}

}

Diff ComputeDiff(const Payload& active, const Payload& staged, size_t truncate_at) {
	Diff diff;
	diff.more_expensive.category = DiffCategory::MoreExpensive;
	diff.cheaper.category = DiffCategory::Cheaper;
	diff.new_entries.category = DiffCategory::New;
	diff.removed_entries.category = DiffCategory::Removed;

	std::unordered_map<uint32_t, const Entry*> active_by_id;
	active_by_id.reserve(active.entries.size());
	for(const auto& entry : active.entries)
		active_by_id[entry.id] = &entry;

	std::unordered_map<uint32_t, const Entry*> staged_by_id;
	staged_by_id.reserve(staged.entries.size());
	for(const auto& entry : staged.entries)
		staged_by_id[entry.id] = &entry;

	// Every id that appears on either side, visited once each — this is
	// what makes a multi-version skip show the net change: we never look
	// at anything but these two snapshots, so whatever happened in
	// between (including changes that cancelled out) is invisible by
	// construction, not by special-casing.
	std::unordered_map<uint32_t, bool> visited;
	visited.reserve(active.entries.size() + staged.entries.size());

	auto consider = [&](uint32_t id) {
		if(!visited.insert({ id, true }).second)
			return;

		const auto active_it = active_by_id.find(id);
		const auto staged_it = staged_by_id.find(id);
		const bool has_old = active_it != active_by_id.end();
		const bool has_new = staged_it != staged_by_id.end();

		DiffEntry entry;
		entry.id = id;
		entry.has_old = has_old;
		entry.has_new = has_new;
		// An id missing from one side is the "unlisted" baseline: limit 3
		// (unrestricted), points 0 (free). This lets New/Removed entries
		// share the exact same magnitude-sorting logic as a points/limit
		// change instead of needing a separate rule ("Prima erano gratis,
		// ora costano" in the doc is literally this baseline).
		entry.old_limit = has_old ? active_it->second->limit : 3;
		entry.old_points = has_old ? active_it->second->points : 0;
		entry.new_limit = has_new ? staged_it->second->limit : 3;
		entry.new_points = has_new ? staged_it->second->points : 0;
		entry.name = has_new ? staged_it->second->name : active_it->second->name;
		entry.macro = has_new ? staged_it->second->macro : active_it->second->macro;
		// Reason describes the new state, so it only ever comes from the
		// staged side. A Removed entry has none, on purpose.
		if(has_new) {
			entry.has_reason = staged_it->second->has_reason;
			entry.reason = staged_it->second->reason;
		}

		DiffCategory category;
		if(!has_old && has_new) {
			category = DiffCategory::New;
		} else if(has_old && !has_new) {
			category = DiffCategory::Removed;
		} else {
			const int points_delta = entry.new_points - entry.old_points;
			const int limit_delta = entry.new_limit - entry.old_limit;
			if(points_delta == 0 && limit_delta == 0)
				return; // no change at all: not part of the diff
			if(points_delta > 0)
				category = DiffCategory::MoreExpensive;
			else if(points_delta < 0)
				category = DiffCategory::Cheaper;
			else
				// points unchanged, limit changed: a stricter limit (fewer
				// copies allowed, e.g. 3 -> banned) reads as "more
				// expensive" the same way a points rise does; a looser
				// limit reads as "cheaper".
				category = (limit_delta < 0) ? DiffCategory::MoreExpensive : DiffCategory::Cheaper;
		}

		switch(category) {
			case DiffCategory::MoreExpensive: diff.more_expensive.entries.push_back(std::move(entry)); break;
			case DiffCategory::Cheaper: diff.cheaper.entries.push_back(std::move(entry)); break;
			case DiffCategory::New: diff.new_entries.entries.push_back(std::move(entry)); break;
			case DiffCategory::Removed: diff.removed_entries.entries.push_back(std::move(entry)); break;
		}
	};

	for(const auto& entry : active.entries)
		consider(entry.id);
	for(const auto& entry : staged.entries)
		consider(entry.id);

	SortAndTruncate(diff.more_expensive, truncate_at);
	SortAndTruncate(diff.cheaper, truncate_at);
	SortAndTruncate(diff.new_entries, truncate_at);
	SortAndTruncate(diff.removed_entries, truncate_at);

	return diff;
}

bool ShouldNotifyForUpdate(int staged_format_version, int last_seen_format_version) {
	return staged_format_version > last_seen_format_version;
}

DiffComparison ChooseDiffComparison(bool has_staged, bool has_previous) {
	if(has_staged)
		return DiffComparison::ActiveVsStaged;
	if(has_previous)
		return DiffComparison::PreviousVsActive;
	return DiffComparison::NoPreviousAvailable;
}

}
