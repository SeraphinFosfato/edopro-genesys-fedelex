// FASE 123C: decisioni del "Chiedi all'Oracolo" fuori dalla GUI.
#include <cstdio>
#include "judge_client.h"

int RunJudgeClientTests();

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

int RunJudgeClientTests() {
	failures = 0;
	const std::string ans = "Colpa di [[Interrupted Kaiju Slumber]] su [[Thunder King]]; ancora [[Thunder King]].";
	const auto seg = JudgeSplitAnswer(ans);
	check(seg.size() == 7 && seg[1].card && seg[1].text == "Interrupted Kaiju Slumber" && !seg[2].card, "split: testo e carte");
	const auto cited = JudgeCitedCards(ans);
	check(cited.size() == 2 && cited[0] == "Interrupted Kaiju Slumber" && cited[1] == "Thunder King", "citate: senza doppioni, in ordine");
	check(JudgeHighlight("a [[X]] b") == "a \xC2\xABX\xC2\xBB b", "evidenziazione con virgolette");
	check(JudgeHighlight("[[aperta") == "[[aperta", "marcatore non chiuso: testo com'e'");
	check(JudgeSplitAnswer("").empty() && JudgeCitedCards("niente").empty(), "vuoto");

	check(JudgeWaitMessage(1, 3) == "Sei in coda: posizione 3.", "tick dispari: posizione");
	check(JudgeWaitMessage(0, 3) != JudgeWaitMessage(2, 3) && JudgeWaitMessage(0, 3).find("posizione") == std::string::npos, "tick pari: battute a rotazione");
	check(JudgeWaitMessage(1, -1).find("posizione") == std::string::npos, "posizione ignota: mai la posizione");

	check(!JudgeButtonVisible(false, true), "senza credenziale: nascosto");
	check(!JudgeButtonVisible(true, false), "fuori da partita/replay: nascosto");
	check(JudgeButtonVisible(true, true), "partita, replay, torneo, spettatore: visibile (decide il bot)");

	check(JudgeErrorMessage("quota").find(L"domande") != std::wstring::npos, "errore quota");
	check(JudgeErrorMessage("bloccato").find(L"Oracolo") != std::wstring::npos && JudgeErrorMessage("paused").find(L"Judge") == std::wstring::npos, "messaggi: nome Oracolo");
	check(JudgeErrorMessage("boh") == L"Qualcosa e' andato storto, riprova.", "errore sconosciuto: generico");

	// errori per codice
	for(const char* c : { "queue_full", "paused", "judge_non_disponibile" }) {
		const auto e = JudgeErrorFor(c, "", 0);
		check(e.kind == JudgeErrorInfo::Occupato && e.retry && !e.blocks_field && !e.bad && e.text.find(L"Oracolo occupato") == 0, "occupato: riprova, non scalata");
	}
	check(JudgeErrorFor("quota", "", 3).text.find(L"0 su 3") != std::wstring::npos && JudgeErrorFor("quota", "", 3).blocks_field, "quota: totale nel testo, blocca il campo");
	check(JudgeErrorFor("oracolo_sospeso", "testo del bot", 0).text == L"testo del bot" && JudgeErrorFor("oracolo_sospeso", "", 0).bad && JudgeErrorFor("oracolo_sospeso", "", 0).intensity == 1.f, "sospeso: testo del bot, rosso, intensita' 1");
	check(JudgeErrorFor("chat_chiusa", "", 0).blocks_field && !JudgeErrorFor("chat_chiusa", "", 0).bad, "chat chiusa: giallo, blocca");
	const auto amb = JudgeErrorFor("ambiguo", "\xC2\xABHarp\xC2\xBB pu\xC3\xB2 essere pi\xC3\xB9 carte", 0);
	check(amb.not_counted && amb.text.find(L"«Harp» può") == 0 && amb.text.find(L"non è stato conteggiato") != std::wstring::npos, "ambiguo: testo del bot (UTF-8) + non conteggiato");
	check(JudgeErrorFor("domanda_non_valida", "", 0).kind == JudgeErrorInfo::DomandaNonValida && JudgeErrorFor("bloccato", "", 0).intensity == 0.8f, "domanda non valida, bloccato");

	// coaching
	check(JudgeCoachingFor("", true).level == JudgeCoachingInfo::None && JudgeCoachingFor("boh", true).level == JudgeCoachingInfo::None, "coaching assente");
	check(JudgeCoachingFor("invito", true).text.find(L"In torneo") == 0 && JudgeCoachingFor("invito", false).text.find(L"torneo") == std::wstring::npos, "invito: fuori torneo non nomina il torneo");
	check(JudgeCoachingFor("avvisati", true).level == JudgeCoachingInfo::Avvisati && JudgeCoachingFor("avvisati", true).text.find(L"judge del torneo") != std::wstring::npos, "avvisati");

	// conto alla rovescia
	const long long t0 = 1000000;
	check(!JudgeCountdownFor(t0, t0 + 49 * 60000).visible, "a 11 minuti: nascosto");
	check(JudgeCountdownFor(t0, t0 + 3600000 - 574000).text == "09:34" && JudgeCountdownFor(t0, t0 + 3600000 - 574000).visible && !JudgeCountdownFor(t0, t0 + 3600000 - 574000).red, "a 9:34: visibile, giallo");
	check(JudgeCountdownFor(t0, t0 + 3600000 - 120000).red && !JudgeCountdownFor(t0, t0 + 3600000 - 121000).red, "rosso da 2:00");
	check(JudgeCountdownFor(t0, t0 + 3600000).closed && !JudgeCountdownFor(t0, t0 + 3600000).visible && !JudgeCountdownFor(-1, t0).visible, "chiusa a 1 h; senza primo messaggio niente");
	check(JudgeParseIsoMs("1970-01-01T00:00:01.250Z") == 1250 && JudgeParseIsoMs("2026-11-26T21:59:45.123Z") == 1795730385123LL && JudgeParseIsoMs("boh") == -1, "created_at ISO");

	// quota
	const auto q = JudgeQuotaFor("free", 2, 3);
	check(q.segments && q.label == L"2 domande su 3 in 7 giorni" && JudgeQuotaFor("free", 1, 3).label == L"1 domanda su 3 in 7 giorni", "quota: etichetta e singolare");
	check(JudgeQuotaFor("pack", 0, 10).finished && !JudgeQuotaFor("pack", 0, 10).segments && JudgeQuotaFor("pack", 4, 10).label == L"4 domande su 10" && !JudgeQuotaFor("pack", 0, 0).known, "quota: pacchetto a barra, finita, ignota");

	// contatore
	check(JudgeCountWords(L"  due  parole\n") == 2 && JudgeCountWords(L"") == 0, "parole");
	check(JudgeCounterFor(L"").text.empty() && JudgeCounterFor(L"ciao mondo").text == L"10/300 · 2/60 parole" && !JudgeCounterFor(L"ciao").over, "contatore");
	check(JudgeCounterFor(std::wstring(301, L'a')).over && JudgeCounterFor(L"a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a").over, "contatore: oltre 300 caratteri o 60 parole");

	// testa e stati
	check(JudgeHeadText(3, 3, false) == L"Istantanea: T3, Main Phase 1" && JudgeHeadText(2, 0, false) == L"Istantanea: T2" && JudgeHeadText(4, 6, true).find(L"punto di visione") != std::wstring::npos, "testa");
	check(JudgeSceneFor(false, 0, false, 3, false) == JudgeScene::Vuota && JudgeSceneFor(true, 0, false, 3, false) == JudgeScene::Scrittura, "stato: vuota, scrittura");
	check(JudgeSceneFor(true, 2, false, 2, false) == JudgeScene::Conversa && JudgeSceneFor(true, 2, true, 2, false) == JudgeScene::Attesa, "stato: conversa, attesa");
	check(JudgeSceneFor(true, 6, false, 0, false) == JudgeScene::Esaurita && JudgeSceneFor(true, 2, false, 2, true) == JudgeScene::Chiusa, "stato: esaurita, chiusa");
	return failures;
}
