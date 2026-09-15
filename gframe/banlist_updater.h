#ifndef BANLIST_UPDATER_H
#define BANLIST_UPDATER_H

// Fetch, staging and promotion of the signed banlist artifact.
//
// This is the half that talks to the network and the disk. Everything
// security-relevant that can be decided without either lives in
// banlist_verify.h, which this file uses and never duplicates.
//
// Design: design/banlist-distribution.md.

#include <atomic>
#include <string>
#include "banlist_verify.h"
#include "epro_thread.h"
#include "text_types.h"

#ifndef BANLIST_URL
#define BANLIST_URL "https://seraphinfosfato.github.io/Banlist-dist/"
#endif

namespace ygo {

class DeckManager;

class BanlistUpdater {
public:
	// Where the two files live. Hidden folders, and never a .conf: they sit
	// inside ./lflists/ but LoadLFListFolder only picks up .conf files, so the
	// signed payload can never be mistaken for a list to load directly.
	static constexpr auto ACTIVE_FOLDER = EPRO_TEXT("./lflists/.active/");
	static constexpr auto STAGED_FOLDER = EPRO_TEXT("./lflists/.staged/");
	static constexpr auto PAYLOAD_NAME = EPRO_TEXT("banlist.json");
	static constexpr auto SIGNATURE_NAME = EPRO_TEXT("banlist.json.sig");

	// The name the list carries in the UI. Only the HASH travels between
	// clients: server_lobby.cpp resolves a room's list by matching that hash
	// against the local lists and falls back to "???" when it knows none with
	// that hash. So carrying the version in the visible name costs nothing and
	// buys the one thing a player needs when a room is refused — being able to
	// see, in words, that their list is not the other one's.
	static constexpr auto LIST_NAME_PREFIX = L"GSY Custom v";

	explicit BanlistUpdater(epro::path_stringview override_url = {});
	~BanlistUpdater();

	// --- startup path, main thread, before DeckManager::LoadLFList() ---

	// Promotes a staged payload to active, if there is one. Crash-safe by
	// ordering, not by hoping: staged is deleted ONLY after the promoted
	// active pair has itself verified. A crash anywhere in between leaves
	// staged in place and the promotion is simply retried at the next start,
	// so there is no window in which the active pair is a new json with an old
	// signature. Idempotent: calling it twice is calling it once.
	//
	// Exception to "apply at the next start": when there is NO active list at
	// all (fresh install), staged is promoted immediately. The rule exists so
	// the rules never change under a player mid-session; with no list yet
	// there is no session to disturb, and the alternative is a new install
	// that plays its first session with no format at all.
	bool PromoteStaged();

	// Builds the LFList in memory from the active signed payload and appends
	// it to the DeckManager. The plaintext list is never written to disk
	// (decision D8): a .conf on disk is hand-editable, the client would eat
	// the edit without noticing, and the player would end up with an hash
	// nobody else has and an error message that explains nothing. Folds every
	// entry through FoldLFListEntry (lflist_hash.h) — same hash as the .conf
	// path, by construction.
	bool LoadActiveInto(DeckManager& deckManager);

	// --- background path ---

	// Non-blocking. Spawns the worker: fetch → verify → anti-rollback →
	// staging. Never blocks startup and never fails loudly: a client with no
	// network starts normally on the list it already has (§5).
	void StartCheck();
	void Join();

	bool HasStagedUpdate() const { return staged_ready; }
	bool CheckFailed() const { return failed; }
	// True when the active payload is past its expires_at. Advisory only —
	// it produces a discreet persistent notice and never blocks play.
	// Instance methods, not statics: they read and fill `active_payload`,
	// which is the version the anti-rollback check compares against. The
	// updater is therefore constructed BEFORE LoadLFList(), not after.
	bool ActiveExpired() const;

	// Valid only once HasStagedUpdate(); the diff of FASE 4c reads these.
	const banlist::Payload& StagedPayload() const { return staged_payload; }
	const banlist::Payload& ActivePayload() const { return active_payload; }

private:
	void CheckTask();
	// Writes one file through a temporary + atomic replace. Does NOT use
	// Utils::FileMove: on Windows that is MoveFile, which fails outright when
	// the destination exists, so it cannot replace anything. This uses
	// MoveFileEx(MOVEFILE_REPLACE_EXISTING) / rename(2).
	static bool AtomicWrite(epro::path_stringview path, const std::string& bytes);
	static bool AtomicReplace(epro::path_stringview from, epro::path_stringview to);
	// GETs one url into memory. Bounded: refuses a body past a sane cap, so a
	// hostile or broken endpoint cannot make the client allocate forever.
	static bool Fetch(const std::string& url, std::string& out);

	std::string base_url{ BANLIST_URL };
	banlist::Payload active_payload;
	banlist::Payload staged_payload;
	epro::thread worker;
	std::atomic<bool> staged_ready{ false };
	std::atomic<bool> failed{ false };
};

extern BanlistUpdater* gBanlistUpdater;

}

#endif //BANLIST_UPDATER_H
