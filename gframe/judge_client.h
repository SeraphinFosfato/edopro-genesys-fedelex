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

// Il bottone c'e' solo con credenziale, in partita o replay; in una stanza di torneo solo ai judge.
bool JudgeButtonVisible(bool logged, bool inDuelOrReplay, bool isTournamentRoom, const std::string& role);

// Testo per l'utente di un errore del bot, per codice (i codici sono quelli del contratto).
std::wstring JudgeErrorMessage(const std::string& code);

}

#endif //JUDGE_CLIENT_H
