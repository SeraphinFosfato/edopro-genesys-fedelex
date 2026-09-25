#include "title_checkin.h"

#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "curl.h"
#include "game_config.h"
#include "logging.h"
#include "title_store.h"
#include "utils.h"

namespace ygo {

TitleCheckin* gTitleCheckin = nullptr;

namespace {

// Small on purpose: a real title/revocation/suspension body is a handful of
// fields (a user_ref, one to three ISO timestamps, a base64 signature) —
// a few hundred bytes. This is a hard ceiling against a hostile or broken
// endpoint streaming forever, not a realistic budget, same reasoning as
// banlist_updater.cpp's MAX_BANLIST_BODY_SIZE.
constexpr size_t MAX_TITLE_BODY_SIZE = 64u * 1024u;

struct FetchBuffer {
	std::string* out;
	bool too_large = false;
};

size_t FetchWriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
	auto* buffer = static_cast<FetchBuffer*>(userp);
	const size_t added = size * nmemb;
	if(buffer->out->size() + added > MAX_TITLE_BODY_SIZE) {
		buffer->too_large = true;
		return 0; // returning less than `added` tells curl to abort the transfer
	}
	buffer->out->append(contents, added);
	return added;
}

}

TitleCheckin::TitleCheckin(std::string override_url) {
	if(!override_url.empty())
		base_url = std::move(override_url);
}

TitleCheckin::~TitleCheckin() {
	Join();
}

void TitleCheckin::Start() {
	// Model: BanlistUpdater::StartCheck — spawn, do not block startup.
	// Unlike that one-shot check, Loop() itself never returns until Join().
	worker_ = epro::thread(&TitleCheckin::Loop, this);
}

void TitleCheckin::Join() {
	{
		std::lock_guard<epro::mutex> lock(wake_mutex_);
		stop_ = true;
	}
	wake_cv_.notify_all();
	if(worker_.joinable())
		worker_.join();
	// Stessa ragione del worker periodico: una verifica interattiva ancora
	// in volo mentre il processo si smonta scriverebbe in campi gia' morti.
	// Non c'e' niente da segnalarle — al massimo si aspettano i dieci
	// secondi del suo timeout, che e' il tetto per costruzione.
	if(credential_worker_.joinable())
		credential_worker_.join();
}

void TitleCheckin::SetInDuel(bool in_duel) {
	const bool changed = in_duel_.exchange(in_duel, std::memory_order_relaxed) != in_duel;
	if(!changed)
		return;
	// Holding the lock around notify_all isn't strictly required for
	// correctness here (Loop() always re-reads in_duel_ fresh at the top of
	// its wait, so a "lost" notify only costs one stale-cadence sleep, never
	// a wrong decision), but it costs nothing and removes any doubt about
	// the classic lost-wakeup race.
	std::lock_guard<epro::mutex> lock(wake_mutex_);
	wake_cv_.notify_all();
}

void TitleCheckin::Loop() {
	// Capped at 15 visible characters + null (Utils::SetThreadName
	// static_asserts <= 16 bytes total — the posix pthread_setname_np limit).
	Utils::SetThreadName("Title checkin");

	CheckOnce(); // startup (§2)

	std::unique_lock<epro::mutex> lock(wake_mutex_);
	while(!stop_) {
		// Re-read in_duel_ fresh on every iteration — a SetInDuel() call
		// notifies mid-wait specifically so this recomputes immediately
		// instead of finishing out whatever was left of the previous
		// interval (see the class comment in title_checkin.h).
		const auto interval = std::chrono::minutes(in_duel_.load(std::memory_order_relaxed) ? 5 : 15);
		// No predicate on this wait: a spurious or deliberate notify (stop,
		// or an isInDuel transition) must break out immediately so the loop
		// can recompute `interval` on the spot — the predicate overload
		// would silently absorb that wakeup and keep sleeping.
		wake_cv_.wait_for(lock, interval);
		if(stop_)
			break;
		lock.unlock();
		CheckOnce();
		lock.lock();
	}
}

void TitleCheckin::CheckOnce() {
	// Read fresh every attempt: the player can paste a credential in at any
	// time via the options UI, and an empty one means nothing to check in
	// with yet (§12) — not an error, just nothing to do this cycle.
	const std::string credential = gGameConfig->titleCredential;
	if(credential.empty())
		return;

	PostResult result;
	if(!Post(base_url, credential, 20L, result))
		return; // transport failure (unreachable/timeout/TLS): D102, no-op

	// D102's status allowlist, enforced right here: only a signed 200
	// (title) or 403 (revocation/suspension) body ever reaches the store.
	// 401 ("unknown_credential"), 404, 429 (rate limited) and any 5xx are
	// never signed by design (server.ts) — treating them as a no-op is not
	// a workaround, it is the only thing they could ever safely mean.
	if(result.status_code != 200 && result.status_code != 403)
		return;

	gTitleStore->ApplyResponse(result.body);
}

void TitleCheckin::StartCredentialCheck(std::string credential) {
	{
		std::lock_guard<epro::mutex> lock(credential_mutex_);
		if(credential_running_)
			return; // una verifica alla volta: il bottone e' gia' disabilitato, questa e' la rete
		credential_running_ = true;
		credential_ready_ = false;
	}
	// Il worker precedente ha gia' finito (credential_running_ era false) ma
	// puo' essere ancora joinable: join qui, non nel distruttore soltanto,
	// altrimenti il secondo tentativo assegnerebbe sopra un thread vivo.
	if(credential_worker_.joinable())
		credential_worker_.join();
	credential_worker_ = epro::thread(&TitleCheckin::CredentialCheckTask, this, std::move(credential));
}

bool TitleCheckin::CredentialCheckRunning() const {
	std::lock_guard<epro::mutex> lock(credential_mutex_);
	return credential_running_;
}

bool TitleCheckin::TakeCredentialCheckResult(CredentialCheckResult& out) {
	std::lock_guard<epro::mutex> lock(credential_mutex_);
	if(!credential_ready_)
		return false;
	credential_ready_ = false;
	out = std::move(credential_result_);
	credential_result_ = CredentialCheckResult{};
	return true;
}

void TitleCheckin::CredentialCheckTask(std::string credential) {
	Utils::SetThreadName("Title verify");

	CredentialCheckResult result;
	PostResult post;
	// I dieci secondi sono il totale, non il connect (vedi Post): un bot che
	// risponde all'undicesimo secondo e' un fallimento, e il giocatore riceve
	// comunque una frase invece di una finestra ferma.
	if(!Post(base_url, credential, 10L, post)) {
		result.verdict = CredentialVerdict::Unreachable;
	} else switch(post.status_code) {
	case 200:
		result.verdict = CredentialVerdict::Accepted;
		result.body = std::move(post.body);
		break;
	case 403:
		// Revoca o sospensione: e' firmata come un titolo, quindi si applica
		// e si salva. Il giocatore ha una chiave vera, solo bloccata.
		result.verdict = CredentialVerdict::Blocked;
		result.body = std::move(post.body);
		break;
	case 401:
		// L'unico caso in cui sappiamo con certezza che la stringa e'
		// sbagliata, e quindi l'unico che non salva: tenerla vuol dire
		// riproporla al giocatore la volta dopo, cioe' il giro che ha
		// bruciato una serata al tester del 25.
		result.verdict = CredentialVerdict::Unknown;
		break;
	case 429:
		result.verdict = CredentialVerdict::TooManyTries;
		break;
	default:
		// 404, 5xx, qualunque altra cosa: non sappiamo se la chiave sia
		// buona, quindi si salva e si riprova da soli col check-in di fondo.
		result.verdict = CredentialVerdict::Unreachable;
		break;
	}

	std::lock_guard<epro::mutex> lock(credential_mutex_);
	credential_result_ = std::move(result);
	credential_ready_ = true;
	credential_running_ = false;
}

bool TitleCheckin::Post(const std::string& url, const std::string& credential, long timeout_seconds, PostResult& out) {
	out = PostResult{};
	char error_buffer[CURL_ERROR_SIZE]{};
	auto curl_handle = curl_easy_init();
	FetchBuffer buffer{ &out.body };

	// {"credential": "..."} via nlohmann rather than hand-built string
	// concatenation: the credential is an opaque, player-pasted value this
	// client never validates the shape of, so it must go through a real
	// JSON string escaper, not string + "\"" + credential + "\"".
	const std::string body = nlohmann::json{ { "credential", credential } }.dump();

	curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

	curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(curl_handle, CURLOPT_URL, url.data());
	curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, body.data());
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, FetchWriteCallback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
	// Short timeouts on purpose (same reasoning as BanlistUpdater::Fetch):
	// a background check-in must never make a player wait.
	// CONNECTTIMEOUT non puo' superare il totale, altrimenti un connect che
	// si pianta mangerebbe da solo la finestra dei dieci secondi che 27.2
	// promette al giocatore: il totale e' TIMEOUT, e comanda lui.
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, timeout_seconds);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, Utils::GetUserAgent().data());
	curl_easy_setopt(curl_handle, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	if(gGameConfig->ssl_certificate_path.size()
	   && Utils::FileExists(Utils::ToPathString(gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl_handle, CURLOPT_CAINFO, gGameConfig->ssl_certificate_path.data());

	// Deliberately NOT CURLOPT_FAILONERROR: unlike the banlist GET (which
	// only ever cares about success), this call must distinguish 200 from
	// 403 from everything else, so the HTTP status has to reach CheckOnce()
	// untouched instead of being folded into a single curl-level failure.
	const auto res = curl_easy_perform(curl_handle);
	if(res == CURLE_OK) {
		long status = 0;
		curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &status);
		out.status_code = status;
		out.transport_ok = true;
	} else if(gGameConfig->logDownloadErrors) {
		ErrorLog("Title check-in curl error: ({}) {} ({})", res, curl_easy_strerror(res), error_buffer);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl_handle);

	if(buffer.too_large) {
		// An oversized body is exactly as untrustworthy as a transport
		// failure: something is wrong with this response, and D102 says
		// "wrong" and "unreachable" are handled the same way — no-op.
		out.body.clear();
		out.transport_ok = false;
	}
	return out.transport_ok;
}

}
