#include "client_updater.h"
#if defined(UPDATE_URL) && !EDOPRO_IOS
#include "config.h"
#if EDOPRO_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif EDOPRO_LINUX || EDOPRO_APPLE
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#endif //EDOPRO_WINDOWS
#include "file_stream.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include "MD5/md5.h"
#include "logging.h"
#include "epro_thread.h"
#include "utils.h"
#include "porting.h"
#include "game_config.h"
#include "fmt.h"
#include "curl.h"
#include "sha256.h"
#include "update_verify.h"

// Where this instance's last successfully-applied manifest version is
// recorded (design/client-update.md, anti-rollback). Plain text, one
// integer, same spirit as LOCKFILE just above: local, not sensitive, not
// part of anything that ships. A player who edits this file already
// controls their own client (see this repo's CLAUDE.md, "Onestà sui
// deterrenti") — this file is not a defense against that, only the record
// CompareVersion() checks a NETWORK payload against.
#define UPDATE_VERSION_FILE EPRO_TEXT("./.edopro_update_version")

#define LOCKFILE EPRO_TEXT("./.edopro_lock")
#define UPDATES_FOLDER EPRO_TEXT("./updates/{}")

struct WritePayload {
	std::vector<char>* outbuffer = nullptr;
	std::ostream* outstream = nullptr;
	MD5_CTX* md5context = nullptr;
};

struct Payload {
	update_callback callback = nullptr;
	int current = 1;
	int total = 1;
	bool is_new = true;
	int previous_percent = 0;
	void* payload = nullptr;
	const char* filename = nullptr;
};

template<typename off_type>
static int progress_callback(void* ptr, off_type TotalToDownload, [[maybe_unused]] off_type NowDownloaded, [[maybe_unused]] off_type TotalToUpload, off_type NowUploaded) {
	Payload* payload = static_cast<Payload*>(ptr);
	if(payload && payload->callback) {
		int percentage = 0;
		if(TotalToDownload > static_cast<off_type>(0)) {
			double fractiondownloaded = static_cast<double>(NowDownloaded) / static_cast<double>(TotalToDownload);
			percentage = static_cast<int>(std::round(fractiondownloaded * 100));
		}
		if(percentage != payload->previous_percent) {
			payload->callback(percentage, payload->current, payload->total, payload->filename, payload->is_new, payload->payload);
			payload->is_new = false;
			payload->previous_percent = percentage;
		}
	}
	return 0;
}

static size_t WriteCallback(char *contents, size_t size, size_t nmemb, void *userp) {
	size_t readsize = size * nmemb;
	auto* payload = static_cast<WritePayload*>(userp);
	auto buff = payload->outbuffer;
	if(buff) {
		size_t prev_size = buff->size();
		buff->resize(prev_size + readsize);
		memcpy(buff->data() + prev_size, contents, readsize);
	}
	if(payload->outstream)
		payload->outstream->write(contents, readsize);
	if(payload->md5context)
		MD5_Update(payload->md5context, contents, readsize);
	return readsize;
}

static CURLcode curlPerform(const char* url, void* payload, void* payload2 = nullptr) {
	char curl_error_buffer[CURL_ERROR_SIZE];
	auto curl_handle = curl_easy_init();
	curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error_buffer);
	curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 60L);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, payload);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, ygo::Utils::GetUserAgent().data());
	curl_easy_setopt(curl_handle, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
#if (LIBCURL_VERSION_NUM >= CURL_VERSION_BITS(7,32,0))
	if(curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, progress_callback<curl_off_t>) == CURLE_OK) {
		curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, payload2);
	} else
#endif
	{
		curl_easy_setopt(curl_handle, CURLOPT_PROGRESSFUNCTION, progress_callback<double>);
		curl_easy_setopt(curl_handle, CURLOPT_PROGRESSDATA, payload2);
	}
	curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
	if(ygo::gGameConfig->ssl_certificate_path.size()
	   && ygo::Utils::FileExists(ygo::Utils::ToPathString(ygo::gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl_handle, CURLOPT_CAINFO, ygo::gGameConfig->ssl_certificate_path.data());
	auto res = curl_easy_perform(curl_handle);
	curl_easy_cleanup(curl_handle);
	if(res != CURLE_OK && ygo::gGameConfig->logDownloadErrors)
		ygo::ErrorLog("Curl error: ({}) {} ({})", res, curl_easy_strerror(res), curl_error_buffer);
	return res;
}

namespace ygo {

void ClientUpdater::StartUnzipper(unzip_callback callback, void* payload) {
#if EDOPRO_ANDROID
	porting::installUpdate(epro::format("{}" UPDATES_FOLDER ".apk", Utils::GetWorkingDirectory(), update_urls.front().name));
#else
	if(Lock.acquired())
		epro::thread(&ClientUpdater::Unzip, this, payload, callback).detach();
#endif
}

void ClientUpdater::CheckUpdates() {
	if(Lock.acquired())
		epro::thread(&ClientUpdater::CheckUpdate, this).detach();
}

bool ClientUpdater::StartUpdate(update_callback callback, void* payload) {
	if(!Lock.acquired() || !has_update || downloading)
		return false;
	epro::thread(&ClientUpdater::DownloadUpdate, this, payload, callback).detach();
	return true;
}
void ClientUpdater::Unzip(void* payload, unzip_callback callback) {
	Utils::SetThreadName("Unzip");
#if EDOPRO_WINDOWS || EDOPRO_LINUX
	const auto& path = ygo::Utils::GetExePath();
	ygo::Utils::FileMove(path, epro::format(EPRO_TEXT("{}.old"), path));
#endif
#if EDOPRO_WINDOWS
	const auto& corepath = ygo::Utils::GetCorePath();
	ygo::Utils::FileMove(corepath, epro::format(EPRO_TEXT("{}.old"), corepath));
#endif
	unzip_payload cbpayload{};
	UnzipperPayload uzpl;
	uzpl.payload = payload;
	uzpl.cur = -1;
	uzpl.tot = static_cast<int>(update_urls.size());
	cbpayload.payload = &uzpl;
	int i = 1;
	for(const auto& file : update_urls) {
		uzpl.cur = i++;
		auto name = epro::format(UPDATES_FOLDER, ygo::Utils::ToPathString(file.name));
		uzpl.filename = name.data();
		ygo::Utils::UnzipArchive(name, callback, &cbpayload);
	}
#if EDOPRO_WINDOWS
	if(!Utils::FileExists(corepath)) {
		Utils::FileMove(epro::format(EPRO_TEXT("{}.old"), corepath), corepath);
	}
#endif
	Utils::Reboot();
}

#if EDOPRO_ANDROID
#define formatstr (UPDATES_FOLDER EPRO_TEXT(".apk"))
#else
#define formatstr UPDATES_FOLDER
#endif

// Downloads a SHA-256-verified file into memory (never straight to the
// final path): the whole point of "checks the entire manifest before
// installing anything" (design/client-update.md, point 3 / cancello 4) is
// that a bad file must not leave a partially-installed update on disk. The
// caller decides what to do with the bytes once every file in the manifest
// has verified.
static bool DownloadAndVerify(const std::string& url, const std::string& expected_sha256_lower,
							  Payload& cbpayload, std::string& out_bytes) {
	WritePayload wpayload;
	std::vector<char> buffer;
	wpayload.outbuffer = &buffer;
	if(curlPerform(url.data(), &wpayload, &cbpayload) != CURLE_OK)
		return false;
	out_bytes.assign(buffer.begin(), buffer.end());
	auto actual = ygo::Sha256Hex(out_bytes);
	return actual == expected_sha256_lower;
}

void ClientUpdater::DownloadUpdate(void* payload, update_callback callback) {
	Utils::SetThreadName("Updater");
	downloading = true;
	Payload cbpayload{};
	cbpayload.callback = callback;
	cbpayload.total = static_cast<int>(update_urls.size());
	cbpayload.payload = payload;
	int cur_file = 1;

	// design/client-update.md, point 3 / cancello 4: a SHA-256 mismatch on
	// ANY file rejects the ENTIRE update, not just that file. Download every
	// file into memory first and verify all of them before a single byte
	// touches disk under UPDATES_FOLDER — that is what makes "the whole
	// batch or nothing" true even under a crash or a killed process midway.
	std::vector<std::pair<epro::path_string, std::string>> verified_files; // path -> bytes
	bool any_mismatch = false;
	for(auto& file : update_urls) {
		if(file.sha256.empty()) {
			// A manifest entry without a sha256 never reaches here in
			// practice (update::Parse refuses it, see update_verify.cpp),
			// but this function must not silently install a file it never
			// authenticated if that contract is ever loosened upstream.
			any_mismatch = true;
			ygo::ErrorLog("Aggiornamento: {} non ha una SHA-256 nel manifesto, rifiuto l'intero aggiornamento.", file.name);
			break;
		}
		auto name = epro::format(formatstr, ygo::Utils::ToPathString(file.name));
		cbpayload.current = cur_file++;
		cbpayload.filename = file.name.data();
		cbpayload.is_new = true;
		cbpayload.previous_percent = -1;

		// Already on disk from a previous run and already matches: skip the
		// re-download, but still carry it into verified_files so the "all
		// files verified" invariant below covers it too.
		{
			FileStream stream{ name, FileStream::in | FileStream::binary };
			if(!stream.fail()) {
				std::string existing((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
				if(ygo::Sha256Hex(existing) == file.sha256) {
					verified_files.emplace_back(name, std::move(existing));
					continue;
				}
			}
		}

		std::string bytes;
		if(!DownloadAndVerify(file.url, file.sha256, cbpayload, bytes)) {
			any_mismatch = true;
			ygo::ErrorLog("Aggiornamento: SHA-256 di {} non corrisponde al manifesto, rifiuto l'intero aggiornamento.", file.name);
			break;
		}
		verified_files.emplace_back(name, std::move(bytes));
	}

	if(any_mismatch) {
		failed = true;
		status_message = "Aggiornamento: un file non corrisponde alla SHA-256 firmata, l'intero aggiornamento è stato rifiutato.";
		downloaded = true;
		return;
	}

	// Every file verified — now, and only now, write them to disk.
	for(auto& [name, bytes] : verified_files) {
		if(!ygo::Utils::CreatePath(name)) {
			failed = true;
			continue;
		}
		FileStream stream{ name, FileStream::out | FileStream::binary | FileStream::trunc };
		if(stream.fail()) {
			failed = true;
			continue;
		}
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}

	if(!failed) {
		// The version this manifest declared is now the installed one —
		// recorded before Unzip()/Reboot() so the NEXT check's anti-rollback
		// compares against it correctly (design/client-update.md, anti-rollback).
		SetInstalledVersion(pending_version);
	}
	downloaded = true;
}

int ClientUpdater::GetInstalledVersion() {
	FileStream stream{ UPDATE_VERSION_FILE, FileStream::in };
	if(stream.fail())
		return 0;
	int version = 0;
	stream >> version;
	if(stream.fail())
		return 0;
	return version;
}

void ClientUpdater::SetInstalledVersion(int version) {
	FileStream stream{ UPDATE_VERSION_FILE, FileStream::out | FileStream::trunc };
	if(stream.fail())
		return;
	stream << version;
}

void ClientUpdater::CheckUpdate() {
	Utils::SetThreadName("CheckUpdate");

	// Fail-closed (design/client-update.md, "Fail-closed" / cancello 6): no
	// compiled public key means this updater cannot trust anything it could
	// fetch, so it does not even try, and says so once per check instead of
	// producing a BadSignature log line indistinguishable from a tampered
	// manifest.
	if(!ygo::update::AnyTrustedKeyConfigured()) {
		status_message = "Aggiornamento: nessuna chiave pubblica di aggiornamento compilata, aggiornamento disabilitato.";
		ygo::ErrorLog(status_message);
		return;
	}

	WritePayload payload{};
	std::vector<char> retrieved_data;
	payload.outbuffer = &retrieved_data;
	if(curlPerform(update_url.data(), &payload) != CURLE_OK) {
		// Endpoint irraggiungibile: continua col client attuale, lo dice una
		// volta, senza bloccare (design/client-update.md §5).
		status_message = "Aggiornamento: endpoint non raggiungibile, continuo con il client attuale.";
		ygo::ErrorLog(status_message);
		return;
	}
	const std::string document(retrieved_data.begin(), retrieved_data.end());

	WritePayload sig_payload{};
	std::vector<char> retrieved_signature;
	sig_payload.outbuffer = &retrieved_signature;
	const auto signature_url = update_url + ".sig";
	if(curlPerform(signature_url.data(), &sig_payload) != CURLE_OK) {
		status_message = "Aggiornamento: endpoint non raggiungibile, continuo con il client attuale.";
		ygo::ErrorLog(status_message);
		return;
	}
	const std::string signature(retrieved_signature.begin(), retrieved_signature.end());

	ygo::update::Manifest manifest;
	std::string error;
	// Verifies BEFORE parsing (design/client-update.md, point 1 / cancello
	// 3): VerifyAndParse never lets nlohmann/json see a byte this client has
	// not authenticated first.
	const auto status = ygo::update::VerifyAndParse(document, signature, manifest, error);
	if(status != ygo::update::VerifyStatus::Ok) {
		// Firma non valida/assente, JSON malformato o schema violato sono
		// tutti "rifiuta, tieni il client attuale, lo dice" allo stesso
		// modo (design/client-update.md §5) — un manifesto che non supera
		// la firma non viene mai distinto da uno che la supera ma non
		// rispetta lo schema: in entrambi i casi non è un manifesto valido.
		status_message = "Aggiornamento: manifesto rifiutato (" + error + "), mantengo il client attuale.";
		ygo::ErrorLog(status_message);
		return;
	}

	const int installed_version = GetInstalledVersion();
	const auto decision = ygo::update::CompareVersion(manifest.version, installed_version);
	switch(decision) {
		case ygo::update::VersionDecision::Rollback:
			status_message = epro::format("Aggiornamento: manifesto con versione {} rifiutato come tentativo di downgrade (attuale {}).",
										  manifest.version, installed_version);
			ygo::ErrorLog(status_message);
			return;
		case ygo::update::VersionDecision::AlreadyCurrent:
			// "Non fa niente, senza allarmi" (design/client-update.md §5).
			return;
		case ygo::update::VersionDecision::Accept:
			break;
	}

	update_urls.clear();
	for(const auto& file : manifest.files)
		update_urls.emplace_back(DownloadInfo{ file.name, file.url, file.sha256, file.md5 });
	pending_version = manifest.version;
	status_message = epro::format("Aggiornamento disponibile: versione {} -> versione {}.", installed_version, manifest.version);
	has_update = !update_urls.empty();
}

static inline void DeleteOld() {
#if EDOPRO_WINDOWS || EDOPRO_LINUX
	ygo::Utils::FileDelete(epro::format(EPRO_TEXT("{}.old"), ygo::Utils::GetExePath()));
#endif
#if EDOPRO_WINDOWS
	ygo::Utils::FileDelete(epro::format(EPRO_TEXT("{}.old"), ygo::Utils::GetCorePath()));
#endif
	(void)0;
}

ClientUpdater::ClientUpdater(epro::path_stringview override_url) {
	if(override_url.size())
		update_url = Utils::ToUTF8IfNeeded(override_url);
	if(Lock.acquired())
		DeleteOld();
}
#if EDOPRO_WINDOWS || EDOPRO_LINUX || EDOPRO_MACOS
ClientUpdater::FileLock::FileLock() {
#if EDOPRO_WINDOWS
	m_lock = CreateFile(LOCKFILE, GENERIC_READ,
					  0, nullptr, CREATE_ALWAYS,
					  FILE_ATTRIBUTE_HIDDEN, nullptr);
	if(m_lock == INVALID_HANDLE_VALUE)
		m_lock = null_lock;
#else
	m_lock = open(LOCKFILE, O_CREAT | O_CLOEXEC, S_IRWXU);
	if(m_lock < 0 || flock(m_lock, LOCK_EX | LOCK_NB) != 0) {
		close(m_lock);
		m_lock = null_lock;
	}
#endif
}

ClientUpdater::FileLock::~FileLock() {
	if(m_lock == null_lock)
		return;
#if EDOPRO_WINDOWS
	CloseHandle(m_lock);
#else
	flock(m_lock, LOCK_UN);
	close(m_lock);
#endif
	ygo::Utils::FileDelete(LOCKFILE);
}
#endif

}

#endif //UPDATE_URL
