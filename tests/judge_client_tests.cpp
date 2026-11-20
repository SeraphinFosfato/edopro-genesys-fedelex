// FASE 123C: decisioni del "Chiedi al Judge" fuori dalla GUI.
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

	check(!JudgeButtonVisible(false, true, false, "judge"), "senza credenziale: nascosto");
	check(!JudgeButtonVisible(true, false, false, "judge"), "fuori da partita/replay: nascosto");
	check(JudgeButtonVisible(true, true, false, "player"), "partita normale: visibile a tutti");
	check(!JudgeButtonVisible(true, true, true, "player"), "torneo, giocatore: nascosto");
	check(JudgeButtonVisible(true, true, true, "judge_assistant") && JudgeButtonVisible(true, true, true, "grandmaster"), "torneo, judge: visibile");

	check(JudgeErrorMessage("quota").find(L"domande") != std::wstring::npos, "errore quota");
	check(JudgeErrorMessage("boh") == L"Qualcosa e' andato storto, riprova.", "errore sconosciuto: generico");
	return failures;
}
