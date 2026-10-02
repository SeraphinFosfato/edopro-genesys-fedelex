#ifndef CLIENT_UPDATER_H
#define CLIENT_UPDATER_H

#include "config.h"
#if defined(UPDATE_URL) && !EDOPRO_IOS
#include <vector>
#include <atomic>
#include <string>
#endif
#include "utils.h"
// ygo::update::Manifest/IsClientSupported()/CLIENT_UPDATE_VERSION: this
// module (update_verify.cpp) compiles unconditionally regardless of
// UPDATE_URL (see premake5.lua — it is not guarded there, unlike this
// class' own implementation below), so callers that need the gate's R1 fact
// even on a build without UPDATE_URL (game.cpp's RefreshOnlineGate(),
// design/blocco-online.md) can always reach it through this header.
#include "update_verify.h"
#include "client_update_version.h"

struct UnzipperPayload {
	int cur;
	int tot;
	const epro::path_char* filename;
	void* payload;
};

using update_callback = void(*)(int percentage, int cur, int tot, const char* filename, bool is_new, void* payload);

namespace ygo {
#if defined(UPDATE_URL) && !EDOPRO_IOS
class ClientUpdater {
public:
	ClientUpdater(epro::path_stringview override_url);
	~ClientUpdater() = default;
	bool StartUpdate(update_callback callback, void* payload);
	void StartUnzipper(unzip_callback callback, void* payload);
	void CheckUpdates();
	// FASE 59 (design/blocco-online.md §7): re-reads the manifest on its
	// own thread, with the SAME verification as CheckUpdate() (firma prima
	// di interpretare, anti-rollback) — the only thing this does NOT do is
	// touch update_urls/has_update/downloaded, because this is not the
	// once-per-launch update PROPOSAL (client-update.md §8, unchanged: a
	// new binary only becomes active at the next restart, so polling for
	// it more often buys nothing). This exists purely to let
	// min_supported — and therefore R1 — close the gate mid-session, with
	// the caller (Game::MainLoop) deciding the 15-minute cadence and
	// calling this no more often than that.
	void CheckOnlineGateThreshold();
	bool HasUpdate() {
		return has_update;
	}
	bool UpdateDownloaded() {
		return downloaded;
	}
	bool UpdateFailed() {
		return failed;
	}
	// One line describing the outcome of the last check — populated for
	// every branch of the behaviour table in design/client-update.md §5, not
	// only the failure ones. The caller is responsible for surfacing it;
	// this class only ever appends to the log itself (see client_updater.cpp),
	// which is not the same audience as a player-facing notice.
	const std::string& GetStatusMessage() const {
		return status_message;
	}
	// design/client-update.md §9 (FASE 36) / design/blocco-online.md (D237,
	// FASE 59): the raw R1 fact, nothing else. 0 means "no verified manifest
	// has ever declared a floor" — the same "absent never closes" sentinel
	// Manifest::min_supported already uses (update_verify.h), so a launch
	// that never reaches a verified manifest (no UPDATE_URL reply, bad
	// signature, unreachable endpoint) answers as "supported" forever,
	// exactly like before this field existed. Deliberately NOT a bool plus a
	// precomputed message anymore (that was FASE 36's OnlineDisabled() /
	// GetOnlineDisabledReason()): FASE 59 needs this combined with R2, which
	// ClientUpdater has no way to know about (that is Game's core state) —
	// composing the single player-facing text is now Game's job
	// (Game::GetOnlineGateMessage(), game.cpp), using
	// ygo::update::IsClientSupported(GetLastMinSupported(),
	// ygo::update::CLIENT_UPDATE_VERSION) for the R1 half.
	int GetLastMinSupported() const {
		return last_min_supported;
	}
private:
	class FileLock {
#if EDOPRO_ANDROID
	public:
		constexpr bool acquired() { return true; }
#elif EDOPRO_WINDOWS || EDOPRO_LINUX || EDOPRO_MACOS
#if EDOPRO_WINDOWS
		using lock_type = void*;
		static constexpr lock_type null_lock = nullptr;
#else
		using lock_type = int;
		static constexpr lock_type null_lock = 0;
#endif
		lock_type m_lock{ null_lock };
	public:
		bool acquired() { return m_lock != null_lock; };
		FileLock();
		~FileLock();
#endif
	};
	void CheckUpdate();
	void CheckOnlineGateThresholdTask();
	// Shared by CheckUpdate() (the once-per-launch full flow) and
	// CheckOnlineGateThreshold() (the 15-minute re-check, FASE 59): fetches
	// the manifest and its detached signature, verifies BEFORE parsing
	// (same ordering as CheckUpdate() always had), and on success updates
	// `last_min_supported`. Returns false on any failure (network, bad
	// signature, rollback, schema) — callers that need the full manifest
	// for more than the threshold (CheckUpdate(), which also installs
	// files) get it back through `out_manifest`; the periodic check passes
	// a throwaway one and only cares about the bool and the log lines this
	// already writes.
	bool FetchVerifiedManifest(ygo::update::Manifest& out_manifest);
	void DownloadUpdate(void* payload, update_callback callback);
	void Unzip(void* payload, unzip_callback callback);
	struct DownloadInfo {
		std::string name;
		std::string url;
		std::string sha256; // the only thing that authorizes installing this file (design/client-update.md, point 3)
		std::string md5;    // upstream compatibility only, kept but never checked — see the same doc
	};
	// The installed version IS the running binary's own build number
	// (design/client-update.md §6quinquies, D238) — never a file written
	// after a download. The file-based version (`.edopro_update_version`,
	// written by a now-removed SetInstalledVersion()) lagged reality
	// whenever DownloadUpdate() wrote it but Unzip()/Reboot() failed
	// afterwards (§6bis/§6ter): the file said "already on the new version"
	// while the OLD binary kept running, and from then on CompareVersion()
	// answered AlreadyCurrent forever — the one remedy (updating) stopped
	// being offered, permanently, to a client that still needed it. With
	// D237's online gate this stops being merely annoying and becomes a
	// trap: online would stay closed with no way out. CLIENT_UPDATE_VERSION
	// cannot lag like that: it changes only when a new binary is actually
	// running. A `.edopro_update_version` left behind by an older build is
	// not read or deleted here — cleaning the data folder is not this
	// function's job.
	static int GetInstalledVersion();

	std::vector<DownloadInfo> update_urls;
	FileLock Lock{};
	std::atomic<bool> has_update{ false };
	std::atomic<bool> downloaded{ false };
	std::atomic<bool> failed{ false };
	std::atomic<bool> downloading{ false };
	std::string update_url{ UPDATE_URL };
	std::string status_message;
	// See GetLastMinSupported() above. 0 ("absent", Manifest::min_supported's
	// own convention) until a manifest verifies at least once — a launch
	// that never reaches a verified manifest (no UPDATE_URL reply, bad
	// signature, unreachable endpoint) leaves online play exactly as
	// available as it always was, forever, never on the absence of
	// information.
	std::atomic<int> last_min_supported{ 0 };
};
#else
class ClientUpdater {
public:
	ClientUpdater(epro::path_stringview) {}
	~ClientUpdater() = default;
	static constexpr bool StartUpdate(update_callback, void*) { return false; }
	static constexpr void StartUnzipper(unzip_callback, void*) {}
	static constexpr void CheckUpdates() {}
	static constexpr void CheckOnlineGateThreshold() {}
	static constexpr bool HasUpdate() { return false; }
	static constexpr bool UpdateDownloaded() { return false; }
	static constexpr bool UpdateFailed() { return true; }
	static epro::stringview GetStatusMessage() { return {}; }
	// A build without UPDATE_URL never fetches a manifest, so it never
	// learns of a min_supported floor: online play stays exactly as
	// available as it always was in this build (0 == "absent", same
	// sentinel as Manifest::min_supported).
	static constexpr int GetLastMinSupported() { return 0; }
};
#endif

extern ClientUpdater* gClientUpdater;

}

#endif //CLIENT_UPDATER_H
