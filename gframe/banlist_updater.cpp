#include "banlist_updater.h"

#include <cstdio>
#include <ctime>
#include <iterator>
#include "curl.h"
#include "deck_manager.h"
#include "file_stream.h"
#include "fmt.h"
#include "game_config.h"
#include "lflist_hash.h"
#include "logging.h"
#include "utils.h"

#if EDOPRO_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace ygo {

BanlistUpdater* gBanlistUpdater = nullptr;

namespace {

// A few hundred KB is the real artifact's size (2026-09, ~1500 entries,
// indented JSON). This is a hard ceiling against a hostile or broken
// endpoint that streams forever, not a realistic budget — enforced inside
// the write callback so the client never has to allocate past it to find
// out it should stop.
constexpr size_t MAX_BANLIST_BODY_SIZE = 8u * 1024u * 1024u;

struct FetchBuffer {
	std::string* out;
	bool too_large = false;
};

size_t FetchWriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
	auto* buffer = static_cast<FetchBuffer*>(userp);
	const size_t added = size * nmemb;
	if(buffer->out->size() + added > MAX_BANLIST_BODY_SIZE) {
		buffer->too_large = true;
		return 0; // returning less than `added` tells curl to abort the transfer
	}
	buffer->out->append(contents, added);
	return added;
}

// Takes the owning path_string, not a view: FileStream's constructor (a
// thin wrapper over std::fstream on most platforms) has no overload for
// epro::path_stringview, only for an actual std::string/path_string or a
// null-terminated C string.
bool ReadFileToString(const epro::path_string& path, std::string& out) {
	FileStream in{ path, FileStream::in | FileStream::binary };
	if(in.fail())
		return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

epro::path_string JoinPath(epro::path_stringview folder, epro::path_stringview name) {
	return epro::format(EPRO_TEXT("{}{}"), folder, name);
}

std::time_t TimegmPortable(std::tm& tm) {
#if EDOPRO_WINDOWS
	return _mkgmtime(&tm);
#else
	return timegm(&tm);
#endif
}

}

BanlistUpdater::BanlistUpdater(epro::path_stringview override_url) {
	// Empty (the default) keeps BANLIST_URL. Overridable so a test build can
	// point at a local file server without touching TRUSTED_KEYS.
	if(!override_url.empty())
		base_url = Utils::ToUTF8IfNeeded(override_url);
}

BanlistUpdater::~BanlistUpdater() {
	// A detached thread still writing into the staging folder while the
	// process tears down is how a half-written file outlives the run.
	Join();
}

void BanlistUpdater::StartCheck() {
	// Once per start (D70): called once, from DataHandler, after LoadLFList().
	// Model: ClientUpdater::CheckUpdates (client_updater.cpp) — spawn, do not
	// block startup, read results back through atomics.
	worker = epro::thread(&BanlistUpdater::CheckTask, this);
}

void BanlistUpdater::Join() {
	if(worker.joinable())
		worker.join();
}

void BanlistUpdater::CheckTask() {
	// Capped at 15 visible characters + null (Utils::SetThreadName
	// static_asserts <= 16 bytes total — the posix pthread_setname_np limit).
	Utils::SetThreadName("Banlist update");

	std::string document, signature;
	if(!Fetch(base_url + "banlist.json", document) ||
	   !Fetch(base_url + "banlist.json.sig", signature)) {
		// An unreachable endpoint is a normal Tuesday (§3, §5): keep the
		// active list, raise no alarm at the player. Nothing is written to
		// disk on a failed fetch.
		failed = true;
		return;
	}

	banlist::Payload incoming;
	std::string error;
	if(banlist::VerifyAndParse(document, signature, incoming, error) != banlist::VerifyStatus::Ok) {
		// Covers both a bad/absent signature and a schema violation — either
		// way, keep the active list, log, write nothing (§2a, §3).
		ErrorLog("Banlist update rejected: {}", error);
		failed = true;
		return;
	}

	switch(banlist::CompareVersion(incoming.format_version, active_payload.format_version)) {
	case banlist::VersionDecision::AlreadyCurrent:
		return; // no action, no notification: this is not an update
	case banlist::VersionDecision::Rollback:
		// A downgrade attempt is a thing that happened, not a no-op: it gets
		// logged even though nothing is written.
		ErrorLog("Banlist update refused: format_version {} is not newer than the active {}",
				incoming.format_version, active_payload.format_version);
		failed = true;
		return;
	case banlist::VersionDecision::Accept:
		break;
	}

	// Fresh install: no active list was loaded at startup, so format_version
	// is still its struct default of 0 — a real published version is always
	// >= 1 (D62). Read before staging changes anything.
	const bool had_no_active_list = (active_payload.format_version == 0);

	if(!AtomicWrite(JoinPath(STAGED_FOLDER, PAYLOAD_NAME), document) ||
	   !AtomicWrite(JoinPath(STAGED_FOLDER, SIGNATURE_NAME), signature)) {
		failed = true;
		return;
	}

	// staged_payload before staged_ready, in that order: the main thread
	// must never observe the flag before the bytes it describes are on disk
	// and the struct it reads is filled in.
	staged_payload = std::move(incoming);
	staged_ready = true;

	// Exception to "applies at the next start" (design/banlist-distribution.md,
	// "Ciclo di vita"): on a fresh install there is no active list, so there
	// is no running session whose rules would change out from under a
	// player. This converges the ON-DISK state immediately — not the
	// in-memory list DeckManager already loaded at startup, which this class
	// has no way to reach outside of LoadActiveInto(DeckManager&) at the
	// next startup — so a fresh install that loses network right after this
	// first successful fetch still finds an active list on its very next
	// launch instead of needing to reach the endpoint a second time.
	if(had_no_active_list) {
		PromoteStaged();
		// PromoteStaged() only touches disk — it has no reason to know
		// about staged_ready/staged_payload/active_payload, since at every
		// OTHER call site (startup, before LoadActiveInto) those members
		// are not what's being decided. Here they are: without this, a
		// fresh install would leave staged_ready true and active_payload
		// stuck at its format_version-0 default for the rest of the
		// session, so HasStagedUpdate() would lie to the FASE 4c
		// notification (nothing is pending — it just got applied) and
		// ComputeDiff(ActivePayload(), StagedPayload()) would compare
		// against an empty list, making every single entry read as "New"
		// instead of showing nothing, which is what a fresh install with
		// no real predecessor to diff against should show.
		active_payload = staged_payload;
		staged_payload = banlist::Payload{};
		staged_ready = false;
	}
}

bool BanlistUpdater::PromoteStaged() {
	const auto staged_payload_path = JoinPath(STAGED_FOLDER, PAYLOAD_NAME);
	const auto staged_signature_path = JoinPath(STAGED_FOLDER, SIGNATURE_NAME);
	if(!Utils::FileExists(staged_payload_path) || !Utils::FileExists(staged_signature_path))
		return true; // nothing staged — not an error, and idempotent to call again

	std::string document, signature;
	if(!ReadFileToString(staged_payload_path, document) ||
	   !ReadFileToString(staged_signature_path, signature))
		return true; // an unreadable staged pair is treated as "nothing to promote"

	banlist::Payload parsed;
	std::string error;
	if(banlist::VerifyAndParse(document, signature, parsed, error) != banlist::VerifyStatus::Ok) {
		// Bytes that sat on a disk are not bytes verified in memory an hour
		// ago (an interrupted write, tampering, bit rot). Left in place
		// rather than deleted: wiping them here would throw away the only
		// evidence of what went wrong, and the next successful background
		// check overwrites them anyway.
		ErrorLog("Banlist staged pair failed to re-verify, not promoted: {}", error);
		return false;
	}

	const auto active_payload_path = JoinPath(ACTIVE_FOLDER, PAYLOAD_NAME);
	const auto active_signature_path = JoinPath(ACTIVE_FOLDER, SIGNATURE_NAME);
	// Fresh copies into ACTIVE_FOLDER through AtomicWrite's own tmp+rename —
	// never a rename of the staged files themselves. That is what makes "a
	// crash at any point leaves staged intact" true by construction: nothing
	// here mutates or moves the staged pair, only reads it, until the
	// explicit delete at the very end.
	if(!AtomicWrite(active_payload_path, document) || !AtomicWrite(active_signature_path, signature))
		return false;

	// Re-verify the pair that is now active from a fresh read of the bytes
	// actually on disk — not a reuse of the verification a moment ago on the
	// pre-write copy. This second check is the other half of the
	// crash-safety argument: staged is deleted only once it passes.
	std::string active_document, active_signature;
	if(!ReadFileToString(active_payload_path, active_document) ||
	   !ReadFileToString(active_signature_path, active_signature))
		return false;
	banlist::Payload reverified;
	if(banlist::VerifyAndParse(active_document, active_signature, reverified, error) != banlist::VerifyStatus::Ok)
		return false;

	Utils::FileDelete(staged_payload_path);
	Utils::FileDelete(staged_signature_path);
	return true;
}

bool BanlistUpdater::LoadActiveInto(DeckManager& deckManager) {
	const auto active_payload_path = JoinPath(ACTIVE_FOLDER, PAYLOAD_NAME);
	const auto active_signature_path = JoinPath(ACTIVE_FOLDER, SIGNATURE_NAME);

	std::string document, signature;
	if(!ReadFileToString(active_payload_path, document) || !ReadFileToString(active_signature_path, signature))
		return false; // no active pair on disk: nothing to load, not an error to shout about

	banlist::Payload payload;
	std::string error;
	if(banlist::VerifyAndParse(document, signature, payload, error) != banlist::VerifyStatus::Ok) {
		// The active pair not verifying, with nothing staged to fall back on
		// (PromoteStaged already ran and found nothing, or itself failed),
		// means: load nothing. The client keeps whatever .conf lists it has
		// — upstream behaviour — and the player is told the custom list is
		// unavailable rather than silently playing an unsigned format.
		ErrorLog("Active banlist failed to verify, custom format unavailable: {}", error);
		return false;
	}

	LFList lflist;
	lflist.listName = LIST_NAME_PREFIX + std::to_wstring(payload.format_version);
	lflist.hash = LFLIST_HASH_SEED;
	lflist.whitelist = false;
	for(const auto& entry : payload.entries) {
		lflist.content[entry.id] = BanlistEntry{ entry.limit, entry.points };
		lflist.hash = FoldLFListEntry(lflist.hash, entry.id, entry.limit, entry.points);
	}
	// Nothing here is written to disk (D8): the plaintext list only ever
	// exists in this in-memory LFList, built fresh from the verified payload
	// every time this runs.
	deckManager._lfList.push_back(std::move(lflist));

	// Fills the version CheckTask's anti-rollback compares the fetched one
	// against — which is why this has to run before StartCheck().
	active_payload = std::move(payload);
	return true;
}

bool BanlistUpdater::ActiveExpired() const {
	if(active_payload.expires_at.empty())
		return false;
	// expires_at is an ISO-8601 UTC timestamp, e.g. "2027-03-09T00:00:00Z"
	// (design/vault-banlist/vault-pipeline.md §10.2).
	std::tm tm{};
	if(std::sscanf(active_payload.expires_at.c_str(), "%d-%d-%dT%d:%d:%d",
				  &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return false; // unparseable timestamp: advisory only, never blocks play
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	const std::time_t expires = TimegmPortable(tm);
	if(expires == static_cast<std::time_t>(-1))
		return false;
	return std::time(nullptr) > expires;
}

bool BanlistUpdater::Fetch(const std::string& url, std::string& out) {
	out.clear();
	char error_buffer[CURL_ERROR_SIZE]{};
	auto curl_handle = curl_easy_init();
	FetchBuffer buffer{ &out };
	curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_URL, url.data());
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, FetchWriteCallback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
	// Short timeouts on purpose: this is a two-file background check that
	// must never make a player wait, not a large download with a progress
	// bar (ClientUpdater's 60s connect timeout is for that other case).
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
	curl_easy_setopt(curl_handle, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	if(gGameConfig->ssl_certificate_path.size()
	   && Utils::FileExists(Utils::ToPathString(gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl_handle, CURLOPT_CAINFO, gGameConfig->ssl_certificate_path.data());
	auto res = curl_easy_perform(curl_handle);
	curl_easy_cleanup(curl_handle);
	if(buffer.too_large) {
		out.clear();
		return false;
	}
	if(res != CURLE_OK) {
		if(gGameConfig->logDownloadErrors)
			ErrorLog("Banlist fetch curl error: ({}) {} ({})", res, curl_easy_strerror(res), error_buffer);
		out.clear();
		return false;
	}
	return true;
}

bool BanlistUpdater::AtomicWrite(epro::path_stringview path, const std::string& bytes) {
	const auto tmp_path = epro::format(EPRO_TEXT("{}.tmp"), path);
	if(!Utils::CreatePath(tmp_path))
		return false;
	{
		FileStream out{ tmp_path, FileStream::out | FileStream::binary | FileStream::trunc };
		if(out.fail())
			return false;
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if(out.fail())
			return false;
	}
	return AtomicReplace(tmp_path, path);
}

bool BanlistUpdater::AtomicReplace(epro::path_stringview from, epro::path_stringview to) {
#if EDOPRO_WINDOWS
	return MoveFileEx(from.data(), to.data(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
	// NOT Utils::FileMove — that is a bare MoveFile on Windows and fails
	// outright when the destination exists, which is every call here after
	// the first.
	return std::rename(from.data(), to.data()) == 0;
#endif
}

}
