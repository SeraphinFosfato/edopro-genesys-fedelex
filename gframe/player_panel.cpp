#include <cmath>
#include <algorithm>
// FASE 98C: pannello del player (profilo, pubblicazioni, "Pubblica un deck",
// replay cloud). Contratto client -> bot: PHASES.md FASE 98 "Contratto bot ->
// client/web". Tutti i testi sono letterali (niente strings.conf, stessa
// ragione di wTitleAuth). Una richiesta alla volta (TitleCheckin::StartApiRequest),
// le altre aspettano in coda.
#include <algorithm>
#include <nlohmann/json.hpp>
#include <irrlicht.h>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#if defined(_WIN32)
#include <sys/utime.h>
#else
#include <utime.h>
#endif
#include "Base64.h"
#include "cloud_decks.h"
#include "sha256.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "deep_link.h"
#include "file_stream.h"
#include "fmt.h"
#include "game.h"
#include "game_config.h"
#include "judge_client.h"
#include "duelclient.h"
#include "replay_mode.h"
#include "logging.h"
#include "menu_handler.h"
#include "replay.h"
#include "title_checkin.h"
#include "utils.h"

namespace ygo {

namespace {
constexpr size_t MAX_ATTACHED_REPLAY = 64u * 1024u; // regole numeriche: replay allegato <= 64 KB .yrpX
constexpr size_t EDIT_REMOVE = 1; // indici fissi nel combo replay in modifica (98L)
const wchar_t* const GENERIC_ERROR = L"Qualcosa è andato storto, riprova";

std::string JStr(const nlohmann::json& j, const char* key) {
	return (j.is_object() && j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : std::string{};
}
long long JNum(const nlohmann::json& j, const char* key) {
	return (j.is_object() && j.contains(key) && j[key].is_number()) ? j[key].get<long long>() : 0;
}
std::wstring W(const std::string& s) {
	return Utils::ToUnicodeIfNeeded(s);
}

// Contratto: il client mostra `messaggio` solo per limite_totale e
// limite_giornaliero e replay_non_pubblicabile; ogni altro errore e' la frase generica.
std::wstring ErrorText(const TitleCheckin::ApiResult& r) {
	if(r.transport_ok) {
		auto j = nlohmann::json::parse(r.body, nullptr, false);
		const auto code = JStr(j, "error");
		const auto msg = JStr(j, "messaggio");
		if((code == "limite_totale" || code == "limite_giornaliero" || code == "replay_non_pubblicabile" || code == "troppo_presto") && !msg.empty())
			return W(msg);
	}
	return GENERIC_ERROR;
}

bool Ok(const TitleCheckin::ApiResult& r) {
	return r.transport_ok && r.status_code >= 200 && r.status_code < 300;
}

long long NowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool LocalMtime(const epro::path_string& path, long long& out) {
#if defined(_WIN32)
	struct _stat64i32 st;
	if(_wstat(path.data(), &st) != 0)
#else
	struct stat st;
	if(stat(path.data(), &st) != 0)
#endif
		return false;
	out = static_cast<long long>(st.st_mtime);
	return true;
}

void SetLocalMtime(const epro::path_string& path, long long t) {
#if defined(_WIN32)
	struct _utimbuf b { static_cast<time_t>(t), static_cast<time_t>(t) };
	_wutime(path.data(), &b);
#else
	struct utimbuf b { static_cast<time_t>(t), static_cast<time_t>(t) };
	utime(path.data(), &b);
#endif
}

std::wstring Slot(const nlohmann::json& slots, const char* key, const wchar_t* label) {
	const auto& s = slots.is_object() && slots.contains(key) ? slots[key] : nlohmann::json();
	return epro::format(L"{} {}/{}", label, JNum(s, "used"), JNum(s, "max"));
}

bool ReadFile(const epro::path_string& path, std::string& out) {
	FileStream f{ path, FileStream::in | FileStream::binary };
	if(f.fail())
		return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

void FillLocalReplays(irr::gui::IGUIComboBox* cb, std::vector<epro::path_string>& files, bool with_none, size_t max_bytes, bool edit = false) {
	cb->clear();
	files.clear();
	if(edit) { // due voci fisse davanti: file vuoto = nessun file da inviare
		cb->addItem(L"(non cambiare il replay)");
		cb->addItem(L"(togli il replay allegato)");
		files.emplace_back();
		files.emplace_back();
	}
	if(with_none) {
		cb->addItem(L"(nessun replay allegato)");
		files.emplace_back();
	}
	for(auto& file : Utils::FindFiles(Replay::GetReplayFolder(), { EPRO_TEXT("yrpx") })) {
		if(max_bytes) {
			std::string content;
			if(!ReadFile(Replay::GetReplayFilePath(file), content) || content.size() > max_bytes)
				continue; // oltre il tetto: non si offre nemmeno
		}
		files.push_back(Replay::GetReplayFilePath(file));
		cb->addItem(Utils::ToUnicodeIfNeeded(file).data());
	}
	if(cb->getItemCount())
		cb->setSelected(0);
}
}

void Game::BuildPlayerPanel() {
	// Icona utente viola in basso a destra: bottone senza bordo, sagoma disegnata
	// da MainLoop con due texture generate qui in memoria (nessun file da distribuire).
	for(int hover = 0; hover < 2; hover++) {
		constexpr int N = 64;
		auto* img = driver->createImage(irr::video::ECF_A8R8G8B8, irr::core::dimension2du(N, N));
		for(int y = 0; y < N; y++)
			for(int x = 0; x < N; x++) {
				// testa: cerchio; spalle: semiellisse; bordo sfumato di ~1.5px
				const float dh = std::sqrt(float((x - 32) * (x - 32) + (y - 20) * (y - 20))) - 13.f;
				const float ds = std::sqrt(float((x - 32) * (x - 32)) / (26.f * 26.f) + float((y - 60) * (y - 60)) / (22.f * 22.f)) * 22.f - 22.f;
				const float d = std::min(dh, y <= 60 ? ds : 99.f);
				const float a = std::clamp(0.5f - d / 1.5f, 0.f, 1.f);
				img->setPixel(x, y, hover ? irr::video::SColor(irr::u32(a * 255), 190, 150, 255) : irr::video::SColor(irr::u32(a * 255), 155, 107, 255));
			}
		texPlayerIcon[hover] = driver->addTexture(hover ? "player_icon_hover" : "player_icon", img);
		img->drop();
	}
	wBtnPlayer = env->addWindow(PlayerIconRect());
	wBtnPlayer->getCloseButton()->setVisible(false);
	wBtnPlayer->setDraggable(false);
	wBtnPlayer->setDrawTitlebar(false);
	wBtnPlayer->setDrawBackground(false);
	wBtnPlayer->setVisible(false);
	btnPlayerIcon = env->addButton(Scale(0, 0, 40, 40), wBtnPlayer, BUTTON_PLAYER_ICON, L"");
	btnPlayerIcon->setDrawBorder(false);
	btnPlayerIcon->setToolTipText(L"Profilo");

	wPlayer = env->addWindow(Scale(300, 50, 740, 560), false, L"Il mio profilo");
	wPlayer->getCloseButton()->setVisible(false);
	wPlayer->setVisible(false);
	stPlayerProfile = env->addStaticText(L"", Scale(20, 30, 420, 120), false, true, wPlayer);
	stPlayerSlots = env->addStaticText(L"", Scale(20, 122, 420, 162), false, true, wPlayer);
	lstPlayerPubs = env->addListBox(Scale(20, 168, 420, 340), wPlayer, -1, true);
	stPlayerStatus = env->addStaticText(L"", Scale(20, 346, 420, 396), false, true, wPlayer);
	env->addButton(Scale(20, 404, 150, 430), wPlayer, BUTTON_PLAYER_PUBDECK, L"Pubblica un deck");
	env->addButton(Scale(160, 404, 290, 430), wPlayer, BUTTON_PLAYER_CLOUD, L"I miei replay");
	env->addButton(Scale(300, 404, 420, 430), wPlayer, BUTTON_PLAYER_DELETE, L"Cancella");
	env->addButton(Scale(300, 440, 420, 466), wPlayer, BUTTON_PLAYER_EDIT, L"Modifica");
	env->addButton(Scale(20, 440, 140, 466), wPlayer, BUTTON_PLAYER_CLOUDDECKS, L"Mazzi nel cloud");
	env->addButton(Scale(150, 440, 290, 466), wPlayer, BUTTON_PLAYER_CLOSE, L"Chiudi");
	env->addButton(Scale(20, 474, 290, 500), wPlayer, BUTTON_JUDGE_PENDING, L"Risposta dell'Oracolo");

	// FASE 123C: "Chiedi all'Oracolo", in partita/replay (btnJudge) e per rileggere una risposta pronta (wPlayer).
	btnJudge = env->addButton(Scale(205, 84, 295, 108), 0, BUTTON_JUDGE_OPEN, L"Chiedi all'Oracolo");
	btnJudge->setAlignment(irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE, irr::gui::EGUIA_SCALE);
	btnJudge->setVisible(false);
	wJudge = env->addWindow(Scale(300, 40, 740, 520), false, L"Chiedi all'Oracolo");
	wJudge->getCloseButton()->setVisible(false);
	wJudge->setVisible(false);
	env->addStaticText(L"La tua domanda sulla partita:", Scale(20, 30, 420, 50), false, false, wJudge);
	ebJudgeQuestion = env->addEditBox(L"", Scale(20, 52, 420, 112), true, wJudge);
	ebJudgeQuestion->setMultiLine(true);
	ebJudgeQuestion->setWordWrap(true);
	btnJudgeSend = env->addButton(Scale(20, 120, 190, 146), wJudge, BUTTON_JUDGE_SEND, L"Invia");
	env->addButton(Scale(230, 120, 420, 146), wJudge, BUTTON_JUDGE_CLOSE, L"Chiudi");
	stJudgeStatus = env->addStaticText(L"", Scale(20, 154, 420, 184), false, true, wJudge);
	stJudgeAnswer = env->addStaticText(L"", Scale(20, 190, 420, 322), true, true, wJudge);
	lstJudgeCards = env->addListBox(Scale(20, 328, 420, 378), wJudge, LISTBOX_JUDGE_CARD, true);
	stJudgeCardText = env->addStaticText(L"", Scale(20, 384, 420, 470), true, true, wJudge);

	wPubDeck = env->addWindow(Scale(300, 60, 740, 540), false, L"Pubblica un deck");
	wPubDeck->getCloseButton()->setVisible(false);
	wPubDeck->setVisible(false);
	env->addStaticText(L"Mazzi locali (sola lettura):", Scale(20, 30, 420, 50), false, false, wPubDeck);
	cbPubDeckDeck = env->addComboBox(Scale(20, 54, 420, 79), wPubDeck, COMBOBOX_PUBDECK_DECK);
	cbPubDeckDeck->setMaxSelectionRows(10);
	lstPubDeckCards = env->addListBox(Scale(20, 84, 420, 214), wPubDeck, -1, true);
	env->addStaticText(L"Titolo", Scale(20, 222, 100, 242), false, false, wPubDeck);
	ebPubDeckTitle = env->addEditBox(L"", Scale(100, 220, 420, 245), true, wPubDeck);
	env->addStaticText(L"Descrizione", Scale(20, 252, 100, 272), false, false, wPubDeck);
	ebPubDeckDesc = env->addEditBox(L"", Scale(100, 250, 420, 275), true, wPubDeck);
	env->addStaticText(L"Replay", Scale(20, 282, 100, 302), false, false, wPubDeck);
	cbPubDeckReplay = env->addComboBox(Scale(100, 280, 420, 305), wPubDeck);
	stPubDeckStatus = env->addStaticText(L"", Scale(20, 314, 420, 400), false, true, wPubDeck);
	btnPubDeckSend = env->addButton(Scale(60, 410, 210, 435), wPubDeck, BUTTON_PUBDECK_SEND, L"Pubblica");
	env->addButton(Scale(230, 410, 380, 435), wPubDeck, BUTTON_PUBDECK_CLOSE, L"Chiudi");

	wCloudDecks = env->addWindow(Scale(300, 60, 740, 540), false, L"Mazzi nel cloud");
	wCloudDecks->getCloseButton()->setVisible(false);
	wCloudDecks->setVisible(false);
	lstCloudDecks = env->addListBox(Scale(20, 30, 420, 200), wCloudDecks, LISTBOX_CLOUDDECK_SLOT, true);
	env->addStaticText(L"Mazzo locale da caricare:", Scale(20, 208, 420, 228), false, false, wCloudDecks);
	cbCloudDeckLocal = env->addComboBox(Scale(20, 230, 420, 255), wCloudDecks);
	cbCloudDeckLocal->setMaxSelectionRows(10);
	btnCloudDeckPut = env->addButton(Scale(20, 264, 210, 290), wCloudDecks, BUTTON_CLOUDDECK_PUT, L"Carica");
	btnCloudDeckDelete = env->addButton(Scale(230, 264, 420, 290), wCloudDecks, BUTTON_CLOUDDECK_DELETE, L"Cancella");
	chkCloudDeckOnStart = env->addCheckBox(gGameConfig->cloudDecksOnStart, Scale(20, 300, 420, 322), wCloudDecks, CHECKBOX_CLOUDDECK_ONSTART, L"Aggiorna i mazzi dal cloud all'avvio");
	btnCloudDeckSync = env->addButton(Scale(20, 328, 210, 354), wCloudDecks, BUTTON_CLOUDDECK_SYNC, L"Scarica ora");
	stCloudDeckStatus = env->addStaticText(L"", Scale(20, 362, 420, 430), false, true, wCloudDecks);
	env->addButton(Scale(150, 438, 290, 462), wCloudDecks, BUTTON_CLOUDDECK_CLOSE, L"Chiudi");

	wCloud = env->addWindow(Scale(300, 60, 740, 540), false, L"I miei replay (cloud)");
	wCloud->getCloseButton()->setVisible(false);
	wCloud->setVisible(false);
	lstCloud = env->addListBox(Scale(20, 30, 420, 190), wCloud, -1, true);
	env->addStaticText(L"Carica sul cloud un replay di torneo salvato qui:", Scale(20, 196, 420, 216), false, false, wCloud);
	cbCloudLocal = env->addComboBox(Scale(20, 218, 420, 243), wCloud);
	env->addButton(Scale(20, 250, 200, 274), wCloud, BUTTON_CLOUD_UPLOAD, L"Carica sul cloud");
	env->addStaticText(L"Titolo", Scale(20, 286, 100, 306), false, false, wCloud);
	ebCloudTitle = env->addEditBox(L"", Scale(100, 284, 420, 309), true, wCloud);
	env->addStaticText(L"Descrizione", Scale(20, 316, 100, 336), false, false, wCloud);
	ebCloudDesc = env->addEditBox(L"", Scale(100, 314, 420, 339), true, wCloud);
	env->addButton(Scale(20, 346, 140, 370), wCloud, BUTTON_CLOUD_PUBLISH, L"Pubblica");
	env->addButton(Scale(150, 346, 300, 370), wCloud, BUTTON_CLOUD_UNPUBLISH, L"Ritira pubblicazione");
	env->addButton(Scale(310, 346, 420, 370), wCloud, BUTTON_CLOUD_DELETE, L"Cancella");
	stCloudStatus = env->addStaticText(L"", Scale(20, 378, 420, 430), false, true, wCloud);
	env->addButton(Scale(150, 438, 290, 462), wCloud, BUTTON_CLOUD_CLOSE, L"Chiudi");
}

void Game::QueuePlayerApi(std::string tag, std::string method, std::string path, std::string body) {
	player_api_queue.push_back({ std::move(tag), std::move(method), std::move(path), std::move(body) });
}

void Game::RequestPlayerProfile() {
	QueuePlayerApi("profile", "GET", "/v1/client/me/profile");
}

void Game::RefreshPublications() {
	QueuePlayerApi("pubs", "GET", "/v1/client/me/publications");
}

void Game::ClearPlayerProfile() {
	forced_nickname.clear();
	player_profile_requested = false;
	player_api_queue.clear();
	player_role.clear();
	judge_ticket.clear();
	judge_unread.clear();
	ebNickName->setEnabled(true);
	ebNickNameOnline->setEnabled(true);
	for(auto* w : { wPlayer, wPubDeck, wCloud, wCloudDecks })
		if(w->isVisible())
			HideElement(w);
	if(!wMainMenu->isVisible())
		ShowElement(wMainMenu);
}

// Una volta a frame dal MainLoop (thread principale, gMutex preso dal chiamante dove serve).
void Game::PlayerPanelFrame() {
	const bool logged = !gGameConfig->titleCredential.empty() && gTitleCheckin;
	if(logged && !player_profile_requested) {
		player_profile_requested = true;
		RequestPlayerProfile();
		// FASE 122B: sincronizzazione all'apertura, dopo la richiesta del profilo (stessa credenziale, stessa coda).
		if(gGameConfig->cloudDecksOnStart)
			QueuePlayerApi("cloud_sync", "GET", "/v1/client/me/cloud-decks");
	}
	wBtnPlayer->setVisible(logged && wMainMenu->isVisible());
	UpdateCloudDeckButtons();
	if(!gTitleCheckin)
		return;
	TitleCheckin::ApiResult result;
	if(gTitleCheckin->TakeApiResult(result)) {
		if(result.tag == "deepdeck")
			FinishDeckFromLink(result);
		else
			OnPlayerApiResult(result);
	}
	if(!deck_link_id.empty() && !deck_link_started && !gTitleCheckin->ApiRequestRunning()) {
		// The id was validated by ParseDeckLink: it is 12 URL-safe chars.
		if(gTitleCheckin->StartPublicGet("deepdeck", "/v1/public/decks/" + deck_link_id + "/ydk"))
			deck_link_started = true;
		else {
			deck_link_id.clear();
			PopupMessage(L"Mazzo non trovato o non raggiungibile.");
		}
	}
	// FASE 123C: bottone visibile solo dove il contratto lo vuole; ritiro periodico del biglietto.
	btnJudge->setVisible(JudgeButtonVisible(logged, (dInfo.isInDuel || dInfo.isReplay) && !dInfo.isSingleMode, dInfo.isTournamentRoom, player_role));
	if(!judge_ticket.empty() && NowMs() >= judge_next_poll_ms && player_api_queue.empty() && !gTitleCheckin->ApiRequestRunning()) {
		judge_next_poll_ms = NowMs() + 3000;
		QueuePlayerApi("judge_poll", "GET", "/v1/client/judge/questions/" + judge_ticket);
	}
	if(!player_api_queue.empty() && !gTitleCheckin->ApiRequestRunning()) {
		auto call = std::move(player_api_queue.front());
		player_api_queue.pop_front();
		if(!gTitleCheckin->StartApiRequest(call.tag, call.method, call.path, call.body)) {
			TitleCheckin::ApiResult failed;
			failed.tag = call.tag;
			OnPlayerApiResult(failed);
		}
	}
}

void Game::OpenDeckFromLink(const std::string& id) {
	deck_link_id = id;
	deck_link_started = false;
}

void Game::FinishDeckFromLink(const TitleCheckin::ApiResult& r) {
	deck_link_id.clear();
	deck_link_started = false;
	const auto fail = [&] { PopupMessage(L"Mazzo non trovato o non raggiungibile."); };
	if(!r.transport_ok || r.status_code != 200 || r.body.empty())
		return fail();
	deck_link_name = ygo::deep_link::SafeDeckFileName(ygo::deep_link::DeckNameFromDisposition(r.disposition));
	deck_link_body = r.body;
	// A link must not write into the deck folder on its own: ask first.
	menuHandler.prev_operation = ACTION_CONFIRM_DECK_LINK;
	stQMessage->setText(epro::format(L"Un link vuole salvare il mazzo \"{}\" nella tua cartella dei mazzi e aprirlo nell'editor. Vuoi continuare?", Utils::ToUnicodeIfNeeded(deck_link_name)).data());
	SetCentered(wQuery);
	PopupElement(wQuery);
}

void Game::ConfirmDeckFromLink(bool yes) {
	const auto name = std::move(deck_link_name);
	const auto body = std::move(deck_link_body);
	deck_link_name.clear();
	deck_link_body.clear();
	if(!yes)
		return;
	const auto fail = [&] { PopupMessage(L"Mazzo non trovato o non raggiungibile."); };
	const auto folder = epro::path_string(DeckManager::GetDeckFolder());
	const auto path_of = [&](const std::string& stem) { return folder + EPRO_TEXT("/") + Utils::ToPathString(stem) + EPRO_TEXT(".ydk"); };
	const auto stem = ygo::deep_link::FirstFreeDeckStem(name, [&](const std::string& s) { return Utils::FileExists(path_of(s)); });
	if(stem.empty()) {
		PopupMessage(L"Nella cartella dei mazzi non c'e' un nome libero per questo mazzo: rinomina o elimina qualche mazzo e riprova.");
		return;
	}
	const auto path = path_of(stem);
	{
		FileStream f{ path, FileStream::out | FileStream::binary | FileStream::trunc };
		if(f.fail())
			return fail();
		f.write(body.data(), body.size());
		if(f.fail())
			return fail();
	}
	// Same sequence as BUTTON_DECK_EDIT with a dropped .ydk (menu_handler.cpp).
	if(!wMainMenu->isVisible() || !deckBuilder.SetCurrentDeckFromFile(path, true))
		return fail();
	RefreshDeck(cbDBDecks);
	ebDeckname->setText(Utils::ToUnicodeIfNeeded(Utils::GetFileName(path)).data());
	cbDBDecks->setSelected(-1);
	HideElement(wMainMenu);
	deckBuilder.Initialize();
}

void Game::OnPlayerApiResult(const TitleCheckin::ApiResult& r) {
	const auto j = nlohmann::json::parse(r.body, nullptr, false);
	auto say = [&](irr::gui::IGUIStaticText* st, const std::wstring& text) { st->setText(text.data()); };
	const auto& tag = r.tag;
	if(tag == "profile") {
		if(!Ok(r) || !j.is_object())
			return; // all'avvio niente rumore: il nickname resta quello locale
		const auto nick = JStr(j, "nickname");
		player_role = JStr(j, "role");
		QueuePlayerApi("judge_pending", "GET", "/v1/client/judge/pending"); // "hai una risposta dell'Oracolo da leggere"
		const auto& rank = j.contains("rank") ? j["rank"] : nlohmann::json();
		const auto& xp = j.contains("experience") ? j["experience"] : nlohmann::json();
		std::wstring rank_text = W(JStr(rank, "grade"));
		if(rank.is_object() && rank.contains("league") && rank["league"].is_number())
			rank_text += epro::format(L" (lega {})", JNum(rank, "league"));
		std::wstring text = epro::format(L"Nickname: {}\nRuolo: {}\nRango: {}\nEsperienza: livello {} - {} ({}%)",
			W(nick), W(JStr(j, "role")), rank_text, JNum(xp, "level"), W(JStr(xp, "title")),
			static_cast<long long>(xp.is_object() && xp.contains("fraction") && xp["fraction"].is_number() ? xp["fraction"].get<double>() * 100.0 : 0.0));
		stPlayerProfile->setText(text.data());
		if(!nick.empty()) {
			// Nickname forzato: quello della piattaforma, non modificabile.
			forced_nickname = nick;
			const auto wnick = W(nick);
			ebNickName->setText(wnick.data());
			ebNickNameOnline->setText(wnick.data());
			ebNickName->setEnabled(false);
			ebNickNameOnline->setEnabled(false);
		}
		return;
	}
	if(tag == "judge_ask") {
		if(!Ok(r) || !j.is_object() || JStr(j, "ticket").empty()) {
			stJudgeStatus->setText(JudgeErrorMessage(JStr(j, "error")).data());
			btnJudgeSend->setEnabled(true);
			return;
		}
		judge_ticket = JStr(j, "ticket");
		judge_tick = 0;
		judge_next_poll_ms = NowMs() + 1500;
		stJudgeStatus->setText(W(JudgeWaitMessage(judge_tick++, -1)).data());
		return;
	}
	if(tag == "judge_poll") {
		const auto state = JStr(j, "state");
		if(!Ok(r) || !j.is_object()) {
			if(r.transport_ok && r.status_code == 404)
				judge_ticket.clear();
			return; // un errore di rete passeggero: si riprova al prossimo ritiro
		}
		if(state == "queued" || state == "running") {
			stJudgeStatus->setText(W(JudgeWaitMessage(judge_tick++, state == "queued" ? static_cast<int>(JNum(j, "position")) : -1)).data());
			return;
		}
		judge_ticket.clear();
		btnJudgeSend->setEnabled(true);
		ShowJudgeAnswer(r.body);
		return;
	}
	if(tag == "judge_pending") {
		judge_unread.clear();
		if(Ok(r) && j.is_object() && j.contains("tickets") && j["tickets"].is_array())
			for(const auto& t : j["tickets"])
				judge_unread.push_back(JStr(t, "ticket"));
		if(!judge_unread.empty())
			PopupMessage(L"Hai una risposta dell'Oracolo da leggere: aprila dal tuo profilo (\"Risposta dell'Oracolo\").");
		return;
	}
	if(tag == "pubs") {
		if(!Ok(r) || !j.is_object()) {
			say(stPlayerStatus, GENERIC_ERROR);
			return;
		}
		lstPlayerPubs->clear();
		player_pubs.clear();
		cloud_ids.clear();
		cloud_published.clear();
		lstCloud->clear();
		if(j.contains("decks") && j["decks"].is_array()) {
			for(const auto& d : j["decks"]) {
				auto name = JStr(d, "name");
				if(name.empty()) name = JStr(d, "title");
				lstPlayerPubs->addItem(epro::format(L"[Mazzo] {}", W(name)).data());
				player_pubs.push_back({ true, JStr(d, "id"), true, name, JStr(d, "description") });
			}
		}
		if(j.contains("replays") && j["replays"].is_array()) {
			for(const auto& c : j["replays"]) {
				const bool published = c.contains("published") && c["published"].is_object();
				const auto line = epro::format(L"{}{}", W(JStr(c, "name")),
					published ? epro::format(L"  [pubblicato, {} visite]", JNum(c["published"], "views")) : std::wstring{});
				lstPlayerPubs->addItem(epro::format(L"[Replay] {}", line).data());
				player_pubs.push_back({ false, JStr(c, "id"), published });
				lstCloud->addItem(line.data());
				cloud_ids.push_back(JStr(c, "id"));
				cloud_published.push_back(published);
			}
		}
		const auto& slots = j.contains("slots") ? j["slots"] : nlohmann::json();
		stPlayerSlots->setText(epro::format(L"{}   {}   {}", Slot(slots, "decks", L"Mazzi"), Slot(slots, "cloud", L"Replay cloud"), Slot(slots, "published", L"Replay pubblicati")).data());
		return;
	}
	if(tag == "cloud_list" || tag == "cloud_sync" || tag == "cloud_sync_manual") {
		const bool manual = tag == "cloud_sync_manual";
		if(!Ok(r) || !j.is_array()) {
			if(tag != "cloud_sync") // all'avvio un fallimento di rete non fa rumore
				say(stCloudDeckStatus, GENERIC_ERROR);
			return;
		}
		for(auto& c : cloud_slots)
			c = CloudSlot{};
		for(const auto& d : j) {
			const auto n = JNum(d, "slot");
			if(n >= 1 && n <= 10)
				cloud_slots[n - 1] = { true, JStr(d, "name"), JStr(d, "updated_at") };
		}
		FillCloudDeckList();
		if(tag != "cloud_list")
			ApplyCloudDecks(r.body, manual);
		return;
	}
	if(tag == "cloud_put" || tag == "cloud_del") {
		if(Ok(r)) {
			cloud_wait_until_ms = NowMs() + 10000;
			say(stCloudDeckStatus, tag == "cloud_put" ? L"Mazzo salvato nel cloud." : L"Slot svuotato.");
			QueuePlayerApi("cloud_list", "GET", "/v1/client/me/cloud-decks");
			return;
		}
		const auto err = r.transport_ok ? nlohmann::json::parse(r.body, nullptr, false) : nlohmann::json();
		if(r.status_code == 429) {
			const auto secs = std::clamp(JNum(err, "retry_after_s"), 1LL, 60LL);
			cloud_wait_until_ms = NowMs() + secs * 1000;
			say(stCloudDeckStatus, epro::format(L"Aspetta ancora {} secondi e riprova.", secs));
		} else if(r.status_code == 409) {
			say(stCloudDeckStatus, L"Hai già un altro slot con questo nome.");
		} else if(r.status_code == 400) {
			say(stCloudDeckStatus, L"Questo mazzo non si può caricare: controlla nome e contenuto.");
		} else {
			say(stCloudDeckStatus, GENERIC_ERROR);
		}
		return;
	}
	// Azioni: esito in una frase, poi si rilegge l'elenco.
	struct Action { const char* tag; const wchar_t* done; irr::gui::IGUIStaticText* st; };
	const Action actions[] = {
		{ "deck_publish", L"Mazzo pubblicato.", stPubDeckStatus },
		{ "deck_edit", L"Mazzo modificato.", stPubDeckStatus },
		{ "deck_delete", L"Mazzo cancellato.", stPlayerStatus },
		{ "replay_upload", L"Replay caricato sul cloud.", stCloudStatus },
		{ "replay_publish", L"Replay pubblicato.", stCloudStatus },
		{ "replay_unpublish", L"Pubblicazione ritirata.", stCloudStatus },
		{ "replay_delete", L"Replay cancellato.", stCloudStatus },
	};
	for(const auto& a : actions) {
		if(tag != a.tag)
			continue;
		if(!Ok(r)) {
			say(a.st, ErrorText(r));
			return;
		}
		std::wstring text = a.done;
		if(tag == "replay_publish" && !JStr(j, "publicId").empty())
			text += epro::format(L" Codice: {}", W(JStr(j, "publicId")));
		say(a.st, text);
		if(tag == "deck_publish") {
			ebPubDeckTitle->setText(L"");
			ebPubDeckDesc->setText(L"");
		}
		RefreshPublications();
	}
}

void Game::FillCloudDeckList() {
	const auto sel = lstCloudDecks->getSelected();
	lstCloudDecks->clear();
	for(int i = 0; i < 10; i++) {
		const auto& c = cloud_slots[i];
		std::string when = c.updated_at.substr(0, 16);
		std::replace(when.begin(), when.end(), 'T', ' ');
		lstCloudDecks->addItem(c.used ? epro::format(L"{}. {}  ({})", i + 1, W(c.name), W(when)).data() : epro::format(L"{}. (vuoto)", i + 1).data());
	}
	lstCloudDecks->setSelected(sel >= 0 ? sel : 0);
	cloud_delete_confirming = false;
	UpdateCloudDeckButtons();
}

// Ogni frame: i testi dei pulsanti seguono lo slot scelto e il conto alla rovescia.
void Game::UpdateCloudDeckButtons() {
	if(!wCloudDecks->isVisible())
		return;
	const auto sel = lstCloudDecks->getSelected();
	const bool used = sel >= 0 && sel < 10 && cloud_slots[sel].used;
	const long long left_ms = cloud_wait_until_ms - NowMs();
	const int secs = left_ms > 0 ? static_cast<int>((left_ms + 999) / 1000) : 0;
	const int state = secs * 4 + (used ? 1 : 0) + (cloud_delete_confirming ? 2 : 0);
	if(state == cloud_wait_shown)
		return;
	cloud_wait_shown = state;
	const std::wstring wait = secs ? epro::format(L" ({} s)", secs) : std::wstring{};
	btnCloudDeckPut->setText(((used ? L"Sostituisci" : L"Carica") + wait).data());
	btnCloudDeckDelete->setText(((cloud_delete_confirming ? L"Conferma cancellazione" : L"Cancella") + wait).data());
	btnCloudDeckPut->setEnabled(!secs);
	btnCloudDeckDelete->setEnabled(!secs && used);
}

// Regola 3 della forma (cloud-mazzi.md): mai cancellare file locali, i file stanno nella radice di deck/ come "[Cloud] <slot>.ydk".
void Game::ApplyCloudDecks(const std::string& body, bool manual) {
	const auto j = nlohmann::json::parse(body, nullptr, false);
	if(!j.is_array())
		return;
	const auto folder = epro::path_string(DeckManager::GetDeckFolder());
	int downloaded = 0, kept = 0, same = 0, failed = 0;
	for(const auto& d : j) {
		const auto name = JStr(d, "name");
		const auto cloud_time = ParseCloudTimestamp(JStr(d, "updated_at"));
		if(!d.contains("ydk") || !d["ydk"].is_string() || cloud_time < 0 || !CloudDeckNameIsSafeFileName(name)) {
			failed++;
			continue;
		}
		const auto ydk = d["ydk"].get<std::string>();
		const auto path = folder + EPRO_TEXT("/") + Utils::ToPathString(CloudDeckFileName(name)) + EPRO_TEXT(".ydk");
		long long mtime = 0;
		std::string current;
		const bool exists = LocalMtime(path, mtime) && ReadFile(path, current);
		switch(DecideCloudDeck(exists, mtime, cloud_time, exists && current == ydk)) {
		case CloudDeckAction::Skip: same++; continue;
		case CloudDeckAction::KeepLocal: kept++; continue;
		case CloudDeckAction::Download: break;
		}
		{
			FileStream f{ path, FileStream::out | FileStream::binary | FileStream::trunc };
			if(f.fail()) { failed++; continue; }
			f.write(ydk.data(), ydk.size());
			if(f.fail()) { failed++; continue; }
		}
		SetLocalMtime(path, cloud_time);
		downloaded++;
	}
	ErrorLog("Cloud decks: {} scaricati, {} locali piu' recenti, {} uguali, {} non applicati.", downloaded, kept, same, failed);
	if(downloaded)
		RefreshDeck(cbDBDecks);
	if(manual)
		stCloudDeckStatus->setText(epro::format(L"Scaricati {} mazzi in deck/ come \"[Cloud] ...\" ({} già aggiornati o più recenti qui).", downloaded, same + kept).data());
}

void Game::OnPubDeckDeckChanged() {
	lstPubDeckCards->clear();
	pubdeck_ydk.clear();
	const auto sel = cbPubDeckDeck->getSelected();
	if(sel < 0)
		return;
	Deck deck;
	if(!DeckManager::LoadDeckFromFile(Utils::ToPathString(cbPubDeckDeck->getItem(sel)), deck, true)) {
		stPubDeckStatus->setText(L"Impossibile leggere questo mazzo.");
		return;
	}
	stPubDeckStatus->setText(L"");
	const auto names = DeckManager::ExportDeckCardNames(deck);
	size_t pos = 0;
	while(pos < names.size()) {
		auto end = names.find(L'\n', pos);
		if(end == std::wstring::npos)
			end = names.size();
		lstPubDeckCards->addItem(names.substr(pos, end - pos).data());
		pos = end + 1;
	}
	auto section = [this](const char* header, const Deck::Vector& cards) {
		pubdeck_ydk += header;
		for(auto card : cards)
			pubdeck_ydk += epro::to_string(card->getRealCode()) + "\n";
	};
	section("#main\n", deck.main);
	section("#extra\n", deck.extra);
	section("!side\n", deck.side);
	if(ebPubDeckTitle->getText()[0] == 0)
		ebPubDeckTitle->setText(cbPubDeckDeck->getItem(sel));
}

void Game::ShowJudgeAnswer(const std::string& body) {
	const auto j = nlohmann::json::parse(body, nullptr, false);
	const auto state = JStr(j, "state");
	lstJudgeCards->clear();
	judge_card_texts.clear();
	stJudgeCardText->setText(L"");
	if(state == "done") {
		stJudgeStatus->setText(L"Risposta dell'Oracolo:");
		stJudgeAnswer->setText(W(JudgeHighlight(JStr(j, "answer"))).data());
		if(j.contains("cards") && j["cards"].is_array())
			for(const auto& c : j["cards"]) {
				lstJudgeCards->addItem(W(JStr(c, "name")).data());
				judge_card_texts.push_back(W(JStr(c, "text")));
			}
	} else if(state == "facts_only") {
		stJudgeStatus->setText(L"L'Oracolo non e' riuscito a rispondere con sicurezza: ecco i fatti del replay (la domanda non e' stata scalata).");
		stJudgeAnswer->setText(W(JStr(j, "facts")).data());
	} else if(state == "rejected") {
		stJudgeStatus->setText(L"Domanda non accettata.");
		stJudgeAnswer->setText(L"");
	} else {
		stJudgeStatus->setText(L"La richiesta e' scaduta, riprova.");
		stJudgeAnswer->setText(L"");
	}
}

// Replay da mandare al bot: in un replay il file aperto (gia' oscurato), in una partita lo stato visto finora.
bool Game::CollectJudgeReplay(std::string& out) {
	if(dInfo.isReplay) {
		const auto name = ReplayMode::cur_replay.GetReplayName();
		if(name.empty() || (!ReadFile(name, out) && !ReadFile(Replay::GetReplayFilePath(name), out)))
			return false;
	} else {
		std::vector<uint8_t> blob;
		if(!DuelClient::BuildJudgeSnapshot(blob))
			return false;
		out.assign(blob.begin(), blob.end());
	}
	return !out.empty() && out.size() <= 512u * 1024u; // stesso tetto del bot (MAX_YRPX_BYTES)
}

void Game::OnPlayerPanelButton(int id) {
	auto selected_cloud = [this]() -> int {
		const auto sel = lstCloud->getSelected();
		return (sel >= 0 && static_cast<size_t>(sel) < cloud_ids.size()) ? sel : -1;
	};
	switch(id) {
	case BUTTON_PLAYER_ICON:
		if(wPlayer->isVisible())
			break;
		stPlayerStatus->setText(L"");
		RequestPlayerProfile();
		RefreshPublications();
		HideElement(wMainMenu);
		ShowElement(wPlayer);
		break;
	case BUTTON_PLAYER_CLOSE:
		HideElement(wPlayer);
		ShowElement(wMainMenu);
		break;
	case BUTTON_PLAYER_PUBDECK:
		pubdeck_edit_id.clear();
		wPubDeck->setText(L"Pubblica un deck");
		cbPubDeckDeck->setVisible(true);
		lstPubDeckCards->setVisible(true);
		btnPubDeckSend->setText(L"Pubblica");
		stPubDeckStatus->setText(L"");
		RefreshDeck(cbPubDeckDeck);
		FillLocalReplays(cbPubDeckReplay, pubdeck_replay_files, true, MAX_ATTACHED_REPLAY);
		ebPubDeckTitle->setText(L"");
		ebPubDeckDesc->setText(L"");
		OnPubDeckDeckChanged();
		HideElement(wPlayer);
		ShowElement(wPubDeck);
		break;
	case BUTTON_PLAYER_EDIT: {
		const auto sel = lstPlayerPubs->getSelected();
		if(sel < 0 || static_cast<size_t>(sel) >= player_pubs.size() || !player_pubs[sel].is_deck || player_pubs[sel].id.empty()) {
			stPlayerStatus->setText(L"Seleziona prima un mazzo pubblicato dall'elenco.");
			break;
		}
		pubdeck_edit_id = player_pubs[sel].id;
		wPubDeck->setText(L"Modifica pubblicazione");
		cbPubDeckDeck->setVisible(false); // le carte non si modificano: per cambiarle si ripubblica
		lstPubDeckCards->setVisible(false);
		btnPubDeckSend->setText(L"Salva");
		FillLocalReplays(cbPubDeckReplay, pubdeck_replay_files, false, MAX_ATTACHED_REPLAY, true);
		ebPubDeckTitle->setText(W(player_pubs[sel].name).data());
		ebPubDeckDesc->setText(W(player_pubs[sel].description).data());
		stPubDeckStatus->setText(L"Descrizione vuota = la cancella. Le carte non si modificano.");
		HideElement(wPlayer);
		ShowElement(wPubDeck);
		break;
	}
	case BUTTON_PUBDECK_CLOSE:
		HideElement(wPubDeck);
		ShowElement(wPlayer);
		break;
	case BUTTON_PUBDECK_SEND: {
		if(!pubdeck_edit_id.empty()) {
			nlohmann::json body = nlohmann::json::object();
			const std::string title = Utils::ToUTF8IfNeeded(std::wstring(ebPubDeckTitle->getText()));
			const std::string desc = Utils::ToUTF8IfNeeded(std::wstring(ebPubDeckDesc->getText()));
			if(!title.empty()) body["name"] = title;
			body["description"] = desc; // 98N: campo precompilato, vuoto = svuota
			const auto rsel = cbPubDeckReplay->getSelected();
			if(rsel == static_cast<int>(EDIT_REMOVE)) {
				body["replay"] = nullptr;
			} else if(rsel > static_cast<int>(EDIT_REMOVE) && static_cast<size_t>(rsel) < pubdeck_replay_files.size()) {
				std::string content;
				if(!ReadFile(pubdeck_replay_files[rsel], content) || content.empty() || content.size() > MAX_ATTACHED_REPLAY) {
					stPubDeckStatus->setText(L"Il replay allegato supera 64 KB o non si legge.");
					break;
				}
				body["replay"] = base64_encode<std::string>(content);
			}
			stPubDeckStatus->setText(L"Invio in corso...");
			QueuePlayerApi("deck_edit", "PUT", "/v1/client/decks/" + pubdeck_edit_id, body.dump());
			break;
		}
		const std::string title = Utils::ToUTF8IfNeeded(std::wstring(ebPubDeckTitle->getText()));
		if(pubdeck_ydk.empty() || title.empty()) {
			stPubDeckStatus->setText(L"Scegli un mazzo e scrivi un titolo.");
			break;
		}
		nlohmann::json body{ { "name", title }, { "description", Utils::ToUTF8IfNeeded(std::wstring(ebPubDeckDesc->getText())) }, { "ydk", pubdeck_ydk } };
		const auto rsel = cbPubDeckReplay->getSelected();
		if(rsel > 0 && static_cast<size_t>(rsel) < pubdeck_replay_files.size()) {
			std::string content;
			if(!ReadFile(pubdeck_replay_files[rsel], content) || content.empty() || content.size() > MAX_ATTACHED_REPLAY) {
				stPubDeckStatus->setText(L"Il replay allegato supera 64 KB o non si legge.");
				break;
			}
			body["replay"] = base64_encode<std::string>(content);
		}
		stPubDeckStatus->setText(L"Invio in corso...");
		QueuePlayerApi("deck_publish", "POST", "/v1/client/decks", body.dump());
		break;
	}
	case BUTTON_PLAYER_DELETE: {
		const auto sel = lstPlayerPubs->getSelected();
		if(sel < 0 || static_cast<size_t>(sel) >= player_pubs.size()) {
			stPlayerStatus->setText(L"Seleziona prima una pubblicazione dall'elenco.");
			break;
		}
		const auto& pub = player_pubs[sel];
		if(pub.id.empty()) {
			stPlayerStatus->setText(GENERIC_ERROR);
			break;
		}
		stPlayerStatus->setText(L"Cancellazione in corso...");
		QueuePlayerApi(pub.is_deck ? "deck_delete" : "replay_delete", "DELETE", (pub.is_deck ? "/v1/client/decks/" : "/v1/client/replays/") + pub.id);
		break;
	}
	case BUTTON_PLAYER_CLOUD:
		stCloudStatus->setText(L"");
		FillLocalReplays(cbCloudLocal, cloud_local_files, false, 0);
		RefreshPublications();
		HideElement(wPlayer);
		ShowElement(wCloud);
		break;
	case BUTTON_PLAYER_CLOUDDECKS:
		stCloudDeckStatus->setText(L"");
		cloud_delete_confirming = false;
		cloud_wait_shown = -1;
		chkCloudDeckOnStart->setChecked(gGameConfig->cloudDecksOnStart);
		RefreshDeck(cbCloudDeckLocal);
		FillCloudDeckList();
		QueuePlayerApi("cloud_list", "GET", "/v1/client/me/cloud-decks");
		HideElement(wPlayer);
		ShowElement(wCloudDecks);
		break;
	case BUTTON_CLOUDDECK_CLOSE:
		HideElement(wCloudDecks);
		ShowElement(wPlayer);
		break;
	case CHECKBOX_CLOUDDECK_ONSTART:
		gGameConfig->cloudDecksOnStart = chkCloudDeckOnStart->isChecked();
		SaveConfig();
		break;
	case BUTTON_CLOUDDECK_SYNC:
		stCloudDeckStatus->setText(L"Scarico...");
		QueuePlayerApi("cloud_sync_manual", "GET", "/v1/client/me/cloud-decks");
		break;
	case BUTTON_CLOUDDECK_PUT: {
		const auto slot = lstCloudDecks->getSelected();
		const auto dsel = cbCloudDeckLocal->getSelected();
		if(slot < 0 || slot >= 10 || dsel < 0) {
			stCloudDeckStatus->setText(L"Scegli uno slot e un mazzo locale.");
			break;
		}
		// lo slot si chiama come il mazzo, senza il prefisso "[Cloud] ".
		auto name = CloudDeckSlotName(Utils::ToUTF8IfNeeded(std::wstring(cbCloudDeckLocal->getItem(dsel))));
		std::string ydk;
		if(!ReadFile(DeckManager::GetDeckPath(Utils::ToPathString(cbCloudDeckLocal->getItem(dsel))), ydk) || ydk.empty()) {
			stCloudDeckStatus->setText(L"Impossibile leggere questo mazzo.");
			break;
		}
		stCloudDeckStatus->setText(L"Caricamento in corso...");
		QueuePlayerApi("cloud_put", "PUT", "/v1/client/cloud-decks/" + std::to_string(slot + 1), nlohmann::json{ { "name", name }, { "ydk", ydk } }.dump());
		break;
	}
	case BUTTON_CLOUDDECK_DELETE: {
		const auto slot = lstCloudDecks->getSelected();
		if(slot < 0 || slot >= 10 || !cloud_slots[slot].used)
			break;
		if(!cloud_delete_confirming) {
			cloud_delete_confirming = true;
			cloud_wait_shown = -1;
			stCloudDeckStatus->setText(L"Cancella solo lo slot nel cloud: i file sui dispositivi restano. Premi di nuovo per confermare.");
			break;
		}
		cloud_delete_confirming = false;
		cloud_wait_shown = -1;
		stCloudDeckStatus->setText(L"Cancellazione in corso...");
		QueuePlayerApi("cloud_del", "DELETE", "/v1/client/cloud-decks/" + std::to_string(slot + 1));
		break;
	}
	case BUTTON_JUDGE_PENDING:
		if(judge_unread.empty()) {
			PopupMessage(L"Nessuna risposta dell'Oracolo da leggere.");
			break;
		}
		judge_ticket = judge_unread.front();
		judge_unread.erase(judge_unread.begin());
		judge_next_poll_ms = 0;
		stJudgeStatus->setText(L"Recupero la risposta...");
		stJudgeAnswer->setText(L"");
		lstJudgeCards->clear();
		stJudgeCardText->setText(L"");
		HideElement(wPlayer);
		ShowElement(wJudge);
		break;
	case BUTTON_JUDGE_OPEN:
		if(wJudge->isVisible())
			break;
		ShowElement(wJudge);
		break;
	case BUTTON_JUDGE_CLOSE:
		HideElement(wJudge);
		break;
	case LISTBOX_JUDGE_CARD: {
		const auto sel = lstJudgeCards->getSelected();
		if(sel >= 0 && static_cast<size_t>(sel) < judge_card_texts.size())
			stJudgeCardText->setText(judge_card_texts[sel].data());
		break;
	}
	case BUTTON_JUDGE_SEND: {
		const std::string question = Utils::ToUTF8IfNeeded(std::wstring(ebJudgeQuestion->getText()));
		if(question.empty()) {
			stJudgeStatus->setText(JudgeErrorMessage("domanda_non_valida").data());
			break;
		}
		std::string replay;
		if(!CollectJudgeReplay(replay)) {
			stJudgeStatus->setText(L"L'Oracolo si apre da una partita o da un replay (non riesco a leggerne lo stato).");
			break;
		}
		btnJudgeSend->setEnabled(false);
		stJudgeAnswer->setText(L"");
		lstJudgeCards->clear();
		stJudgeCardText->setText(L"");
		stJudgeStatus->setText(L"Invio in corso...");
		QueuePlayerApi("judge_ask", "POST", "/v1/client/judge/questions",
			nlohmann::json{ { "question", question }, { "yrpx", base64_encode<std::string>(replay) }, { "tournamentDuel", dInfo.isTournamentRoom }, { "spectator", dInfo.player_type >= 7 } }.dump());
		break;
	}
	case BUTTON_CLOUD_CLOSE:
		HideElement(wCloud);
		ShowElement(wPlayer);
		break;
	case BUTTON_CLOUD_UPLOAD: {
		const auto sel = cbCloudLocal->getSelected();
		std::string content;
		if(sel < 0 || static_cast<size_t>(sel) >= cloud_local_files.size()) {
			stCloudStatus->setText(L"Nessun replay locale da caricare.");
			break;
		}
		if(!ReadFile(cloud_local_files[sel], content) || content.empty()) {
			stCloudStatus->setText(L"Impossibile leggere questo replay.");
			break;
		}
		stCloudStatus->setText(L"Caricamento in corso...");
		QueuePlayerApi("replay_upload", "POST", "/v1/client/replays", nlohmann::json{ { "yrpx", base64_encode<std::string>(content) } }.dump());
		break;
	}
	case BUTTON_CLOUD_PUBLISH: {
		const auto sel = selected_cloud();
		if(sel < 0) {
			stCloudStatus->setText(L"Seleziona prima un replay dall'elenco.");
			break;
		}
		const std::string title = Utils::ToUTF8IfNeeded(std::wstring(ebCloudTitle->getText()));
		if(title.empty()) {
			stCloudStatus->setText(L"Scrivi un titolo.");
			break;
		}
		stCloudStatus->setText(L"Pubblicazione in corso...");
		QueuePlayerApi("replay_publish", "POST", "/v1/client/replays/" + cloud_ids[sel] + "/publish",
			nlohmann::json{ { "title", title }, { "description", Utils::ToUTF8IfNeeded(std::wstring(ebCloudDesc->getText())) } }.dump());
		break;
	}
	case BUTTON_CLOUD_UNPUBLISH: {
		const auto sel = selected_cloud();
		if(sel < 0) {
			stCloudStatus->setText(L"Seleziona prima un replay dall'elenco.");
			break;
		}
		QueuePlayerApi("replay_unpublish", "DELETE", "/v1/client/replays/" + cloud_ids[sel] + "/publish");
		break;
	}
	case BUTTON_CLOUD_DELETE: {
		const auto sel = selected_cloud();
		if(sel < 0) {
			stCloudStatus->setText(L"Seleziona prima un replay dall'elenco.");
			break;
		}
		QueuePlayerApi("replay_delete", "DELETE", "/v1/client/replays/" + cloud_ids[sel]);
		break;
	}
	}
}

}
