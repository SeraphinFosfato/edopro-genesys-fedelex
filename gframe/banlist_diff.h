#ifndef BANLIST_DIFF_H
#define BANLIST_DIFF_H

// Client-side diff between the active banlist and a freshly staged one.
// design/banlist-distribution.md, section "Il diff".
//
// Deliberately as gframe-free as banlist_verify.h: no irrlicht, no globals,
// just two Payloads in and a data structure out. The GUI layer formats and
// displays it; this module only decides WHAT changed and in what order.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "banlist_verify.h"

namespace ygo::banlist {

enum class DiffCategory {
	MoreExpensive, // points up, or limit tightened at equal points
	Cheaper,       // points down, or limit loosened at equal points
	New,           // id absent from active, present in staged
	Removed,       // id present in active, absent from staged
};

struct DiffEntry {
	uint32_t id = 0;
	std::string name;
	std::string macro;

	// An id absent from one side is treated as the "unlisted" baseline
	// (limit 3 / points 0, i.e. unrestricted and free) rather than a
	// missing value — see ComputeDiff's comment for why. has_old/has_new
	// says which side is real vs. this baseline, for a GUI that wants to
	// render "— " instead of a literal 0/3.
	bool has_old = false;
	bool has_new = false;
	int old_points = 0;
	int new_points = 0;
	int old_limit = 3;
	int new_limit = 3;

	// Sourced from the STAGED entry only (the side describing the new
	// state). A Removed entry has no staged side, so it never carries a
	// reason — inventing one for "why was this removed" is exactly what
	// the contract forbids ("il client non inventa mai un perché").
	bool has_reason = false;
	std::string reason;
};

struct DiffGroup {
	DiffCategory category = DiffCategory::MoreExpensive;
	std::vector<DiffEntry> entries; // sorted by magnitude descending, already truncated
	size_t omitted = 0;             // how many more entries exist beyond `entries` ("e altre N")
};

struct Diff {
	DiffGroup more_expensive;
	DiffGroup cheaper;
	DiffGroup new_entries;
	DiffGroup removed_entries;
};

constexpr size_t DIFF_GROUP_TRUNCATE = 15;

// Computes the four groups between `active` and `staged`, by id. Pure and
// synchronous — this is comparing two already-parsed, already-verified
// payloads, nothing here touches disk or network.
Diff ComputeDiff(const Payload& active, const Payload& staged, size_t truncate_at = DIFF_GROUP_TRUNCATE);

// Whether the "staging is ready" notification should be shown for
// `staged_format_version`, given the format_version the player last
// dismissed a notification for (0 if never notified). Persisting that last
// value is the GUI layer's job (design doc, "La notifica": "stato 'già
// vista' persistente per format_version") — this function is only the pure
// gate, so the rule itself has a test that does not need a config file.
bool ShouldNotifyForUpdate(int staged_format_version, int last_seen_format_version);

// Which pair the "Novità" window (FASE 4e, point 4) compares, given only
// whether a staged update and a saved previous pair exist. Pure on purpose,
// same reason as CompareVersion in banlist_verify.h: the choice is worth a
// test that needs no disk, no GUI, and no BanlistUpdater instance. The
// caller resolves has_staged/has_previous (HasStagedUpdate(),
// LoadPreviousPayload()) and picks the actual Payloads to diff and the
// version numbers to put in the title; this function only picks which case
// it is.
enum class DiffComparison {
	ActiveVsStaged,      // staging ready: "attiva → in arrivo"
	PreviousVsActive,     // no staging, a previous exists: "precedente → attiva"
	NoPreviousAvailable, // no staging, nothing saved yet (fresh install)
};

DiffComparison ChooseDiffComparison(bool has_staged, bool has_previous);

}

#endif //BANLIST_DIFF_H
