#ifndef SERVERLOBBY_H
#define SERVERLOBBY_H

#include "address.h"
#include "config.h"
#include "network.h"
#include <atomic>

namespace ygo {
struct ServerInfo {
	enum Protocol : uint8_t {
		HTTP,
		HTTPS,
	} protocol{ HTTP };
	mutable bool resolved{ false };
	std::wstring name;
	std::string address;
	std::string roomaddress;
	uint16_t duelport;
	uint16_t roomlistport;
	// FASE 83: `"tls": true` in configs.json. The game connection to this
	// server is encrypted and its identity is its NAME (see server_tls.h);
	// it has no room list, so roomaddress/roomlistport are left empty.
	bool tls{ false };
	mutable epro::Host resolved_address;
	const epro::Host& Resolved() const;
	epro::stringview GetProtocolString() const {
		return GetProtocolString(protocol);
	}
	static epro::stringview GetProtocolString(Protocol protocol) {
		switch(protocol) {
		case HTTP:
			return "http";
		case HTTPS:
			return "https";
		default:
			unreachable();
		}
	}
	static Protocol GetProtocol(epro::stringview protocol) {
		if(protocol == "https")
			return HTTPS;
		return HTTP;
	}
};
struct RoomInfo {
	std::wstring name;
	uint32_t id;
	bool started;
	bool locked;
	std::vector<std::wstring> players;
	std::wstring description;
	HostInfo info;
};
class ServerLobby {
public:
	static std::vector<RoomInfo> roomsVector;
	static std::vector<ServerInfo> serversVector;
	// Servers WITHOUT `tls`, by resolved address (a TLS server never
	// matches here: for it an address proves nothing, see FindTlsServer).
	static bool IsKnownHost(epro::Host host);
	// FASE 83: the TLS server a link naming `name`:`port` points at, or null.
	// Compares the NAME (not what it resolves to): the certificate verified
	// against that name is what makes the connection trustworthy.
	static const ServerInfo* FindTlsServer(const std::string& name, uint16_t port);
	static void RefreshRooms();
	static bool HasRefreshedRooms();
	static void GetRoomsThread();
	static void FillOnlineRooms();
	static void JoinServer(bool host);
	static std::atomic_bool is_refreshing;
	static std::atomic_bool has_refreshed;
};
//extern ServerLobby serverLobby;
}
#endif //SERVERLOBBY_H
