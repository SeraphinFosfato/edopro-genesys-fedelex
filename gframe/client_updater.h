#ifndef CLIENT_UPDATER_H
#define CLIENT_UPDATER_H

#include "config.h"
#if defined(UPDATE_URL) && !EDOPRO_IOS
#include <vector>
#include <atomic>
#include <string>
#endif
#include "utils.h"

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
	// design/client-update.md §9 (FASE 36): true once a verified manifest has
	// declared a min_supported this build's CLIENT_UPDATE_VERSION does not
	// meet. The single call site that must honor this is
	// ServerLobby::JoinServer (server_lobby.cpp) — it covers both "ospita
	// online" and "entra in una stanza online"; local play, vs AI and replay
	// never go through it and stay available regardless of this flag.
	bool OnlineDisabled() const {
		return online_disabled;
	}
	// Populated together with online_disabled, above: the player-facing
	// reason, always naming the versions involved (never a bare "disabled").
	const std::string& GetOnlineDisabledReason() const {
		return online_disabled_reason;
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
	// See OnlineDisabled()/GetOnlineDisabledReason() above. Defaults to
	// "not disabled": a launch that never reaches a verified manifest (no
	// UPDATE_URL reply, bad signature, unreachable endpoint) leaves online
	// play exactly as available as it always was — the gate only closes on
	// an explicit, verified min_supported that this build does not meet,
	// never on the absence of information.
	std::atomic<bool> online_disabled{ false };
	std::string online_disabled_reason;
};
#else
class ClientUpdater {
public:
	ClientUpdater(epro::path_stringview) {}
	~ClientUpdater() = default;
	static constexpr bool StartUpdate(update_callback, void*) { return false; }
	static constexpr void StartUnzipper(unzip_callback, void*) {}
	static constexpr void CheckUpdates() {}
	static constexpr bool HasUpdate() { return false; }
	static constexpr bool UpdateDownloaded() { return false; }
	static constexpr bool UpdateFailed() { return true; }
	static epro::stringview GetStatusMessage() { return {}; }
	// A build without UPDATE_URL never fetches a manifest, so it never
	// learns of a min_supported floor: online play stays exactly as
	// available as it always was in this build.
	static constexpr bool OnlineDisabled() { return false; }
	static epro::stringview GetOnlineDisabledReason() { return {}; }
};
#endif

extern ClientUpdater* gClientUpdater;

}

#endif //CLIENT_UPDATER_H
