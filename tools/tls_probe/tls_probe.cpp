// FASE 83 cancello 3: exercises gframe/tls_client.cpp (the very function
// DuelClient::StartClient calls for a server that declares "tls") against a
// TLS server, without irrlicht, a display or any game data. Prints what the
// connection did and exits 0 only when the handshake passed AND a reply came
// back over the encrypted channel.
//
//   g++ -std=c++17 -Igframe tools/tls_probe/tls_probe.cpp gframe/tls_client.cpp gframe/tls_roots.cpp -levent -levent_pthreads -lssl -lcrypto -o tls_probe
//   tls_probe <connect-to-address> <port> [--name <hostname>] [--ca <file>] [--edopro]
//
// <connect-to-address> is where TCP goes; --name is the hostname the
// certificate must be valid for (default: the same string). --ca replaces
// the trust store with that one file; without it the usual store applies,
// including SSL_CERT_FILE / SSL_CERT_DIR, so a test CA can be offered to this
// process alone without touching any system certificate store.
//
// --edopro: after the handshake, instead of "ping" send the two opening game
// packets a client sends (CTOS_PLAYER_INFO, then CTOS_JOIN_GAME with no
// password, game id 0) and print the first thing the server answers. Needs
// -Iocgcore for network.h. A server without a permit for that name refuses
// the join; the refusal itself is the proof that game packets travel.
//
// Not part of any build or CI job: a manual instrument, like the other
// small tools under tools/.

#include <cstdio>
#include <cstring>
#include <string>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/thread.h>
#include <event2/util.h>
#include "tls_client.h"
#include "network.h"

namespace {
event_base* base = nullptr;
bufferevent* bev = nullptr;
bufferevent* raw = nullptr;
bool connected = false;
bool replied = false;
bool edopro = false;
int exit_code = 3; // timeout until something says otherwise

template<typename T>
void Send(uint8_t proto, const T& payload) {
	std::string frame(2 + 1 + sizeof(T), '\0');
	const uint16_t size = static_cast<uint16_t>(1 + sizeof(T));
	std::memcpy(&frame[0], &size, 2);
	frame[2] = static_cast<char>(proto);
	std::memcpy(&frame[3], &payload, sizeof(T));
	bufferevent_write(bev, frame.data(), frame.size());
}

void OnRead(bufferevent* b, void*) {
	auto* in = bufferevent_get_input(b);
	std::string data(evbuffer_get_length(in), '\0');
	evbuffer_remove(in, data.data(), data.size());
	if(edopro) {
		// frame = u16 length, u8 proto, payload
		std::printf("RECV over TLS: %zu bytes, first server packet proto=0x%02x, hex:", data.size(), data.size() > 2 ? static_cast<unsigned char>(data[2]) : 0);
		for(size_t i = 0; i < data.size() && i < 48; ++i)
			std::printf(" %02x", static_cast<unsigned char>(data[i]));
		std::printf("\n");
	} else {
		std::printf("RECV over TLS: %zu bytes: %s", data.size(), data.c_str());
	}
	replied = true;
	exit_code = 0;
	event_base_loopexit(base, nullptr);
}

void OnEvent(bufferevent*, short what, void*) {
	if(what & BEV_EVENT_CONNECTED) {
		connected = true;
		std::printf("CONNECTED: handshake done, certificate verified\n");
		bufferevent_lock(raw); // the lock everything on this connection is serialised on
		if(edopro) {
			Send(0x10, ygo::CTOS_PlayerInfo{ { 't', 'l', 's', 'p', 'r', 'o', 'b', 'e' } }); // CTOS_PLAYER_INFO
			ygo::CTOS_JoinGame join{};
			join.version = 0x1362;
			Send(0x12, join); // CTOS_JOIN_GAME
		} else {
			static const char msg[] = "ping\n";
			bufferevent_write(bev, msg, sizeof(msg) - 1);
		}
		bufferevent_unlock(raw);
		return;
	}
	std::printf("%s (what=0x%x, connected=%d, replied=%d)\n", (what & BEV_EVENT_ERROR) ? "ERROR" : "CLOSED", what, connected, replied);
	if(!replied)
		exit_code = 2;
	event_base_loopexit(base, nullptr);
}

void Log(const std::string& line) {
	std::printf("LOG: %s\n", line.c_str());
}
}

int main(int argc, char** argv) {
	if(argc < 3) {
		std::fprintf(stderr, "uso: %s <indirizzo> <porta> [--name <host>] [--ca <file>]\n", argv[0]);
		return 64;
	}
	ygo::tls::Options options;
	options.hostname = argv[1];
	for(int i = 3; i + 1 < argc; ++i) {
		if(!std::strcmp(argv[i], "--name"))
			options.hostname = argv[i + 1];
		else if(!std::strcmp(argv[i], "--ca"))
			options.ca_file = argv[i + 1];
	}
	for(int i = 3; i < argc; ++i)
		edopro = edopro || !std::strcmp(argv[i], "--edopro");
	options.log = Log;
	evthread_use_pthreads();
	base = event_base_new();
	sockaddr_storage ss{};
	int len = sizeof(ss);
	if(evutil_parse_sockaddr_port((std::string(argv[1]) + ":" + argv[2]).c_str(), reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
		std::fprintf(stderr, "indirizzo non valido (serve un IP): %s\n", argv[1]);
		return 64;
	}
	bev = ygo::tls::NewClient(base, reinterpret_cast<sockaddr*>(&ss), len, options, OnRead, OnEvent, nullptr, &raw);
	if(!bev) {
		std::printf("NewClient failed\n");
		return 2;
	}
	timeval timeout{ 10, 0 };
	event_base_loopexit(base, &timeout);
	event_base_dispatch(base);
	bufferevent_free(bev);
	event_base_free(base);
	std::printf("RESULT: %s (exit %d)\n", exit_code == 0 ? "ACCEPTED" : (exit_code == 2 ? "REJECTED/FAILED" : "TIMEOUT"), exit_code);
	return exit_code;
}
