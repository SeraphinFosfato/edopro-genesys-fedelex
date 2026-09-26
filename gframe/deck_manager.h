#ifndef DECKMANAGER_H
#define DECKMANAGER_H

#include <unordered_map>
#include <vector>
#include <map>
#include "network.h"
#include "text_types.h"
#include "fmt.h"
#include "data_manager.h"
#include "deck.h"

namespace ygo {

	struct BanlistEntry {
		int limit = 3;
		int points = 0;
	};
using banlist_content_t = std::unordered_map<uint32_t, BanlistEntry>;

using cardlist_type = std::vector<uint32_t>;

struct LFList {
	uint32_t hash;
	std::wstring listName;
	banlist_content_t content;
	bool whitelist;
	// 0 = no budget applied (FASE 32, design/banlist-distribution.md "Il
	// tetto di punti è un dato della lista, non del binario"): absent from
	// both the .conf directive ($points_budget) and the signed artifact
	// (points_budget) alike, so an LFList built from either source that
	// never set one defaults here to "no cap".
	int points_budget = 0;
	auto GetLimitationIterator(const CardDataC* pcard) const {
		auto flit = content.find(pcard->code);
		if(flit == content.end() && pcard->alias) {
			if(!whitelist || pcard->IsInArtworkOffsetRange())
				flit = content.find(pcard->alias);
		}
		return flit;
	}
	auto GetCardPoints(const CardDataC* pcard) const {
		auto flit = GetLimitationIterator(pcard);
		int points = 0;
		if(flit != content.end()) {
			points = flit->second.points;
		}
		return points;
	}
};
enum class DuelAllowedCards {
	ALLOWED_CARDS_OCG_ONLY,
	ALLOWED_CARDS_TCG_ONLY,
	ALLOWED_CARDS_OCG_TCG,
	ALLOWED_CARDS_WITH_PRERELEASE,
	ALLOWED_CARDS_ANY
};
class DeckManager {
private:
	size_t null_lflist_index = ~size_t();
	mutable std::unordered_map<uint32_t, CardDataC*> dummy_entries;
	const CardDataC* GetDummyOrMappedCardData(uint32_t code) const;
	bool load_dummies{ true };
	static inline epro::path_string deck_folder = EPRO_TEXT("./deck/");
public:
	Deck sent_deck;
	Deck pre_deck;
	std::vector<LFList> _lfList;
	static epro::path_string GetDeckPath(epro::path_stringview file) {
		return Utils::NormalizePath(epro::format(EPRO_TEXT("{}/{}.ydk"), deck_folder, file), false);
	}
	static epro::path_stringview GetDeckFolder() {
		return deck_folder;
	}
	static void SetDeckFolder(epro::path_stringview path) {
		deck_folder = path;
	}
	~DeckManager() {
		ClearDummies();
	}
	void StopDummyLoading() {
		load_dummies = false;
	}
	void ClearDummies();
	bool LoadLFListSingle(const epro::path_string& path);
	bool LoadLFListFolder(epro::path_stringview path);
	void LoadLFList();
	void RefreshLFList();
	LFList const* GetLFList(uint32_t lfhash) const;
	epro::wstringview GetLFListName(uint32_t lfhash) const;
	static void RefreshDeck(Deck& deck);
	// `points_budget` (FASE 34): the ROOM's applied cap, resolved by the
	// caller — NOT necessarily `lflist->points_budget`, which is only the
	// list's declared default. See the comment at the definition
	// (deck_manager.cpp) and design/banlist-distribution.md, "Il tetto di
	// punti: un valore della lista, una regola della stanza".
	static DeckError CheckDeckContent(const Deck& deck, LFList const* lflist, DuelAllowedCards allowedCards, uint32_t forbiddentypes, bool rituals_in_extra, int points_budget);
	static DeckError CheckDeckSize(const Deck& deck, const DeckSizes& sizes);
	static int TypeCount(const Deck::Vector& cards, uint32_t type);
	static int CountPoints(const Deck::Vector& cards, const LFList* flist);
	static int CountLegends(const Deck::Vector& cards, uint32_t type);
	static uint32_t LoadDeckFromBuffer(Deck& deck, uint32_t* dbuf, uint32_t mainc, uint32_t sidec, RITUAL_LOCATION rituals_in_extra = RITUAL_LOCATION::DEFAULT);
	static uint32_t LoadDeck(Deck& deck, const cardlist_type& mainlist, const cardlist_type& sidelist, const cardlist_type* extralist = nullptr, RITUAL_LOCATION rituals_in_extra = RITUAL_LOCATION::DEFAULT);
	static bool LoadSide(Deck& deck, uint32_t* dbuf, uint32_t mainc, uint32_t sidec, bool rituals_in_extra);
	static bool LoadDeckFromFile(epro::path_stringview file, Deck& out, bool separated = false, RITUAL_LOCATION rituals_in_extra = RITUAL_LOCATION::DEFAULT);
	static bool SaveDeck(epro::path_stringview name, const Deck& deck);
	static bool SaveDeck(epro::path_stringview name, const cardlist_type& mainlist, const cardlist_type& extralist, const cardlist_type& sidelist);
	static std::string MakeYdkEntryString(uint32_t code);
	static std::wstring ExportDeckYdke(const Deck& deck);
	static std::wstring ExportDeckCardNames(Deck deck);
	static void ImportDeckYdke(Deck& deck, epro::wstringview buffer);
	static bool ImportDeckBase64Omega(Deck& deck, epro::wstringview buffer);
	static bool DeleteDeck(Deck& deck, epro::path_stringview name);
	static bool RenameDeck(epro::path_stringview oldname, epro::path_stringview newname);
};

extern DeckManager* gdeckManager;

}

#endif //DECKMANAGER_H
