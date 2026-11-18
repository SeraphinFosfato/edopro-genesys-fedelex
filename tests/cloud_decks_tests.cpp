// FASE 122B: decisione di sincronizzazione dei mazzi nel cloud, fuori dalla GUI.
#include <cstdio>
#include "cloud_decks.h"

int RunCloudDecksTests();

using namespace ygo;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
	if(!ok) {
		++failures;
		std::printf("  FAIL  %s\n", what);
	}
}
}

int RunCloudDecksTests() {
	failures = 0;
	check(DecideCloudDeck(false, 0, 100, false) == CloudDeckAction::Download, "file assente: scarica");
	check(DecideCloudDeck(false, 0, 100, true) == CloudDeckAction::Download, "file assente: scarica anche se same_content e' vero per errore");
	check(DecideCloudDeck(true, 50, 100, false) == CloudDeckAction::Download, "cloud piu' nuovo: scarica");
	check(DecideCloudDeck(true, 200, 100, false) == CloudDeckAction::KeepLocal, "locale piu' nuovo: resta");
	check(DecideCloudDeck(true, 100, 100, false) == CloudDeckAction::KeepLocal, "stesso istante, contenuto diverso: resta il locale");
	check(DecideCloudDeck(true, 50, 100, true) == CloudDeckAction::Skip, "sha uguale, cloud piu' nuovo: niente");
	check(DecideCloudDeck(true, 200, 100, true) == CloudDeckAction::Skip, "sha uguale, locale piu' nuovo: niente");

	check(ParseCloudTimestamp("1970-01-01T00:00:00.000Z") == 0, "epoch zero");
	check(ParseCloudTimestamp("2026-11-18T12:34:56.789Z") == 1795005296LL, "data reale");
	check(ParseCloudTimestamp("2000-03-01T00:00:00Z") == 951868800LL, "dopo il 29 febbraio");
	check(ParseCloudTimestamp("") == -1 && ParseCloudTimestamp("ieri") == -1, "testo non valido");
	check(ParseCloudTimestamp("2026-13-18T12:34:56Z") == -1, "mese fuori range");

	check(CloudDeckNameIsSafeFileName("Mio mazzo 1"), "nome normale");
	check(!CloudDeckNameIsSafeFileName("") && !CloudDeckNameIsSafeFileName("a/b") && !CloudDeckNameIsSafeFileName("..") && !CloudDeckNameIsSafeFileName("a:b"), "nomi pericolosi");
	check(!CloudDeckNameIsSafeFileName(std::string(65, 'a')), "oltre 64");
	check(CloudDeckFileName("Mio mazzo") == "[Cloud] Mio mazzo", "slot -> file");
	check(CloudDeckSlotName("[Cloud] Mio mazzo") == "Mio mazzo", "file -> slot");
	check(CloudDeckSlotName("Altro") == "Altro" && CloudDeckSlotName("[Cloud]x") == "[Cloud]x", "senza prefisso: invariato");
	check(CloudDeckSlotName(CloudDeckFileName("a b")) == "a b", "andata e ritorno");
	std::printf("cloud_decks_tests: %d failures\n", failures);
	return failures;
}
