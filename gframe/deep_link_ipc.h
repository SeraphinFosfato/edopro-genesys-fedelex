#ifndef DEEP_LINK_IPC_H
#define DEEP_LINK_IPC_H

#include <cstdint>
#include <functional>
#include <string>

// FASE 88 B (owner-approved): a fedelex:// link clicked while the simulator is
// already open is handed to that simulator instead of opening a second one.
//   - The running simulator listens on 127.0.0.1:kPort (Listener below).
//   - The launcher (launcher/main.cpp), which is what the OS starts for a
//     link, calls SendToRunningClient() first; if a simulator answers "OK" it
//     exits, otherwise it carries on as before.
// The wire format is ONE line: the URI, then '\n' (or EOF). Nothing else is
// accepted: no commands, no other schemes. Not a security boundary against a
// local attacker (they can already run the client), it only has to refuse
// whatever else may poke a localhost port (a web page, a scanner).
namespace ygo::deep_link_ipc {

// Fixed, unlikely to clash (IANA dynamic range, not a known service).
constexpr uint16_t kPort = 47873;
constexpr size_t kMaxLength = 2048;

// True if `message` (without its terminator) is acceptable: starts with
// "fedelex://" (case-insensitive), is at most kMaxLength bytes, and has no
// control characters.
bool IsAcceptableMessage(const std::string& message);

// Launcher side. True only if a simulator accepted the link and answered OK.
bool SendToRunningClient(const std::string& uri);

// Simulator side. The callback runs on the listener thread: it must only queue.
class Listener {
public:
	using Callback = std::function<void(std::string)>;
	Listener() = default;
	~Listener();
	Listener(const Listener&) = delete;
	Listener& operator=(const Listener&) = delete;
	// False when the port is busy / cannot bind: no listener, old behaviour.
	bool Start(Callback callback);
private:
	struct Impl;
	Impl* impl{ nullptr };
};

}

#endif //DEEP_LINK_IPC_H
