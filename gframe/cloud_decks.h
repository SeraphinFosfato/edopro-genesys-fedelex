#ifndef CLOUD_DECKS_H
#define CLOUD_DECKS_H

// FASE 122B: logica pura dei mazzi nel cloud, senza GUI ne' rete, cosi' si
// prova da sola (tests/cloud_decks_tests.cpp). Forma: design/bot-telegram/cloud-mazzi.md.

#include <string>

namespace ygo {

enum class CloudDeckAction {
	Download,  // il file locale viene (ri)scritto con la versione del cloud
	KeepLocal, // il file locale e' piu' recente: resta com'e'
	Skip,      // stesso contenuto: niente da fare
};

// Regola 3 della forma. Gli orari sono secondi epoch UTC.
// File assente -> Download; contenuto uguale -> Skip; cloud piu' recente
// (strettamente) -> Download; altrimenti KeepLocal.
CloudDeckAction DecideCloudDeck(bool local_exists, long long local_mtime, long long cloud_updated_at, bool same_content);

// "2026-11-18T12:34:56.789Z" -> secondi epoch UTC; -1 se non e' in quella forma.
long long ParseCloudTimestamp(const std::string& iso);

// Il nome dello slot diventa nome di file: true solo se e' sicuro su Windows,
// Linux e Android (niente separatori, caratteri riservati, punto iniziale).
bool CloudDeckNameIsSafeFileName(const std::string& name);

// Nome del file locale (senza .ydk) di uno slot: "[Cloud] <slot>".
std::string CloudDeckFileName(const std::string& slot);

// Nome dello slot per un mazzo locale: toglie il prefisso "[Cloud] " se c'e', altrimenti invariato.
std::string CloudDeckSlotName(const std::string& deck_name);

}

#endif //CLOUD_DECKS_H
