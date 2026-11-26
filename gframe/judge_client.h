#ifndef JUDGE_CLIENT_H
#define JUDGE_CLIENT_H

// FASE 123C: logica del "Chiedi al Judge" fuori dalla GUI (nessuna dipendenza da irrlicht/curl),
// cosi' ogni decisione si prova in tests/judge_client_tests.cpp. Contratto: design/judge/ponte.md
// (nel vault, non qui: questo file descrive solo cio' che fa il client).

#include <string>
#include <vector>

namespace ygo {

struct JudgeSegment {
	std::string text;
	bool card; // true = nome di carta (era fra [[ ]])
};

// Spezza la risposta del bot in testo e nomi di carta (marcati [[Nome]]).
std::vector<JudgeSegment> JudgeSplitAnswer(const std::string& answer);
// Nomi di carta citati, senza doppioni, nell'ordine della prima citazione.
std::vector<std::string> JudgeCitedCards(const std::string& answer);
// Testo mostrato: i nomi di carta evidenziati con «».
std::string JudgeHighlight(const std::string& answer);

// Messaggio d'attesa: a tick pari una battuta a rotazione, a tick dispari la posizione in coda
// (position < 0 = sconosciuta: sempre la battuta).
std::string JudgeWaitMessage(unsigned tick, int position);

// FASE 124d: la scheda «Judge Chat» c'e' solo con credenziale, in partita o replay. Anche in torneo: decide il bot.
bool JudgeButtonVisible(bool logged, bool inDuelOrReplay);

// Testo per l'utente di un errore del bot, per codice (i codici sono quelli del contratto).
std::wstring JudgeErrorMessage(const std::string& code);

// Come si presenta un errore: testo (quello del bot dove esiste), gravita', cosa permette, intensita' del momento `errore`.
struct JudgeErrorInfo {
	enum Kind { Occupato, Quota, Sospeso, Bloccato, Chiusa, Ambiguo, DomandaNonValida, Altro };
	Kind kind = Altro;
	std::wstring text;
	bool bad = false; // rosso; altrimenti giallo
	bool retry = false; // «Riprova»
	bool blocks_field = false; // al posto del campo di scrittura
	bool not_counted = false; // il messaggio non e' stato conteggiato
	float intensity = 0.f; // evento momento `errore`
};
// `code` = campo "error" del bot, `bot_message` = campo "messaggio" (puo' essere vuoto), `quota_total` = totale quota noto (0 = ignoto).
JudgeErrorInfo JudgeErrorFor(const std::string& code, const std::string& bot_message, int quota_total);

// Coaching di una risposta: `coaching` = null/""/"invito"/"avvisati". Gialla per l'invito, rossa per gli avvisati.
struct JudgeCoachingInfo {
	enum Level { None, Invito, Avvisati };
	Level level = None;
	std::wstring text;
	float intensity = 0.f;
};
JudgeCoachingInfo JudgeCoachingFor(const std::string& coaching, bool tournament);

// Conto alla rovescia: la chat si chiude 1 h dopo il primo messaggio; si vede solo negli ultimi 10 minuti.
struct JudgeCountdown {
	bool visible = false;
	bool closed = false;
	bool red = false; // sotto 2:00 (lampeggia, salvo animazioni ridotte)
	long long seconds_left = 0;
	std::string text; // "09:34"
};
JudgeCountdown JudgeCountdownFor(long long first_created_ms, long long now_ms);
// created_at del bot ("2026-11-26T21:59:45.123Z") in millisecondi dall'epoca; -1 se non si legge.
long long JudgeParseIsoMs(const std::string& iso);

// Barretta della quota: segmenti (fino a 5) oppure barra; testo accanto.
struct JudgeQuotaView {
	bool known = false;
	int left = 0, total = 0;
	bool segments = false;
	bool finished = false;
	std::wstring label;
};
JudgeQuotaView JudgeQuotaFor(const std::string& plan, int left, int total);

// Contatore sotto il campo: solo indicativo, decide il bot.
int JudgeCountWords(const std::wstring& text);
struct JudgeCounter {
	std::wstring text; // "12/300 · 3/60 parole", vuoto se il campo e' vuoto
	bool over = false;
};
JudgeCounter JudgeCounterFor(const std::wstring& text);

// Testa della scheda: «Istantanea: T3, Main Phase 1». hud_phase come in DuelInfo (0 = sconosciuta).
std::wstring JudgeHeadText(int turn, int hud_phase, bool replay);

// Stato della scheda, dedotto dai fatti.
enum class JudgeScene { Vuota, Scrittura, Conversa, Attesa, Esaurita, Chiusa };
JudgeScene JudgeSceneFor(bool snapshot_taken, int messages_shown, bool waiting, int messages_left, bool closed);
}

#endif //JUDGE_CLIENT_H
