#ifndef TITLE_CHECKIN_H
#define TITLE_CHECKIN_H

// Background check-in loop against the title API (D78-D80, D102). Talks to
// TitleStore (title_store.h) with whatever it gets back; the network and
// scheduling live here, the verification and the access decision do not.
//
// Cadence (design/fork-edopro/access-control.md §2): once immediately at
// startup, then every 15 minutes at rest or every 5 minutes while
// dInfo.isInDuel — this class does not know what a duel is (no game.h, no
// irrlicht dependency), so the main thread tells it via SetInDuel() at
// whatever call sites already handle entering/leaving a duel.
//
// D102 is enforced right here, at the network boundary: ONLY a completed
// HTTP round-trip whose status is 200 or 403 ever reaches
// TitleStore::ApplyResponse(). A transport failure (unreachable, timeout,
// TLS error) or any other status (401/404/429/5xx) is a plain no-op — never
// even handed to the store, so there is no code path in this class that
// could accidentally let an unsigned response degrade the player's access.

#include <atomic>
#include <string>
#include "epro_condition_variable.h"
#include "epro_mutex.h"
#include "epro_thread.h"

#ifndef TITLE_API_URL
#define TITLE_API_URL "https://cuoremeccanico.quoll-ruffe.ts.net/v1/title"
#endif

namespace ygo {

class TitleCheckin {
public:
	explicit TitleCheckin(std::string override_url = {});
	// A detached-in-spirit worker still POSTing while the process tears
	// down is how a half-applied response outlives the run (same reasoning
	// as BanlistUpdater's destructor) — join it here instead.
	~TitleCheckin();

	// Spawns the worker: an immediate check-in, then the periodic loop.
	// Non-blocking, same shape as BanlistUpdater::StartCheck.
	void Start();
	// Signals the loop to stop and joins it. Safe to call even if Start()
	// was never called (worker is simply not joinable yet).
	void Join();

	// Called from wherever the main thread already knows a duel just
	// started or ended. Wakes the loop immediately so a transition INTO a
	// duel starts using the 5-minute cadence right away, instead of
	// finishing out whatever was left of a 15-minute rest wait first.
	void SetInDuel(bool in_duel);

	// FASE 27.2 — la verifica al salvataggio.
	//
	// Perche' esiste: il 2026-09-25 un tester ha passato ore a incollare la
	// stringa sbagliata (il proprio codice giocatore invece della chiave di
	// licenza). Il salvataggio non verificava niente, e una chiave sbagliata
	// finisce in AccessState::NoTitle, che e' l'unico stato che NON produce
	// nessuna notifica (game.cpp): tre silenzi in fila. Qui la chiave si
	// prova subito, una volta, e l'esito si dice sempre.
	//
	// Questo e' l'unico punto del file che NON segue D102: il check-in di
	// fondo scarta tutto cio' che non e' 200/403 perche' non ha nessuno a cui
	// parlare, mentre qui un 401/429/timeout e' esattamente il dato che
	// l'utente sta aspettando. La regola di D102 resta comunque intatta dove
	// conta: solo un 200 o un 403 porta un `body`, e solo quel body puo'
	// arrivare a TitleStore::ApplyResponse() — gli altri verdetti non ne
	// hanno nessuno da consegnare.
	enum class CredentialVerdict {
		Accepted,     // 200 — titolo firmato
		Blocked,      // 403 — revoca/sospensione, firmata pure quella
		Unknown,      // 401 — la chiave non esiste: l'unico caso in cui si sa che e' sbagliata
		TooManyTries, // 429
		Unreachable,  // timeout, rete giu', 5xx, qualunque altro stato
	};
	struct CredentialCheckResult {
		CredentialVerdict verdict = CredentialVerdict::Unreachable;
		std::string body; // valorizzato solo per Accepted/Blocked
	};

	// Avvia la verifica su un worker e torna subito. Un worker invece di una
	// chiamata sincrona non e' una scelta di comodo: dieci secondi dentro il
	// thread della UI congelano anche il disegno, quindi la frase "sto
	// verificando" che 27.2 chiede non verrebbe mai dipinta — la finestra
	// resterebbe immobile e muta, cioe' il difetto che questa fase toglie.
	// La forma dell'interazione resta quella del brief: finestra aperta,
	// bottoni disabilitati, una frase alla fine. Vedi il punto di risalita
	// scritto in PHASES.md FASE 27.
	void StartCredentialCheck(std::string credential);
	// true dal momento in cui StartCredentialCheck parte fino a quando il
	// risultato non e' stato ritirato.
	bool CredentialCheckRunning() const;
	// Consegna il risultato una volta sola (main thread, polling dal
	// MainLoop come per Generation()): true se c'era qualcosa da ritirare.
	bool TakeCredentialCheckResult(CredentialCheckResult& out);

private:
	void Loop();
	// One check-in attempt: POSTs the stored credential, and — only for a
	// completed exchange whose status is 200 or 403 — hands the raw body to
	// gTitleStore->ApplyResponse(). Every other outcome is a silent no-op
	// (D102); see the class comment above.
	void CheckOnce();

	struct PostResult {
		bool transport_ok = false;
		long status_code = 0;
		std::string body;
	};
	// POSTs {"credential": credential} as application/json to `url`.
	// transport_ok is false only for a transport-level failure (DNS,
	// connect, TLS, timeout) — a completed exchange that came back 4xx/5xx
	// still sets transport_ok true, with that status_code and whatever body
	// the server sent; CheckOnce is what decides D102's status allowlist,
	// not this function.
	// `timeout_seconds` e' il totale della richiesta (CURLOPT_TIMEOUT), non
	// il solo connect: il check-in di fondo puo' permettersi di aspettare,
	// la verifica interattiva no — dieci secondi e basta, valore dato
	// dall'utente il 2026-09-25 (FASE 27.2).
	static bool Post(const std::string& url, const std::string& credential, long timeout_seconds, PostResult& out);

	void CredentialCheckTask(std::string credential);

	std::string base_url{ TITLE_API_URL };
	epro::thread worker_;
	epro::thread credential_worker_;
	mutable epro::mutex credential_mutex_;
	bool credential_running_ = false; // guarded by credential_mutex_
	bool credential_ready_ = false;   // guarded by credential_mutex_
	CredentialCheckResult credential_result_; // guarded by credential_mutex_
	epro::mutex wake_mutex_;
	epro::condition_variable wake_cv_;
	std::atomic<bool> in_duel_{ false };
	bool stop_ = false; // guarded by wake_mutex_
};

extern TitleCheckin* gTitleCheckin;

}

#endif //TITLE_CHECKIN_H
