// FASE 124d: scheda «Judge Chat» di wInfos. Forma, misure e testi: design/judge/anteprime/scheda-consegna-judge-chat-v1.md
// (nel vault, non qui). Tutte le decisioni (regole, quota, coda) sono del bot: il client mostra e invia.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <irrlicht.h>
#include "Base64.h"
#include "CGUITTFont/CGUITTFont.h"
#include "data_manager.h"
#include "game.h"
#include "game_config.h"
#include "judge_client.h"
#include "menu_handler.h"
#include "title_checkin.h"
#include "utils.h"

namespace ygo {

namespace {
constexpr irr::u32 C_BG = 0xff11151f, C_LINE = 0xff2b3346, C_FG = 0xffe4e8f1, C_DIM = 0xff8a93a8, C_HEAD = 0xffcfd6e6;
constexpr irr::u32 C_ACC = 0xff8b5cf6, C_ACCD = 0xff5a2d91, C_WARN = 0xfff5b941, C_BAD = 0xffff5d6c;
constexpr irr::u32 C_YOU = 0xff2c3550, C_YOU_LINE = 0xff46527a, C_ORA = 0xff211a3a, C_CODE = 0xffc6b6f5;
constexpr irr::u32 C_WARN_BG = 0xff2a2415, C_BAD_BG = 0xff2d1a1e, C_WARN_TX = 0xfff0e3c4, C_BAD_TX = 0xffffd9dd;

std::string JStr(const nlohmann::json& j, const char* key) {
	return (j.is_object() && j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : std::string{};
}
long long JNum(const nlohmann::json& j, const char* key) {
	return (j.is_object() && j.contains(key) && j[key].is_number()) ? j[key].get<long long>() : -1;
}
long long SteadyMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
long long WallMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// A capo per parole; una parola piu' larga della riga si spezza a caratteri. Niente testo tagliato.
std::vector<std::wstring> Wrap(irr::gui::CGUITTFont* f, const std::wstring& text, irr::s32 maxw) {
	std::vector<std::wstring> out;
	maxw = std::max(maxw, 10);
	size_t pos = 0;
	while(pos <= text.size()) {
		auto nl = text.find(L'\n', pos);
		if(nl == std::wstring::npos)
			nl = text.size();
		const auto para = text.substr(pos, nl - pos);
		std::wstring line;
		size_t p = 0;
		while(p <= para.size()) {
			auto e = para.find(L' ', p);
			if(e == std::wstring::npos)
				e = para.size();
			std::wstring word = para.substr(p, e - p);
			p = e + 1;
			if(word.empty() && e >= para.size())
				break;
			while(static_cast<irr::s32>(f->getDimension(word.data()).Width) > maxw && word.size() > 1) {
				size_t n = word.size();
				while(n > 1 && static_cast<irr::s32>(f->getDimension(word.substr(0, n).data()).Width) > maxw)
					--n;
				if(!line.empty()) {
					out.push_back(line);
					line.clear();
				}
				out.push_back(word.substr(0, n));
				word = word.substr(n);
			}
			const auto cand = line.empty() ? word : line + L" " + word;
			if(!line.empty() && static_cast<irr::s32>(f->getDimension(cand.data()).Width) > maxw) {
				out.push_back(line);
				line = word;
			} else
				line = cand;
		}
		out.push_back(line);
		pos = nl + 1;
		if(nl >= text.size())
			break;
	}
	return out;
}

const std::vector<std::wstring>& Instructions(bool tournament, bool spectator, bool replay) {
	static std::vector<std::wstring> v;
	v = { L"Carte col nome intero: Orcust Harp Horror",
		L"Un archetipo fra quadre: [Orcust]",
		L"I turni come T1, T2… (il contatore è accanto alle fasi)",
		tournament ? L"In torneo niente consigli: chiedi cosa è successo e perché" : L"Chiedi cosa è successo e perché, non cosa conviene fare",
		L"3 messaggi per chat, la chat dura un'ora" };
	if(spectator)
		v.push_back(L"Da spettatore l'Oracolo vede solo le carte pubbliche");
	if(replay)
		v.push_back(L"L'istantanea è presa al punto in cui stai guardando");
	return v;
}
const wchar_t* const INSTR_TITLE = L"Chiedi a un judge cosa è successo";
const wchar_t* const ESAURITA = L"Hai usato i 3 messaggi di questa chat. Puoi rileggerla, oppure aprire una chat nuova con un'altra domanda.";
} // namespace

// Disegna testa, flusso, riquadri, quota; dispone il campo e i tasti. Lo stato sta in Game::jc.
class JudgeChatView final : public irr::gui::IGUIElement {
public:
	JudgeChatView(irr::gui::IGUIEnvironment* env, irr::gui::IGUIElement* parent, const irr::core::recti& r) :
		irr::gui::IGUIElement(irr::gui::EGUIET_ELEMENT, env, parent, -1, r) {}

	bool OnEvent(const irr::SEvent& e) override {
		if(e.EventType != irr::EET_MOUSE_INPUT_EVENT)
			return IGUIElement::OnEvent(e);
		if(e.MouseInput.Event == irr::EMIE_MOUSE_WHEEL) {
			scroll = std::clamp(scroll - static_cast<int>(e.MouseInput.Wheel * mainGame->Scale(30)), 0, std::max(0, content_h - feed.getHeight()));
			mainGame->jc.stick = scroll >= std::max(0, content_h - feed.getHeight());
			return true;
		}
		if(e.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP) {
			const irr::core::vector2di p(e.MouseInput.X, e.MouseInput.Y);
			for(const auto& h : report_hits)
				if(h.second.isPointInside(p)) {
					mainGame->OnJudgeReport(h.first);
					return true;
				}
		}
		return IGUIElement::OnEvent(e);
	}

	void draw() override {
		if(!IsVisible)
			return;
		auto& g = *mainGame;
		auto* const drv = g.driver;
		auto* const font = g.textFont;
		const auto clip = AbsoluteClippingRect;
		const auto parentSize = Parent->getAbsolutePosition();
		setRelativePosition(irr::core::recti(0, 0, parentSize.getWidth(), parentSize.getHeight()));
		const auto abs = getAbsolutePosition();
		const int u = g.Scale(1), pad = g.Scale(6), gap = g.Scale(3);
		const int lh = font->getDimension(L"Ag").Height + g.Scale(3);
		const auto at = [&](int x, int y, int x2, int y2) { return irr::core::recti(abs.UpperLeftCorner.X + x, abs.UpperLeftCorner.Y + y, abs.UpperLeftCorner.X + x2, abs.UpperLeftCorner.Y + y2); };
		const int W = abs.getWidth(), H = abs.getHeight();
		auto& jc = g.jc;
		const bool avail = g.jc_active;
		const auto cd = JudgeCountdownFor(jc.first_ms, WallMs());
		const auto scene = JudgeSceneFor(jc.snapshot, static_cast<int>(jc.msgs.size()), jc.waiting, jc.messages_left, jc.closed || cd.closed);
		drv->draw2DRectangle(C_BG, abs, &clip);

		// ---- misure della colonna ----
		const int headH = g.Scale(22), rowH = g.Scale(25), quotaH = g.Scale(14), fieldH = g.Scale(30), counterH = g.Scale(12);
		const int rowY = H - g.Scale(7) - rowH;
		int y = rowY - gap;
		const auto quotaR = at(pad, y - quotaH, W - pad, y);
		y -= quotaH + gap;
		// riquadro che sostituisce il campo
		std::wstring blockText;
		bool blockBad = false;
		if(scene == JudgeScene::Chiusa) {
			blockText = JudgeErrorFor("chat_chiusa", "", 0).text;
		} else if(jc.has_err && jc.err.blocks_field) {
			blockText = jc.err.text;
			blockBad = jc.err.bad;
		} else if(scene == JudgeScene::Esaurita) {
			blockText = ESAURITA;
		}
		const int textW = W - 2 * pad - g.Scale(10);
		irr::core::recti blockR, fieldR, counterR, noteR;
		std::vector<std::wstring> blockLines, noteLines;
		const bool showField = avail && blockText.empty();
		if(!blockText.empty()) {
			blockLines = Wrap(font, blockText, textW);
			const int bh = std::max(fieldH, static_cast<int>(blockLines.size()) * lh + g.Scale(8));
			blockR = at(pad, y - bh, W - pad, y);
			y -= bh + gap;
		} else if(showField) {
			counterR = at(pad, y - counterH, W - pad, y);
			y -= counterH;
			fieldR = at(pad, y - fieldH, W - pad, y);
			y -= fieldH + gap;
		}
		// riquadro sopra il campo (errore transitorio o coaching): spinge su il flusso
		std::wstring noteText;
		bool noteBad = false;
		if(showField && jc.has_err && !jc.err.blocks_field) {
			noteText = jc.err.text;
			noteBad = jc.err.bad;
		} else if(showField && jc.coaching.level != JudgeCoachingInfo::None) {
			noteText = jc.coaching.text;
			noteBad = jc.coaching.level == JudgeCoachingInfo::Avvisati;
		}
		const bool retryShown = showField && jc.has_err && jc.err.retry;
		if(!noteText.empty()) {
			noteLines = Wrap(font, noteText, textW);
			const int nh = static_cast<int>(noteLines.size()) * lh + g.Scale(8) + (retryShown ? g.Scale(20) : 0);
			noteR = at(pad, y - nh, W - pad, y);
			y -= nh + gap;
		}
		const int headBottom = headH;
		feed = at(pad, headBottom + gap, W - pad, std::max(headBottom + gap + g.Scale(20), y));
		// ---- dispone i figli ----
		const auto rel = [&](const irr::core::recti& r) { return irr::core::recti(r.UpperLeftCorner - abs.UpperLeftCorner, r.LowerRightCorner - abs.UpperLeftCorner); };
		const int sendW = g.Scale(46);
		g.ebJudgeQuestion->setVisible(showField);
		g.btnJudgeSend->setVisible(showField);
		if(showField) {
			g.ebJudgeQuestion->setRelativePosition(rel(irr::core::recti(fieldR.UpperLeftCorner.X, fieldR.UpperLeftCorner.Y, fieldR.LowerRightCorner.X - sendW - gap, fieldR.LowerRightCorner.Y)));
			g.btnJudgeSend->setRelativePosition(rel(irr::core::recti(fieldR.LowerRightCorner.X - sendW, fieldR.UpperLeftCorner.Y, fieldR.LowerRightCorner.X, fieldR.LowerRightCorner.Y)));
			g.btnJudgeSend->setEnabled(!jc.waiting);
		}
		g.btnJudgeRetry->setVisible(retryShown);
		if(retryShown) {
			const int rw = g.Scale(70);
			g.btnJudgeRetry->setRelativePosition(rel(irr::core::recti(noteR.UpperLeftCorner.X + g.Scale(8), noteR.LowerRightCorner.Y - g.Scale(22), noteR.UpperLeftCorner.X + g.Scale(8) + rw, noteR.LowerRightCorner.Y - g.Scale(3))));
		}
		const int expW = g.Scale(70), copyW = g.Scale(56);
		const int rowTop = rowY, rowBot = rowY + rowH;
		g.btnExpandJudge->setRelativePosition(irr::core::recti(pad, rowTop, pad + expW, rowBot));
		g.btnJudgeCopy->setRelativePosition(irr::core::recti(pad + expW + gap, rowTop, pad + expW + gap + copyW, rowBot));
		const int anX = pad + expW + copyW + 2 * gap, anW = W - pad - anX;
		g.btnJudgeAnother->setText(static_cast<int>(font->getDimension(L"Chiedi un'altra domanda").Width) + g.Scale(10) > anW ? L"Altra domanda" : L"Chiedi un'altra domanda");
		g.btnJudgeAnother->setRelativePosition(irr::core::recti(anX, rowTop, W - pad, rowBot));
		g.btnJudgeCopy->setEnabled(!jc.msgs.empty());
		g.btnJudgeAnother->setEnabled(avail && scene != JudgeScene::Vuota && scene != JudgeScene::Attesa);
		g.btnJudgeCopy->setVisible(true);

		// ---- testa ----
		{
			auto hr = at(pad, 0, W - pad, headH);
			if(!avail) {
				font->drawustring(L"Judge Chat", hr, C_HEAD, false, true, &clip);
			} else if(jc.snapshot) {
				font->drawustring(jc.head, hr, C_HEAD, false, true, &clip);
			} else {
				font->drawustring(L"Nessuna istantanea", hr, C_DIM, false, true, &clip);
			}
			if(avail && cd.visible && scene != JudgeScene::Chiusa) {
				const bool blink = cd.red && !gGameConfig->quick_animation && (WallMs() / 500) % 2 == 0;
				const auto col = cd.red ? (blink ? 0xff7a2a33 : C_BAD) : C_WARN;
				const auto txt = std::wstring(cd.text.begin(), cd.text.end());
				const int tw = font->getDimension(txt.data()).Width;
				font->drawustring(txt, at(W - pad - tw, 0, W - pad, headH), col, false, true, &clip);
			}
			drv->draw2DRectangle(C_LINE, at(0, headH - 1, W, headH), &clip);
		}

		// ---- flusso ----
		report_hits.clear();
		if(!avail) {
			auto lines = Wrap(font, L"La Judge Chat si apre in partita o in un replay, con la credenziale attiva.", feed.getWidth());
			int ty = feed.UpperLeftCorner.Y + gap;
			for(const auto& l : lines) {
				font->drawustring(l, irr::core::recti(feed.UpperLeftCorner.X, ty, feed.LowerRightCorner.X, ty + lh), C_DIM, false, true, &feed);
				ty += lh;
			}
		} else {
			const bool showInstr = jc.msgs.empty() && !jc.typed;
			int cy = 0;
			const auto fx = feed.UpperLeftCorner.X, fy = feed.UpperLeftCorner.Y - scroll;
			if(showInstr) {
				font->drawustring(INSTR_TITLE, irr::core::recti(fx, fy + cy, feed.LowerRightCorner.X, fy + cy + lh), 0xffaab3c8, false, true, &feed);
				cy += lh + gap;
				for(const auto& it : Instructions(g.dInfo.isTournamentRoom, g.dInfo.player_type >= 7, g.dInfo.isReplay)) {
					const auto lines = Wrap(font, it, feed.getWidth() - g.Scale(12));
					font->drawustring(L"•", irr::core::recti(fx + g.Scale(2), fy + cy, fx + g.Scale(12), fy + cy + lh), C_DIM, false, true, &feed);
					for(const auto& l : lines) {
						font->drawustring(l, irr::core::recti(fx + g.Scale(12), fy + cy, feed.LowerRightCorner.X, fy + cy + lh), 0xff9aa3b8, false, true, &feed);
						cy += lh;
					}
				}
				cy += pad;
			}
			// tasto «Prendi l'istantanea e chiedi» sotto le istruzioni
			const bool snapShown = scene == JudgeScene::Vuota;
			g.btnJudgeSnap->setVisible(snapShown);
			if(snapShown)
				g.btnJudgeSnap->setRelativePosition(rel(irr::core::recti(fx, fy + cy, feed.LowerRightCorner.X, fy + cy + g.Scale(26))));
			if(snapShown)
				cy += g.Scale(26) + pad;
			const int bw = feed.getWidth() * 92 / 100;
			for(size_t i = 0; i < jc.msgs.size(); ++i) {
				const auto& m = jc.msgs[i];
				const int innerW = (m.kind == 2 ? feed.getWidth() : bw) - 2 * pad;
				const auto lines = Wrap(font, m.text, innerW);
				const bool reportable = m.kind == 1 && !m.ticket.empty();
				const int extra = (m.not_counted ? lh : 0) + (reportable ? lh : 0);
				const int bh = (m.kind == 2 ? 0 : lh) + static_cast<int>(lines.size()) * lh + extra + g.Scale(6);
				const int bx = m.kind == 0 ? feed.LowerRightCorner.X - bw : fx;
				const int bwid = m.kind == 2 ? feed.getWidth() : bw;
				const irr::core::recti br(bx, fy + cy, bx + bwid, fy + cy + bh);
				irr::u32 bg = m.kind == 0 ? C_YOU : m.kind == 1 ? C_ORA : (m.bad ? C_BAD_BG : C_WARN_BG);
				irr::u32 edge = m.kind == 0 ? C_YOU_LINE : m.kind == 1 ? C_ACC : (m.bad ? C_BAD : C_WARN);
				irr::u32 tx = m.kind == 2 ? (m.bad ? C_BAD_TX : C_WARN_TX) : 0xffeeeeee;
				if(m.not_counted)
					tx = 0xff8a93a8;
				drv->draw2DRectangle(bg, br, &feed);
				drv->draw2DRectangle(edge, irr::core::recti(br.UpperLeftCorner.X, br.UpperLeftCorner.Y, br.UpperLeftCorner.X + std::max(1, u), br.LowerRightCorner.Y), &feed);
				int ty = br.UpperLeftCorner.Y + g.Scale(3);
				if(m.kind != 2) {
					font->drawustring(m.kind == 0 ? L"Tu" : L"Oracolo", irr::core::recti(br.UpperLeftCorner.X + pad, ty, br.LowerRightCorner.X - pad, ty + lh), m.kind == 0 ? 0xff9ec5ff : C_CODE, false, true, &feed);
					ty += lh;
				}
				for(const auto& l : lines) {
					font->drawustring(l, irr::core::recti(br.UpperLeftCorner.X + pad, ty, br.LowerRightCorner.X - pad, ty + lh), tx, false, true, &feed);
					ty += lh;
				}
				if(m.not_counted) {
					font->drawustring(L"non conteggiato", irr::core::recti(br.UpperLeftCorner.X + pad, ty, br.LowerRightCorner.X - pad, ty + lh), 0xffffb0b8, false, true, &feed);
					ty += lh;
				}
				if(reportable) {
					const std::wstring label = m.reported ? L"Segnalata" : L"⚑ Segnala";
					const int lw = font->getDimension(label.data()).Width;
					const irr::core::recti lr(br.LowerRightCorner.X - pad - lw, ty, br.LowerRightCorner.X - pad, ty + lh);
					font->drawustring(label, lr, m.reported ? C_DIM : 0xffb8bdc8, false, true, &feed);
					if(!m.reported) {
						auto hit = lr;
						hit.UpperLeftCorner.X -= pad;
						hit.UpperLeftCorner.Y -= gap;
						hit.LowerRightCorner.X += pad;
						hit.LowerRightCorner.Y += gap;
						if(hit.isRectCollided(feed))
							report_hits.emplace_back(i, hit);
					}
				}
				cy += bh + gap;
			}
			if(jc.waiting) {
				const int dh = lh + g.Scale(6), dw = g.Scale(46);
				const irr::core::recti br(fx, fy + cy, fx + dw, fy + cy + dh);
				drv->draw2DRectangle(C_ORA, br, &feed);
				drv->draw2DRectangle(C_ACC, irr::core::recti(br.UpperLeftCorner.X, br.UpperLeftCorner.Y, br.UpperLeftCorner.X + std::max(1, u), br.LowerRightCorner.Y), &feed);
				const long long t = SteadyMs();
				for(int d = 0; d < 3; ++d) {
					const bool on = gGameConfig->quick_animation ? true : ((t / 300) % 3) == d;
					const int cx = br.UpperLeftCorner.X + g.Scale(12) + d * g.Scale(11), cyy = br.UpperLeftCorner.Y + dh / 2;
					const int r = std::max(1, g.Scale(2));
					drv->draw2DRectangle(on ? C_CODE : 0xff6c7896, irr::core::recti(cx - r, cyy - r, cx + r, cyy + r), &feed);
				}
				cy += dh + gap;
			}
			content_h = cy;
			const int maxScroll = std::max(0, content_h - feed.getHeight());
			if(jc.stick)
				scroll = maxScroll;
			else
				scroll = std::clamp(scroll, 0, maxScroll);
		}

		// ---- riquadri ----
		const auto box = [&](const irr::core::recti& r, const std::vector<std::wstring>& lines, bool bad) {
			drv->draw2DRectangle(bad ? C_BAD_BG : C_WARN_BG, r, &clip);
			drv->draw2DRectangle(bad ? C_BAD : C_WARN, irr::core::recti(r.UpperLeftCorner.X, r.UpperLeftCorner.Y, r.UpperLeftCorner.X + std::max(2, g.Scale(3)), r.LowerRightCorner.Y), &clip);
			int ty = r.UpperLeftCorner.Y + g.Scale(4);
			for(const auto& l : lines) {
				font->drawustring(l, irr::core::recti(r.UpperLeftCorner.X + g.Scale(8), ty, r.LowerRightCorner.X - g.Scale(2), ty + lh), bad ? C_BAD_TX : C_WARN_TX, false, true, &clip);
				ty += lh;
			}
		};
		if(!blockText.empty())
			box(blockR, blockLines, blockBad);
		if(!noteText.empty())
			box(noteR, noteLines, noteBad);
		if(showField) {
			const auto c = JudgeCounterFor(g.ebJudgeQuestion->getText());
			if(!c.text.empty()) {
				const int tw = font->getDimension(c.text.data()).Width;
				font->drawustring(c.text, irr::core::recti(counterR.LowerRightCorner.X - tw, counterR.UpperLeftCorner.Y, counterR.LowerRightCorner.X, counterR.LowerRightCorner.Y), c.over ? C_BAD : C_DIM, false, true, &clip);
			}
		}

		// ---- quota ----
		if(avail) {
			const auto q = JudgeQuotaFor(g.jc_quota_plan, g.jc_quota_left, g.jc_quota_total);
			if(q.known) {
				int x = quotaR.UpperLeftCorner.X;
				const int sh = std::max(3, g.Scale(5)), sy = quotaR.UpperLeftCorner.Y + (quotaH - sh) / 2;
				if(q.segments) {
					const int sw = g.Scale(14);
					for(int i = 0; i < q.total; ++i) {
						drv->draw2DRectangle(i < q.left ? C_ACC : 0xff333a4c, irr::core::recti(x, sy, x + sw, sy + sh), &clip);
						x += sw + std::max(1, u);
					}
				} else {
					const int bwd = g.Scale(60);
					drv->draw2DRectangle(0xff333a4c, irr::core::recti(x, sy, x + bwd, sy + sh), &clip);
					drv->draw2DRectangle(C_ACC, irr::core::recti(x, sy, x + bwd * q.left / std::max(1, q.total), sy + sh), &clip);
					x += bwd;
				}
				font->drawustring(q.label, irr::core::recti(x + gap, quotaR.UpperLeftCorner.Y, quotaR.LowerRightCorner.X, quotaR.LowerRightCorner.Y), q.finished ? C_BAD : C_DIM, false, true, &clip);
			}
		}
		IGUIElement::draw();
	}

private:
	irr::core::recti feed;
	int scroll = 0, content_h = 0;
	std::vector<std::pair<size_t, irr::core::recti>> report_hits;
};

void Game::PopulateJudgeChatTab() {
	tabJudge = wInfos->addTab(L"Judge Chat");
	auto* view = new JudgeChatView(env, tabJudge, Scale(0, 0, 300, 340));
	view->drop();
	jcView = view;
	ebJudgeQuestion = env->addEditBox(L"", Scale(5, 200, 240, 230), true, tabJudge);
	ebJudgeQuestion->setMultiLine(true);
	ebJudgeQuestion->setWordWrap(true);
	ebJudgeQuestion->setAutoScroll(true);
	ebJudgeQuestion->setVisible(false);
	btnJudgeSend = env->addButton(Scale(240, 200, 285, 230), tabJudge, BUTTON_JUDGE_SEND, L"Invia");
	btnJudgeSend->setVisible(false);
	btnJudgeSnap = env->addButton(Scale(5, 100, 285, 126), tabJudge, BUTTON_JUDGE_SNAP, L"Prendi l'istantanea e chiedi");
	btnJudgeSnap->setVisible(false);
	btnJudgeRetry = env->addButton(Scale(10, 100, 80, 120), tabJudge, BUTTON_JUDGE_RETRY, L"Riprova");
	btnJudgeRetry->setVisible(false);
	btnExpandJudge = env->addButton(Scale(5, 300, 75, 325), tabJudge, BUTTON_EXPAND_INFOBOX, gDataManager->GetSysString(2043).data());
	defaultStrings.emplace_back(btnExpandJudge, 2043);
	btnJudgeCopy = env->addButton(Scale(80, 300, 136, 325), tabJudge, BUTTON_JUDGE_COPY, L"Copia");
	btnJudgeAnother = env->addButton(Scale(140, 300, 295, 325), tabJudge, BUTTON_JUDGE_ANOTHER, L"Chiedi un'altra domanda");
}

void Game::JudgeMoment(const char* name, float intensity) {
	// Contratto col Sound Designer: nome stabile + intensita'. Nel client non c'e' (ancora) un bus di eventi `momento`
	// ne' un suono da agganciare: il punto di emissione e' questo, unico, e non suona niente.
	(void)name;
	(void)intensity;
}

void Game::JudgeChatFrame(bool available) {
	if(available != jc_active) {
		jc_active = available;
		if(!available) { // le chat stanno solo in memoria e si perdono a fine partita
			jc = JudgeChat{};
			judge_ticket.clear();
			jc_quota_asked = false;
			if(ebJudgeQuestion)
				ebJudgeQuestion->setText(L"");
		}
	}
	if(!available || !tabJudge)
		return;
	const bool open = wInfos->getActiveTab() >= 0 && wInfos->getTab(wInfos->getActiveTab()) == tabJudge;
	if(open && !jc_quota_asked && wInfos->isVisible()) { // la quota si legge all'apertura della scheda
		jc_quota_asked = true;
		QueuePlayerApi("judge_pending", "GET", "/v1/client/judge/pending");
	}
	// scrivere fa sparire le istruzioni
	const bool typed = ebJudgeQuestion->getText()[0] != 0;
	if(typed && !jc.typed)
		JudgeMoment("scrittura");
	jc.typed = typed;
	// conto alla rovescia: eventi alle soglie
	const auto cd = JudgeCountdownFor(jc.first_ms, WallMs());
	if(cd.visible && jc.cd_stage < 1) {
		jc.cd_stage = 1;
		JudgeMoment("conto_alla_rovescia");
	}
	if(cd.visible && cd.seconds_left <= 60 && jc.cd_stage < 2) {
		jc.cd_stage = 2;
		JudgeMoment("ultimo_minuto");
	}
	if(cd.closed && !jc.closed && !jc.msgs.empty()) {
		jc.closed = true;
		JudgeMoment("chat_chiusa", 0.4f);
	}
}

void Game::OnJudgeReport(size_t i) {
	if(i >= jc.msgs.size() || jc.msgs[i].reported || jc.msgs[i].ticket.empty())
		return;
	jc.msgs[i].reported = true;
	JudgeMoment("segnala");
	QueuePlayerApi("judge_report", "POST", "/v1/client/judge/questions/" + jc.msgs[i].ticket + "/report", "{}");
}

void Game::OnJudgeChatButton(int id) {
	switch(id) {
	case BUTTON_JUDGE_SNAP: {
		std::string replay;
		if(!CollectJudgeReplay(replay)) {
			jc.has_err = true;
			jc.err = JudgeErrorInfo{};
			jc.err.text = L"Non riesco a leggere lo stato della partita: riprova fra un momento.";
			JudgeMoment("errore", 0.2f);
			break;
		}
		jc.snapshot_b64 = base64_encode<std::string>(replay);
		jc.head = JudgeHeadText(dInfo.turn, dInfo.hud_phase, dInfo.isReplay);
		jc.snapshot = true;
		jc.has_err = false;
		env->setFocus(ebJudgeQuestion);
		JudgeMoment("snapshot");
		break;
	}
	case BUTTON_JUDGE_RETRY:
	case BUTTON_JUDGE_SEND: {
		if(jc.waiting || !jc_active)
			break;
		std::wstring wq = ebJudgeQuestion->getText();
		while(!wq.empty() && (wq.back() == L' ' || wq.back() == L'\n' || wq.back() == L'\r'))
			wq.pop_back();
		if(wq.empty()) {
			jc.has_err = true;
			jc.err = JudgeErrorFor("domanda_non_valida", "", 0);
			break;
		}
		nlohmann::json body{ { "question", Utils::ToUTF8IfNeeded(wq) } };
		if(jc.root.empty()) {
			if(!jc.snapshot)
				OnJudgeChatButton(BUTTON_JUDGE_SNAP);
			if(!jc.snapshot)
				break;
			body["yrpx"] = jc.snapshot_b64;
			body["tournamentDuel"] = dInfo.isTournamentRoom;
			body["spectator"] = dInfo.player_type >= 7;
		} else {
			body["follow_up_of"] = jc.last_ticket;
		}
		jc.has_err = false;
		jc.coaching = JudgeCoachingInfo{};
		jc.last_question = wq;
		JudgeMsg m;
		m.kind = 0;
		m.text = wq;
		jc.msgs.push_back(std::move(m));
		jc.waiting = true;
		jc.stick = true;
		ebJudgeQuestion->setText(L"");
		JudgeMoment("domanda");
		JudgeMoment("attesa");
		QueuePlayerApi("judge_ask", "POST", "/v1/client/judge/questions", body.dump());
		break;
	}
	case BUTTON_JUDGE_ANOTHER:
		if(jc.waiting)
			break;
		jc = JudgeChat{};
		judge_ticket.clear();
		ebJudgeQuestion->setText(L"");
		JudgeMoment("nuova_chat");
		break;
	case BUTTON_JUDGE_COPY: {
		std::wstring all;
		for(const auto& m : jc.msgs)
			all += std::wstring(m.kind == 0 ? L"Tu: " : m.kind == 1 ? L"Oracolo: " : L"") + m.text + L"\n";
		if(!all.empty() && Utils::OSOperator)
			Utils::OSOperator->copyToClipboard(all.data());
		break;
	}
	default:
		break;
	}
}

void Game::OnJudgeChatApi(const TitleCheckin::ApiResult& r) {
	const auto j = nlohmann::json::parse(r.body, nullptr, false);
	const bool ok = r.transport_ok && r.status_code >= 200 && r.status_code < 300;
	// il messaggio non e' stato accettato: si toglie la bolla (salvo i casi "non conteggiato") e il testo torna nel campo
	const auto reject = [&](const JudgeErrorInfo& e, bool keep_bubble) {
		jc.waiting = false;
		judge_ticket.clear();
		if(!jc.msgs.empty() && jc.msgs.back().kind == 0) {
			if(keep_bubble)
				jc.msgs.back().not_counted = true;
			else {
				ebJudgeQuestion->setText(jc.msgs.back().text.data());
				jc.msgs.pop_back();
			}
		}
		if(e.kind == JudgeErrorInfo::Chiusa)
			jc.closed = true;
		jc.has_err = true;
		jc.err = e;
		JudgeMoment("errore", e.intensity);
	};
	const auto& tag = r.tag;
	if(tag == "judge_ask") {
		if(!ok || !j.is_object() || JStr(j, "ticket").empty()) {
			const auto code = r.transport_ok ? JStr(j, "error") : std::string("judge_non_disponibile");
			const auto e = JudgeErrorFor(code, JStr(j, "messaggio"), jc_quota_total);
			reject(e, e.not_counted);
			return;
		}
		judge_ticket = JStr(j, "ticket");
		judge_tick = 0;
		judge_next_poll_ms = SteadyMs() + 1500;
		return;
	}
	if(tag == "judge_poll") {
		const auto state = JStr(j, "state");
		if(!ok || !j.is_object()) {
			if(r.transport_ok && r.status_code == 404) {
				judge_ticket.clear();
				reject(JudgeErrorFor("judge_non_disponibile", "", 0), false);
			}
			return; // un errore di rete passeggero: si riprova al prossimo ritiro
		}
		if(state == "queued" || state == "running")
			return;
		const auto ticket = judge_ticket;
		judge_ticket.clear();
		if(state == "done" || state == "facts_only") {
			JudgeMsg m;
			m.kind = 1;
			m.ticket = ticket;
			if(state == "done")
				m.text = Utils::ToUnicodeIfNeeded(JudgeHighlight(JStr(j, "answer")));
			else
				m.text = L"L'Oracolo non è riuscito a rispondere con sicurezza: ecco i fatti della partita (la domanda non è stata scalata).\n" + Utils::ToUnicodeIfNeeded(JStr(j, "facts"));
			jc.msgs.push_back(std::move(m));
			jc.last_ticket = ticket;
			if(j.contains("session") && j["session"].is_array() && !j["session"].empty()) {
				const auto& first = j["session"][0];
				if(jc.root.empty())
					jc.root = JStr(first, "ticket");
				if(jc.first_ms < 0)
					jc.first_ms = JudgeParseIsoMs(JStr(first, "created_at"));
			}
			if(jc.root.empty())
				jc.root = ticket;
			const auto left = JNum(j, "messages_left");
			if(left >= 0)
				jc.messages_left = static_cast<int>(left);
			jc.coaching = JudgeCoachingFor(JStr(j, "coaching"), dInfo.isTournamentRoom);
			jc.waiting = false;
			jc.stick = true;
			JudgeMoment("risposta");
			if(jc.coaching.level != JudgeCoachingInfo::None)
				JudgeMoment("errore", jc.coaching.intensity);
			if(jc.messages_left <= 0)
				JudgeMoment("chat_esaurita");
			QueuePlayerApi("judge_pending", "GET", "/v1/client/judge/pending"); // la quota si rilegge dopo ogni risposta
			return;
		}
		if(state == "rejected") {
			jc.waiting = false;
			JudgeMsg m;
			m.kind = 2;
			m.bad = true;
			m.text = L"Domanda non accettata.";
			jc.msgs.push_back(std::move(m));
			jc.stick = true;
			JudgeMoment("errore", 0.4f);
			return;
		}
		reject(JudgeErrorFor("occupato", "", 0), false); // scaduta: come "Oracolo occupato", non scalata
		return;
	}
	if(tag == "judge_pending") {
		if(ok && j.is_object() && j.contains("quota") && j["quota"].is_object()) {
			const auto& q = j["quota"];
			jc_quota_plan = JStr(q, "plan");
			jc_quota_left = static_cast<int>(std::max(0LL, JNum(q, "left")));
			jc_quota_total = static_cast<int>(std::max(0LL, JNum(q, "total")));
		}
		return;
	}
	// judge_report: idempotente, nessun esito da mostrare
}

} // namespace ygo
