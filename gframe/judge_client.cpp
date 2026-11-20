#include "judge_client.h"

namespace ygo {

std::vector<JudgeSegment> JudgeSplitAnswer(const std::string& s) {
	std::vector<JudgeSegment> out;
	size_t i = 0;
	while(i < s.size()) {
		const auto open = s.find("[[", i);
		const auto close = open == std::string::npos ? std::string::npos : s.find("]]", open + 2);
		if(close == std::string::npos) {
			out.push_back({ s.substr(i), false });
			break;
		}
		if(open > i)
			out.push_back({ s.substr(i, open - i), false });
		out.push_back({ s.substr(open + 2, close - open - 2), true });
		i = close + 2;
	}
	return out;
}

std::vector<std::string> JudgeCitedCards(const std::string& answer) {
	std::vector<std::string> out;
	for(const auto& seg : JudgeSplitAnswer(answer)) {
		if(!seg.card || seg.text.empty())
			continue;
		bool seen = false;
		for(const auto& n : out)
			seen = seen || n == seg.text;
		if(!seen)
			out.push_back(seg.text);
	}
	return out;
}

std::string JudgeHighlight(const std::string& answer) {
	std::string out;
	for(const auto& seg : JudgeSplitAnswer(answer))
		out += seg.card ? "\xC2\xAB" + seg.text + "\xC2\xBB" : seg.text; // « »
	return out;
}

std::string JudgeWaitMessage(unsigned tick, int position) {
	static const char* const jokes[] = {
		"L'Oracolo sta consultando il regolamento (e un caffe').",
		"L'Oracolo sta contando quante volte hai detto \"in risposta\".",
		"L'Oracolo cerca il paragrafo giusto fra le erratas.",
		"L'Oracolo sta rileggendo la catena al rallentatore.",
		"L'Oracolo chiede conferma al suo gatto. Il gatto non risponde.",
	};
	constexpr unsigned n = sizeof(jokes) / sizeof(jokes[0]);
	if(position >= 0 && tick % 2 == 1)
		return "Sei in coda: posizione " + std::to_string(position) + ".";
	return jokes[(tick / 2) % n];
}

bool JudgeButtonVisible(bool logged, bool inDuelOrReplay, bool isTournamentRoom, const std::string& role) {
	if(!logged || !inDuelOrReplay)
		return false;
	if(!isTournamentRoom)
		return true;
	return role == "judge_assistant" || role == "judge" || role == "master" || role == "grandmaster";
}

std::wstring JudgeErrorMessage(const std::string& code) {
	if(code == "paused")
		return L"L'Oracolo e' in pausa, riprova piu' tardi.";
	if(code == "quota")
		return L"Hai finito le domande a disposizione per questo periodo.";
	if(code == "queue_full")
		return L"L'Oracolo e' molto richiesto, riprova fra poco.";
	if(code == "not_allowed")
		return L"In una partita di torneo l'Oracolo e' riservato ai judge che guardano da spettatori.";
	if(code == "bloccato")
		return L"L'Oracolo e' bloccato per questo mese: troppe domande fuori tema.";
	if(code == "judge_non_disponibile")
		return L"L'Oracolo non e' disponibile al momento.";
	if(code == "replay_non_valido")
		return L"Non riesco a leggere questa partita.";
	if(code == "domanda_non_valida")
		return L"Scrivi una domanda (al massimo 1000 caratteri).";
	return L"Qualcosa e' andato storto, riprova.";
}

}
