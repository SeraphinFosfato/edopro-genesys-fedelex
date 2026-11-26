#include "judge_client.h"
#include <cstdio>

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

bool JudgeButtonVisible(bool logged, bool inDuelOrReplay) {
	return logged && inDuelOrReplay;
}

std::wstring JudgeErrorMessage(const std::string& code) {
	return JudgeErrorFor(code, "", 0).text;
}

JudgeErrorInfo JudgeErrorFor(const std::string& code, const std::string& bot_message, int quota_total) {
	JudgeErrorInfo e;
	// il testo del bot e' UTF-8: lo si converte a mano (niente dipendenze da irrlicht qui)
	const auto utf8 = [](const std::string& s) {
		std::wstring out;
		for(size_t i = 0; i < s.size();) {
			const unsigned char c = s[i];
			const int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
			wchar_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
			for(int k = 1; k < n && i + k < s.size(); ++k)
				cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
			out.push_back(cp);
			i += n;
		}
		return out;
	};
	const std::wstring bot = utf8(bot_message);
	if(code == "queue_full" || code == "paused" || code == "judge_non_disponibile" || code == "occupato") {
		e.kind = JudgeErrorInfo::Occupato;
		e.text = L"Oracolo occupato. Riprova fra qualche secondo: questa domanda non ti è stata scalata.";
		e.retry = true;
		e.intensity = 0.2f;
	} else if(code == "quota") {
		e.kind = JudgeErrorInfo::Quota;
		e.text = quota_total > 0
			? L"Hai finito le domande a disposizione (0 su " + std::to_wstring(quota_total) + L" negli ultimi 7 giorni). Altre domande dalla Mini App."
			: L"Hai finito le domande a disposizione. Altre domande dalla Mini App.";
		e.bad = e.blocks_field = true;
		e.intensity = 0.4f;
	} else if(code == "oracolo_sospeso") {
		e.kind = JudgeErrorInfo::Sospeso;
		e.text = !bot.empty() ? bot : L"L'Oracolo è sospeso per il tuo account mentre lo staff esamina un tuo messaggio. Decide una persona dello staff.";
		e.bad = e.blocks_field = true;
		e.intensity = 1.f;
	} else if(code == "bloccato") {
		e.kind = JudgeErrorInfo::Bloccato;
		e.text = L"L'Oracolo è bloccato per questo mese: troppe domande fuori tema.";
		e.bad = e.blocks_field = true;
		e.intensity = 0.8f;
	} else if(code == "chat_chiusa") {
		e.kind = JudgeErrorInfo::Chiusa;
		e.text = L"Questa chat è chiusa: è passata un'ora dalla prima domanda. Puoi rileggerla fino a fine partita.";
		e.blocks_field = true;
		e.intensity = 0.4f;
	} else if(code == "ambiguo") {
		e.kind = JudgeErrorInfo::Ambiguo;
		e.text = (bot.empty() ? L"Il nome può essere più di una carta: scrivi il nome intero." : bot) + L" Il messaggio non è stato conteggiato.";
		e.not_counted = true;
		e.intensity = 0.2f;
	} else if(code == "domanda_non_valida") {
		e.kind = JudgeErrorInfo::DomandaNonValida;
		e.text = L"Scrivi una domanda (al massimo 300 caratteri e 60 parole).";
	} else if(code == "replay_non_valido") {
		e.text = L"Non riesco a leggere questa partita.";
	} else {
		e.text = L"Qualcosa e' andato storto, riprova.";
	}
	return e;
}

JudgeCoachingInfo JudgeCoachingFor(const std::string& coaching, bool tournament) {
	JudgeCoachingInfo c;
	if(coaching == "invito") {
		c.level = JudgeCoachingInfo::Invito;
		c.text = std::wstring(tournament ? L"In torneo l'" : L"L'") + L"Oracolo non d\u00e0 consigli e non dice se una mossa si pu\u00f2 fare o conviene. Puoi chiedere cosa \u00e8 successo e perch\u00e9: prova con un'altra domanda.";
		c.intensity = 0.5f;
	} else if(coaching == "avvisati") {
		c.level = JudgeCoachingInfo::Avvisati;
		c.text = L"Di nuovo una richiesta di consiglio. Il tuo avversario è stato avvisato e il judge del torneo è stato allertato. Puoi chiedere cosa è successo e perché.";
		c.intensity = 0.8f;
	}
	return c;
}

JudgeCountdown JudgeCountdownFor(long long first_created_ms, long long now_ms) {
	JudgeCountdown c;
	if(first_created_ms < 0)
		return c;
	const long long left_ms = first_created_ms + 3600LL * 1000 - now_ms;
	if(left_ms <= 0) {
		c.closed = true;
		return c;
	}
	c.seconds_left = (left_ms + 999) / 1000;
	c.visible = c.seconds_left <= 600;
	c.red = c.seconds_left <= 120;
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%02lld:%02lld", c.seconds_left / 60, c.seconds_left % 60);
	c.text = buf;
	return c;
}

long long JudgeParseIsoMs(const std::string& s) {
	int y, mo, d, h, mi, sec;
	if(s.size() < 19 || std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6)
		return -1;
	// giorni dall'epoca (algoritmo di H. Hinnant)
	y -= mo <= 2;
	const long long era = (y >= 0 ? y : y - 399) / 400;
	const long long yoe = y - era * 400;
	const long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long long days = era * 146097 + doe - 719468;
	long long ms = ((days * 24 + h) * 60 + mi) * 60 + sec;
	ms *= 1000;
	if(s.size() > 20 && s[19] == '.') {
		int digits = 0, frac = 0;
		for(size_t i = 20; i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 3; ++i, ++digits)
			frac = frac * 10 + (s[i] - '0');
		for(; digits < 3; ++digits)
			frac *= 10;
		ms += frac;
	}
	return ms; // il bot scrive sempre UTC ("Z")
}

JudgeQuotaView JudgeQuotaFor(const std::string& plan, int left, int total) {
	JudgeQuotaView q;
	if(total <= 0)
		return q;
	q.known = true;
	q.total = total;
	q.left = left < 0 ? 0 : (left > total ? total : left);
	q.segments = total <= 5;
	q.finished = q.left == 0;
	if(q.finished)
		q.label = L"Domande finite";
	else
		q.label = std::to_wstring(q.left) + (q.left == 1 ? L" domanda su " : L" domande su ") + std::to_wstring(total) + (plan == "pack" ? L"" : L" in 7 giorni");
	return q;
}

int JudgeCountWords(const std::wstring& text) {
	int n = 0;
	bool in = false;
	for(const auto c : text) {
		const bool sp = c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
		if(!sp && !in)
			++n;
		in = !sp;
	}
	return n;
}

JudgeCounter JudgeCounterFor(const std::wstring& text) {
	JudgeCounter c;
	if(text.empty())
		return c;
	const int chars = static_cast<int>(text.size()), words = JudgeCountWords(text);
	c.text = std::to_wstring(chars) + L"/300 · " + std::to_wstring(words) + L"/60 parole";
	c.over = chars > 300 || words > 60;
	return c;
}

std::wstring JudgeHeadText(int turn, int hud_phase, bool replay) {
	static const wchar_t* const phases[] = { L"", L"Draw Phase", L"Standby Phase", L"Main Phase 1", L"Battle Phase", L"Main Phase 2", L"End Phase" };
	std::wstring out = replay ? L"Istantanea al punto di visione: " : L"Istantanea: ";
	out += L"T" + std::to_wstring(turn < 0 ? 0 : turn);
	if(hud_phase >= 1 && hud_phase <= 6)
		out += std::wstring(L", ") + phases[hud_phase];
	return out;
}

JudgeScene JudgeSceneFor(bool snapshot_taken, int messages_shown, bool waiting, int messages_left, bool closed) {
	if(closed && messages_shown > 0)
		return JudgeScene::Chiusa;
	if(waiting)
		return JudgeScene::Attesa;
	if(messages_shown > 0)
		return messages_left <= 0 ? JudgeScene::Esaurita : JudgeScene::Conversa;
	return snapshot_taken ? JudgeScene::Scrittura : JudgeScene::Vuota;
}

}
