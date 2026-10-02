#ifndef LOCAL_CONNECTION_H
#define LOCAL_CONNECTION_H

#include <cstdint>
#include <cstring>

namespace ygo {

// FASE 59 — design/blocco-online.md §5, "Host locale solo per se'". With the
// online gate closed, hosting in LAN must keep working for the AI (which
// connects through the same local host, NetServer::StartServer -> GenericDuel)
// and must refuse everyone else, WITHOUT shutting down the listening socket
// itself (that would also refuse the client's own loopback connection and the
// AI with it — design/blocco-online.md's own stated trap: "ascoltare solo su
// ::1 rifiuterebbe anche il client e l'IA, che si collegano a 127.0.0.1").
//
// The property to check is WHO is connecting, never WHERE the socket
// listens — so this takes the raw bytes of the PEER address NetServer
// already has for an incoming connection, not a socket or an epro::Address:
// epro::Address (address.h) needs libevent (evutil_inet_pton) just to
// construct one from a string, which has no place in the standalone test
// binary (tests/premake5.lua) — same boundary as everywhere else in this
// suite. The real call site converts epro::Address's own internal buffer
// (which is already exactly these bytes — see Address::toInAddr/toIn6Addr
// in address.cpp, both plain memcpy) into the calls below; this header
// never needs to know how an Address is built.
//
// `bytes` for IPv4 is 4 bytes in network byte order (the same layout
// Address::setIP4/toInAddr already copy in and out via in_addr::s_addr).
// `bytes` for IPv6 is 16 bytes in network byte order (in6_addr::s6_addr).
enum class AddressFamily {
	IPv4,
	IPv6,
};

inline bool IsLocalCallerAddress(const uint8_t* bytes, AddressFamily family) {
	static constexpr uint8_t v4_loopback[4] = { 127, 0, 0, 1 };
	if(family == AddressFamily::IPv4)
		return std::memcmp(bytes, v4_loopback, sizeof(v4_loopback)) == 0;
	// ::1 — the IPv6 loopback a client binding dual-stack or IPv6-only may
	// present itself as.
	static constexpr uint8_t v6_loopback[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
	if(std::memcmp(bytes, v6_loopback, sizeof(v6_loopback)) == 0)
		return true;
	// ::ffff:127.0.0.1 — the IPv4-mapped IPv6 form a dual-stack socket can
	// hand back for what is, on the wire, still a plain IPv4 loopback
	// connection. Without this case a dual-stack listener would reject its
	// own client/AI depending on which address family the OS happened to
	// pick for the loopback connection that session.
	static constexpr uint8_t v4_mapped_prefix[12] = { 0,0,0,0, 0,0,0,0, 0,0, 0xff,0xff };
	return std::memcmp(bytes, v4_mapped_prefix, sizeof(v4_mapped_prefix)) == 0
		&& std::memcmp(bytes + 12, v4_loopback, sizeof(v4_loopback)) == 0;
}

}

#endif //LOCAL_CONNECTION_H
