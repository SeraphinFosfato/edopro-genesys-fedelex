#ifndef DUELCLIENT_H
#define DUELCLIENT_H

#include "config.h"
#include <vector>
#include <deque>
#include <set>
#include <atomic>
#include <string>
#include <chrono>
#include "epro_thread.h"
#include "epro_mutex.h"
#include "epro_condition_variable.h"
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include "network.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "RNG/mt19937.h"
#include "replay.h"
#include "address.h"

namespace ygo {

class DuelClient {
private:
	static uint32_t connect_state;
	static std::vector<uint8_t> response_buf;
	static uint32_t watching;
	static uint8_t selftype;
	static bool is_host;
	// design/banlist-distribution.md, "Quando qualcun altro sostituisce la
	// lista, si dice": the hash this client sent in CTOS_CREATE_GAME, kept
	// around only long enough to compare it against the hash the server
	// echoes back in STOC_JOIN_GAME. Zero means "not currently waiting on
	// that echo" — never a real list's hash: DeckManager::LoadLFListSingle
	// only ever keeps a list whose folded hash is non-zero, and this is set
	// only while hosting (never when joining, where no list was sent).
	static uint32_t hosted_lflist_hash;
	static event_base* client_base;
	static bufferevent* client_bev;
	// FASE 83 (gframe/tls_client.h): for a TLS connection client_bev is a
	// filter on top of this raw socket bufferevent, and the raw socket's
	// lock is the ONE lock everything is serialised on (the filter has none
	// of its own). Null for a plain connection.
	static bufferevent* client_tls_raw;
	static bool client_is_tls;
	// The popup for a connection that never got established (sys string 1400,
	// or, for a TLS connection, one sentence covering network/certificate/name
	// — the reason itself is in the log, written by gframe/tls_client.cpp).
	static epro::wstringview ConnectFailedMessage();
	static bool is_closing;
	static uint64_t select_hint;
	static std::wstring event_string;
	static bool is_swapping;
	static bool stop_threads;
	static std::deque<std::vector<uint8_t>> to_analyze;
	static epro::mutex analyzeMutex;
	static epro::mutex to_analyze_mutex;
	static epro::thread parsing_thread;
	static epro::thread client_thread;
	static epro::condition_variable cv;
	// FASE 76b: when DuelClient::TournamentReconnectTick() (called every
	// frame from Game::MainLoop, see game.cpp) last fired a StartClient()
	// retry. Armed from HandleSTOCPacketLanAsync (the async parser thread)
	// and read/written from the main thread thereafter — same
	// cross-thread, no-dedicated-lock pattern already accepted for
	// try_needed/temp_ip/temp_port above, not a new category of risk.
	static std::chrono::steady_clock::time_point last_reconnect_attempt;
public:
	static RNG::mt19937 rnd;
	static epro::Address temp_ip;
	static uint16_t temp_port;
	static uint16_t temp_ver;
	// FASE 83: the name a TLS connection was made for ("" = plain). Kept
	// next to temp_ip/temp_port because the same code paths that remember
	// those to connect AGAIN (the version retry in Game::MainLoop, the
	// tournament rejoin) must reconnect the same way.
	static std::string temp_tls_name;
	static bool try_needed;
	static bool is_local_host;
	static std::atomic<bool> answered;

	static void JoinFromDiscord();
	// FASE 75, design/decisioni.md D251: same handoff as JoinFromDiscord()
	// above, for a fedelex:// table link instead of a Discord invite. `uri`
	// is the raw link text (gframe/cli_args.h's DEEP_LINK argument,
	// forwarded by the launcher). Parses it with gframe/deep_link.h,
	// refuses anything that does not parse or whose host is not already in
	// ServerLobby::serversVector (design/server-duelli.md §7: "un link non
	// puo' far entrare il client in un server sconosciuto" — the same rule
	// OnJoin() already applies to a Discord secret), and otherwise behaves
	// like JoinFromDiscord(): JOIN_GAME with gameid 0, the permit's
	// password, and the permit's name set as this client's own nickname
	// before connecting (StartClient()'s CTOS_PLAYER_INFO sends whatever
	// ebNickName holds).
	static void JoinFromDeepLink(const std::string& uri);
	// FASE 76b, design/server-duelli.md §13.6 punto 5: called every frame
	// from Game::MainLoop while mainGame->dInfo.isAwaitingReconnect is
	// true. Cheap when there is nothing to do (gframe/tournament_mode.h's
	// ReconnectAttemptDue is a plain comparison), same "every frame is
	// fine" discipline as RefreshOnlineGate(). Reuses StartClient() itself
	// — same JOIN_GAME (gameid 0) path JoinFromDeepLink() already drives,
	// with the same host/port/password/name it never discarded
	// (dInfo.secret, already set and untouched since the original join —
	// nothing here is written to disk, the process never stopped running).
	static void TournamentReconnectTick();
	// `tls_name` non-empty = encrypted connection to that server name (SNI and
	// certificate check); empty = plain TCP, exactly as before FASE 83.
	static bool StartClient(const epro::Address& ip, uint16_t port, uint32_t gameid = 0, bool create_game = true, const std::string& tls_name = std::string());
	static void ConnectTimeout(evutil_socket_t fd, short events, void* arg);
	static void StopClient(bool is_exiting = false);
	static void ClientRead(bufferevent* bev, void* ctx);
	static void ClientEvent(bufferevent *bev, short events, void *ctx);
	static void ClientThread();
	static void HandleSTOCPacketLanSync(std::vector<uint8_t>&& data);
	static void HandleSTOCPacketLanAsync(const std::vector<uint8_t>& data);
	static void ParserThread();
	static bool CheckReady();
	static std::pair<uint32_t, uint32_t> GetPlayersCount();
	static ReplayStream replay_stream;
	static Replay last_replay;
	static int ClientAnalyze(const uint8_t* msg, uint32_t len);
	static int ClientAnalyze(const CoreUtils::Packet& packet) {
		return ClientAnalyze(packet.data(), static_cast<uint32_t>(packet.buff_size()));
	}
	static int GetSpectatorsCount() {
		return watching;
	};
	static void SwapField();
	static bool IsConnected() {
		return !!connect_state;
	};
	static void SetResponseB(const void* respB, size_t len) {
		response_buf.resize(len);
		memcpy(response_buf.data(), respB, len);
	}
	template<typename T>
	static inline void SetResponse(const T& resp) {
		return SetResponseB(&resp, sizeof(T));
	}
	static inline void SetResponseI(int respI) {
		return SetResponse<int32_t>(respI);
	}
	static void SendResponse();
	// The only way bytes may reach client_bev: on a TLS connection the
	// filter is unlocked and the raw socket's lock is what keeps the writer
	// and the event loop from touching the TLS session at the same time.
	static void WriteToServer(const void* data, size_t len) {
		auto* lock_bev = client_tls_raw ? client_tls_raw : client_bev;
		bufferevent_lock(lock_bev);
		bufferevent_write(client_bev, data, len);
		bufferevent_unlock(lock_bev);
	}
	static void SendPacketToServer(uint8_t proto) {
		if(!client_bev)
			return;
		const auto res = [proto] {
			const uint16_t message_size = sizeof(proto);
			std::array<uint8_t, sizeof(message_size) + message_size> res;
			memcpy(res.data(), &message_size, sizeof(message_size));
			res[2] = proto;
			return res;
		}();
		WriteToServer(res.data(), res.size());
	}
	template<typename ST>
	static void SendPacketToServer(uint8_t proto, const ST& st) {
		if(!client_bev)
			return;
		const auto res = [proto, &st] {
			static constexpr uint16_t message_size = sizeof(proto) + sizeof(st);
			std::array<uint8_t, sizeof(message_size) + message_size> res;
			memcpy(res.data(), &message_size, sizeof(message_size));
			res[2] = proto;
			memcpy(res.data() + 3, &st, sizeof(st));
			return res;
		}();
		WriteToServer(res.data(), res.size());
	}
	static void SendBufferToServer(uint8_t proto, void* buffer, size_t len) {
		if(!client_bev)
			return;
		const auto res = [proto, buffer, len] {
			const uint16_t message_size = static_cast<uint16_t>(1 + len);
			std::vector<uint8_t> res;
			res.resize(sizeof(message_size) + message_size);
			memcpy(res.data(), &message_size, sizeof(message_size));
			res[2] = proto;
			memcpy(res.data() + 3, buffer, len);
			return res;
		}();
		WriteToServer(res.data(), res.size());
	}

	static void ReplayPrompt(bool need_header = false);

protected:
	static bool is_refreshing;
	static int match_kill;
	static event* resp_event;
public:
	static std::vector<epro::Host> hosts;
	static void BeginRefreshHost();
	static int RefreshThread(event_base* broadev);
	static void BroadcastReply(evutil_socket_t fd, short events, void* arg);
};

}

#endif //DUELCLIENT_H
