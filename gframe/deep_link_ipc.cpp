#include "deep_link_ipc.h"
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static constexpr sock_t kBadSock = INVALID_SOCKET;
static void CloseSock(sock_t s) { closesocket(s); }
static void EnsureSockets() {
	static const bool once = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
	(void)once;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static constexpr sock_t kBadSock = -1;
static void CloseSock(sock_t s) { close(s); }
static void EnsureSockets() {}
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace ygo::deep_link_ipc {

namespace {
constexpr int kIoTimeoutMs = 2000;

sockaddr_in LoopbackAddr() {
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(kPort);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return a;
}

// Waits until `s` is readable; false on timeout/error.
bool WaitReadable(sock_t s, int ms) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(s, &set);
	timeval tv{ ms / 1000, (ms % 1000) * 1000 };
	return select(static_cast<int>(s) + 1, &set, nullptr, nullptr, &tv) > 0;
}

// Reads until '\n' / EOF / kMaxLength+1 bytes / overall deadline. False = reject.
bool ReadLine(sock_t s, std::string& out) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
	char buf[256];
	while(out.size() <= kMaxLength) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
		if(left <= 0 || !WaitReadable(s, static_cast<int>(left)))
			return false;
		const auto n = recv(s, buf, sizeof(buf), 0);
		if(n < 0)
			return false;
		if(n == 0)
			return true; // EOF terminates the message
		for(int i = 0; i < n; i++) {
			if(buf[i] == '\n')
				return true;
			out.push_back(buf[i]);
		}
	}
	return false;
}
}

bool IsAcceptableMessage(const std::string& message) {
	static constexpr char prefix[] = "fedelex://";
	constexpr size_t plen = sizeof(prefix) - 1;
	if(message.size() <= plen || message.size() > kMaxLength)
		return false;
	for(size_t i = 0; i < plen; i++)
		if(std::tolower(static_cast<unsigned char>(message[i])) != prefix[i])
			return false;
	for(const char c : message)
		if(static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
			return false;
	return true;
}

bool SendToRunningClient(const std::string& uri) {
	if(!IsAcceptableMessage(uri))
		return false;
	EnsureSockets();
	const sock_t s = socket(AF_INET, SOCK_STREAM, 0);
	if(s == kBadSock)
		return false;
	bool ok = false;
	const auto addr = LoopbackAddr();
	// Nothing listening on loopback = immediate refusal, so a blocking connect is fine.
	if(connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
		const std::string line = uri + "\n";
		if(send(s, line.data(), static_cast<int>(line.size()), MSG_NOSIGNAL) == static_cast<int>(line.size())) {
			std::string reply;
			ok = ReadLine(s, reply) && reply == "OK";
		}
	}
	CloseSock(s);
	return ok;
}

struct Listener::Impl {
	sock_t sock{ kBadSock };
	std::atomic<bool> stop{ false };
	std::thread thread;
	Callback callback;

	void Serve(sock_t c) {
		std::string line;
		if(ReadLine(c, line) && IsAcceptableMessage(line)) {
			callback(std::move(line));
			send(c, "OK\n", 3, MSG_NOSIGNAL);
		}
	}
	void Run() {
		while(!stop) {
			if(!WaitReadable(sock, 250))
				continue;
			sockaddr_in peer{};
			socklen_t len = sizeof(peer);
			const sock_t c = accept(sock, reinterpret_cast<sockaddr*>(&peer), &len);
			if(c == kBadSock)
				continue;
			// Bound to loopback already; checked again so it stays true if that changes.
			if(peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK))
				Serve(c);
			CloseSock(c);
		}
	}
};

bool Listener::Start(Callback callback) {
	if(impl)
		return true;
	EnsureSockets();
	const sock_t s = socket(AF_INET, SOCK_STREAM, 0);
	if(s == kBadSock)
		return false;
#ifdef _WIN32
	// Exclusive: on Windows SO_REUSEADDR would let a second process share the port.
	const BOOL excl = TRUE;
	setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof(excl));
#else
	const int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
	const auto addr = LoopbackAddr();
	if(bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
		CloseSock(s);
		return false;
	}
	impl = new Impl;
	impl->sock = s;
	impl->callback = std::move(callback);
	impl->thread = std::thread([i = impl] { i->Run(); });
	return true;
}

Listener::~Listener() {
	if(!impl)
		return;
	impl->stop = true;
	impl->thread.join();
	CloseSock(impl->sock);
	delete impl;
}

}
