// FASE 98C: pannello del player (profilo, pubblicazioni, "Pubblica un deck",
// replay cloud). Contratto client -> bot: PHASES.md FASE 98 "Contratto bot ->
// client/web". Tutti i testi sono letterali (niente strings.conf, stessa
// ragione di wTitleAuth). Una richiesta alla volta (TitleCheckin::StartApiRequest),
// le altre aspettano in coda.
#include <algorithm>
#include <nlohmann/json.hpp>
#include <irrlicht.h>
#include "Base64.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "deep_link.h"
#include "file_stream.h"
#include "fmt.h"
#include "game.h"
#include "game_config.h"
#include "replay.h"
#include "title_checkin.h"
#include "utils.h"

namespace ygo {

namespace {
constexpr size_t MAX_ATTACHED_REPLAY = 64u * 1024u; // regole numeriche: replay allegato <= 64 KB .yrpX
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
// limite_giornaliero; ogni altro errore e' la frase generica.
std::wstring ErrorText(const TitleCheckin::ApiResult& r) {
	if(r.transport_ok) {
		auto j = nlohmann::json::parse(r.body, nullptr, false);
		const auto code = JStr(j, "error");
		const auto msg = JStr(j, "messaggio");
		if((code == "limite_totale" || code == "limite_giornaliero") && !msg.empty())
			return W(msg);
	}
	return GENERIC_ERROR;
}

bool Ok(const TitleCheckin::ApiResult& r) {
	return r.transport_ok && r.status_code >= 200 && r.status_code < 300;
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

void FillLocalReplays(irr::gui::IGUIComboBox* cb, std::vector<epro::path_string>& files, bool with_none, size_t max_bytes) {
	cb->clear();
	files.clear();
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
	// Icona in basso a destra, viola: un bottone senza bordo con un rettangolo
	// viola disegnato sotto (MainLoop), nessuna texture nuova.
	wBtnPlayer = env->addWindow(Scale(984, 566, 1024, 606));
	wBtnPlayer->getCloseButton()->setVisible(false);
	wBtnPlayer->setDraggable(false);
	wBtnPlayer->setDrawTitlebar(false);
	wBtnPlayer->setDrawBackground(false);
	wBtnPlayer->setVisible(false);
	btnPlayerIcon = env->addButton(Scale(0, 0, 40, 40), wBtnPlayer, BUTTON_PLAYER_ICON, L"P");
	btnPlayerIcon->setDrawBorder(false);

	wPlayer = env->addWindow(Scale(300, 60, 740, 540), false, L"Il mio profilo");
	wPlayer->getCloseButton()->setVisible(false);
	wPlayer->setVisible(false);
	stPlayerProfile = env->addStaticText(L"", Scale(20, 30, 420, 120), false, true, wPlayer);
	stPlayerSlots = env->addStaticText(L"", Scale(20, 122, 420, 162), false, true, wPlayer);
	lstPlayerPubs = env->addListBox(Scale(20, 168, 420, 340), wPlayer, -1, true);
	stPlayerStatus = env->addStaticText(L"", Scale(20, 346, 420, 396), false, true, wPlayer);
	env->addButton(Scale(20, 404, 150, 430), wPlayer, BUTTON_PLAYER_PUBDECK, L"Pubblica un deck");
	env->addButton(Scale(160, 404, 290, 430), wPlayer, BUTTON_PLAYER_CLOUD, L"I miei replay");
	env->addButton(Scale(300, 404, 420, 430), wPlayer, BUTTON_PLAYER_DELETE, L"Cancella");
	env->addButton(Scale(150, 440, 290, 466), wPlayer, BUTTON_PLAYER_CLOSE, L"Chiudi");

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
	env->addButton(Scale(60, 410, 210, 435), wPubDeck, BUTTON_PUBDECK_SEND, L"Pubblica");
	env->addButton(Scale(230, 410, 380, 435), wPubDeck, BUTTON_PUBDECK_CLOSE, L"Chiudi");

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
	ebNickName->setEnabled(true);
	ebNickNameOnline->setEnabled(true);
	for(auto* w : { wPlayer, wPubDeck, wCloud })
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
	}
	wBtnPlayer->setVisible(logged && wMainMenu->isVisible());
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
	auto name = ygo::deep_link::SafeDeckFileName(ygo::deep_link::DeckNameFromDisposition(r.disposition));
	const auto folder = epro::path_string(DeckManager::GetDeckFolder());
	epro::path_string path;
	for(int n = 1; n < 1000; n++) {
		path = folder + EPRO_TEXT("/") + Utils::ToPathString(n == 1 ? name : epro::format("{} ({})", name, n)) + EPRO_TEXT(".ydk");
		if(!Utils::FileExists(path))
			break;
	}
	{
		FileStream f{ path, FileStream::out | FileStream::binary | FileStream::trunc };
		if(f.fail())
			return fail();
		f.write(r.body.data(), r.body.size());
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
				player_pubs.push_back({ true, JStr(d, "id"), true });
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
	// Azioni: esito in una frase, poi si rilegge l'elenco.
	struct Action { const char* tag; const wchar_t* done; irr::gui::IGUIStaticText* st; };
	const Action actions[] = {
		{ "deck_publish", L"Mazzo pubblicato.", stPubDeckStatus },
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
		stPubDeckStatus->setText(L"");
		RefreshDeck(cbPubDeckDeck);
		FillLocalReplays(cbPubDeckReplay, pubdeck_replay_files, true, MAX_ATTACHED_REPLAY);
		ebPubDeckTitle->setText(L"");
		ebPubDeckDesc->setText(L"");
		OnPubDeckDeckChanged();
		HideElement(wPlayer);
		ShowElement(wPubDeck);
		break;
	case BUTTON_PUBDECK_CLOSE:
		HideElement(wPubDeck);
		ShowElement(wPlayer);
		break;
	case BUTTON_PUBDECK_SEND: {
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
