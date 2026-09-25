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
	// Reads the last version this instance successfully installed. 0 if the
	// file is absent, which is not a chosen threshold (§ "punti di
	// risalita" excludes exactly this): it is simply "no prior update
	// applied by this updater", and any manifest.version >= 1 (every real
	// manifest, by the same convention format_version already uses on the
	// banlist side) compares as newer than that.
	static int GetInstalledVersion();
	static void SetInstalledVersion(int version);

	std::vector<DownloadInfo> update_urls;
	FileLock Lock{};
	std::atomic<bool> has_update{ false };
	std::atomic<bool> downloaded{ false };
	std::atomic<bool> failed{ false };
	std::atomic<bool> downloading{ false };
	std::string update_url{ UPDATE_URL };
	int pending_version = 0;
	std::string status_message;
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
};
#endif

extern ClientUpdater* gClientUpdater;

}

#endif //CLIENT_UPDATER_H
