#ifndef TITLE_STORE_H
#define TITLE_STORE_H

// Disk persistence and the D103/D104 state transitions for the client's
// title, building on title_verify.h (signature/schema) and title_state.h
// (the pure access decision this class evaluates against whatever it is
// currently holding). This is the half that talks to the filesystem —
// analogous to BanlistUpdater vs banlist_verify.h, with one structural
// difference: BanlistUpdater's staged_ready is a ONE-SHOT handoff (the
// background fetch runs once per process start), while a title check-in
// repeats for the life of the process (§2 — startup + every 15 min at
// rest / 5 min in-duel), so this class's fields are live-mutated
// repeatedly rather than written once and then only read.
//
// Thread safety: ApplyResponse() runs on title_checkin.cpp's background
// worker. CurrentAccess()/Title()/Block()/BoundUserRef() are read from the
// main thread at any time, including while a check-in is being applied. A
// plain mutex guards the in-memory fields for exactly that reason —
// check-ins are minutes apart and reads are a handful of string copies, so
// contention is not a real cost here. Generation() is deliberately
// lock-free (a cheap poll for "did anything change"); the actual data is
// only ever read through the locked accessors, which is what makes that
// safe regardless of the counter's own memory order — see title_store.cpp.
//
// Design: design/fork-edopro/access-control.md §5-§6 (private vault — this
// repo only ever gets code and sober "what", per this repo's own
// CLAUDE.md).

#include <atomic>
#include <cstdint>
#include <ctime>
#include <string>
#include "epro_mutex.h"
#include "text_types.h"
#include "title_state.h"

namespace ygo {

class TitleStore {
public:
	// Hidden, like BanlistUpdater's ACTIVE_FOLDER/STAGED_FOLDER — never
	// hand-editable-by-accident, and outside ./lflists/ entirely since this
	// is not a list a player could ever load.
	static constexpr auto STORE_FOLDER = EPRO_TEXT("./title/.store/");
	static constexpr auto TITLE_NAME = EPRO_TEXT("title.json");
	static constexpr auto BLOCK_NAME = EPRO_TEXT("block.json"); // last verified revocation OR suspension, whichever
	static constexpr auto USER_REF_NAME = EPRO_TEXT("user_ref.txt");

	// Startup path, main thread, before title_checkin's worker starts:
	// reads and re-verifies whatever is cached (§5 — "si riverifica al
	// caricamento"). A missing or corrupt file is never an error: it is
	// exactly the fresh-install state (AccessState::NoTitle).
	void LoadFromDisk();

	// Verifies and applies one raw HTTP response body from a title
	// check-in (title_checkin.cpp) — a 200 (title) or 403
	// (revocation/suspension) body, never anything else: every other
	// status (401/404/429/5xx/timeout/malformed transport) is filtered
	// before it would reach here (D102 enforced at the check-in layer, not
	// this one). Within a body that DID reach here, a bad signature, an
	// unparseable body, an unrecognized "type", or a user_ref that
	// conflicts with the one this client is bound to (D104) are ALL
	// logged no-ops — this function can only ever leave state unchanged,
	// or move it to a state the server verifiably, cryptographically asked
	// for. Returns true iff it actually changed something.
	bool ApplyResponse(const std::string& raw_document);

	// The access decision, evaluated fresh against `now` — never cached,
	// since a title's expiry changes with the clock alone, without any new
	// message ever arriving.
	title::AccessState CurrentAccess(std::time_t now) const;

	// Snapshots for the UI layer (§8 — differentiated messages) and for
	// enforcement (Block().banned_until, Block().kind, ...). Return by
	// value: the lock only protects the copy, not continued access after
	// the call returns.
	title::StoredTitle Title() const;
	title::StoredBlock Block() const;

	bool HasBoundUserRef() const;
	std::string BoundUserRef() const;

	// Bumped every time ApplyResponse() (or a LoadFromDisk() re-apply)
	// actually changes state. The main loop (game.cpp) compares this
	// against the last value it saw to know when to re-evaluate
	// CurrentAccess() and react — the same "worker sets, main frame loop
	// polls" shape as the rest of gframe's background tasks, generalized
	// from BanlistUpdater's one-shot staged_ready to a counter because
	// check-ins repeat for the life of the process.
	uint64_t Generation() const { return generation_.load(std::memory_order_relaxed); }

private:
	// Shared by ApplyResponse (live, persist=true) and LoadFromDisk (bytes
	// already came FROM disk, persist=false — re-writing them back is
	// redundant). Must NOT be called while mutex_ is already held.
	bool ApplyVerifiedDocument(const std::string& raw_document, bool persist);
	// D104. Caller must already hold mutex_. Binds on first call (and
	// persists the binding immediately) or checks-and-rejects on every
	// call after.
	bool BindOrCheck(const std::string& user_ref);

	static bool AtomicWrite(epro::path_stringview path, const std::string& bytes);
	static bool AtomicReplace(epro::path_stringview from, epro::path_stringview to);

	mutable epro::mutex mutex_;
	title::StoredTitle title_;
	title::StoredBlock block_;
	bool has_bound_user_ref_ = false;
	std::string bound_user_ref_;
	std::atomic<uint64_t> generation_{ 0 };
};

extern TitleStore* gTitleStore;

}

#endif //TITLE_STORE_H
