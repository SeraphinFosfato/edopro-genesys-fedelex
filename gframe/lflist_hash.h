#ifndef LFLIST_HASH_H
#define LFLIST_HASH_H

#include <algorithm>
#include <cstdint>

namespace ygo {

// The hash that identifies a banlist between host and client. It exists in
// exactly one place on purpose: the list can be built from a .conf file
// (DeckManager::LoadLFListSingle) or in memory from a signed banlist.json
// (banlist_updater), and if those two paths ever folded entries differently
// the same list would produce two different hashes depending on where it came
// from — the exact silent-disagreement failure the hash is there to prevent.
// Anything that populates a banlist_content_t folds its entries through here.

inline constexpr uint32_t LFLIST_HASH_SEED = 0x7dfcee6a;

// `limit` is clamped here rather than by the caller: it reaches this function
// from parsed input, and `code << (27 + limit)` is undefined behaviour for
// limit > 3 (shift >= 32 on a uint32_t). Clamping in the single place that
// shifts means no future caller can reintroduce the UB by forgetting to.
inline uint32_t FoldLFListEntry(uint32_t hash, uint32_t code, int limit, int points) {
	limit = std::clamp(limit, 0, 3);
	uint32_t folded = hash
		^ ((code << 18) | (code >> 14))
		^ ((code << (27 + limit)) | (code >> (5 - limit)));
	// The hash is not ours: it is the identifier every EDOPro on the network
	// uses to name a list (design/banlist-distribution.md, "L'hash non è
	// nostro"). A list with no points must fold to bit-for-bit the same value
	// upstream produces — mixing points in unconditionally gave every list,
	// including the standard ones nobody here ever touched, a hash that
	// exists nowhere else on the network. So the points term is folded in
	// only when there is a point cost to disagree about: mixed through a
	// fixed-amount rotation — never shifted by an amount derived from points,
	// which would be the same UB by another door. With it, two clients
	// agreeing on every id/limit but not on points still hash differently and
	// notice they disagree about what's legal; without a single non-zero
	// points entry, that disagreement doesn't exist yet, so nothing needs to
	// be folded in for it.
	if(points != 0) {
		const uint32_t points_mixed = code ^ (static_cast<uint32_t>(points) * 0x1000193u);
		folded ^= ((points_mixed << 7) | (points_mixed >> 25));
	}
	return folded;
}

}

#endif //LFLIST_HASH_H
