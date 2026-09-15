#include "banlist_updater.h"

#include "curl.h"
#include "deck_manager.h"
#include "file_stream.h"
#include "fmt.h"
#include "lflist_hash.h"
#include "utils.h"

namespace ygo {

BanlistUpdater* gBanlistUpdater = nullptr;

// SKELETON — FASE 4b. Contract fixed in banlist_updater.h; bodies are the
// implementation pass. Every comment below is a requirement,
// not a suggestion.

BanlistUpdater::BanlistUpdater(epro::path_stringview override_url) {
	// override_url empty → BANLIST_URL. Kept overridable so a test build can
	// point at a local file server without touching the trusted keys.
	(void)override_url;
}

BanlistUpdater::~BanlistUpdater() {
	// Join the worker. A detached thread writing into the staging folder while
	// the process tears down is how a half-written file outlives the run.
}

void BanlistUpdater::StartCheck() {
	// Spawn CheckTask on `worker`. Model: RepoManager (repo_manager.cpp:98) —
	// worker thread, atomics for the result, main thread reads them.
	// Frequency: once per start (D70). The file is tiny and the cost is nil.
}

void BanlistUpdater::Join() {
}

void BanlistUpdater::CheckTask() {
	// Utils::SetThreadName("Banlist update task");
	//
	// 1. Fetch <base_url>/banlist.json and <base_url>/banlist.json.sig into
	//    memory. Either failing → failed = true, return. NOTHING is written to
	//    disk on a failed fetch, and the first failure raises no alarm at the
	//    player: an unreachable endpoint is a normal Tuesday (§3, §5).
	// 2. banlist::VerifyAndParse on the bytes. Not Parse. Not "parse to look
	//    at the version first" — verification comes before nlohmann/json sees
	//    a byte (§2a).
	// 3. Only now, on a payload that verified: CompareVersion against the
	//    active version. Accept → stage. AlreadyCurrent → nothing, and no
	//    notification. Rollback → refuse and log; a downgrade attempt is a
	//    thing that happened, not a no-op.
	// 4. Staging: AtomicWrite the json, AtomicWrite the sig, into STAGED_FOLDER.
	//    Then set staged_payload and staged_ready — in that order, so the main
	//    thread never sees the flag before the bytes are on disk.
	//
	// A bad signature or a schema violation ends here: keep the active list,
	// log, write nothing.
}

bool BanlistUpdater::PromoteStaged() {
	// Order matters, and it is the whole of the crash-safety argument:
	//  1. if STAGED_FOLDER has no pair, nothing to do → true.
	//  2. re-verify the staged pair from disk. Bytes that sat on a disk are
	//     not bytes we verified in memory an hour ago.
	//  3. AtomicReplace json, then sig, into ACTIVE_FOLDER.
	//  4. re-verify the ACTIVE pair. Only if it verifies, delete staged.
	// A crash at any point leaves staged intact and active either wholly old
	// or wholly new-but-unverified; the next start re-runs this and converges.
	// Never delete staged before step 4 succeeds.
	return false;
}

bool BanlistUpdater::LoadActiveInto(DeckManager& deckManager) {
	// Read + verify the active pair (again — this is the only gate between a
	// file on disk and the rules of the format). Then build an LFList in
	// memory: listName = LIST_NAME_PREFIX + format_version, hash starting at
	// LFLIST_HASH_SEED, content[id] = BanlistEntry{limit, points}, every entry
	// folded through FoldLFListEntry. whitelist = false.
	// Also fills active_payload: it is the version CheckTask compares the
	// fetched one against, so this must run before StartCheck().
	// Append to deckManager._lfList. Nothing is written to disk (D8).
	//
	// If the active pair does not verify and there is no staged pair to fall
	// back on, load nothing and say so: the client keeps whatever .conf lists
	// it has, which is upstream behaviour, and the player is told the custom
	// list is unavailable rather than silently playing an unsigned format.
	(void)deckManager;
	return false;
}

bool BanlistUpdater::ActiveExpired() const {
	// expires_at compared to now, UTC. Advisory only: never blocks play, never
	// refuses to start. The strong "you are behind" signal is the hash
	// mismatch at the door of a room (D9/D71), which is immediate and precise;
	// this only covers "I have not reached the endpoint in months" (§5).
	return false;
}

bool BanlistUpdater::Fetch(const std::string& url, std::string& out) {
	// libcurl through curl.h. CURLOPT_FOLLOWLOCATION, a timeout, the bundled
	// cacert.pem like the rest of the client, and a hard cap on the body size
	// enforced inside the write callback — an endpoint that streams forever
	// must not be able to make the client allocate forever.
	(void)url; (void)out;
	return false;
}

bool BanlistUpdater::AtomicWrite(epro::path_stringview path, const std::string& bytes) {
	// Write to "<path>.tmp", flush, then AtomicReplace onto <path>.
	(void)path; (void)bytes;
	return false;
}

bool BanlistUpdater::AtomicReplace(epro::path_stringview from, epro::path_stringview to) {
	// Windows: MoveFileEx(from, to, MOVEFILE_REPLACE_EXISTING).
	// POSIX: rename(2).
	// NOT Utils::FileMove — that is a bare MoveFile on Windows and fails when
	// the destination exists, which is precisely the case here every time
	// after the first.
	(void)from; (void)to;
	return false;
}

}
