// FASE 88 B: gframe/deep_link_ipc. Validation is pure; the round trip uses the
// real loopback port (kPort), so it is skipped (not failed) if the port is busy.
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include "deep_link_ipc.h"

int RunDeepLinkIpcTests();

namespace {
int failures = 0;
void check(bool ok, const char* what) {
	if(!ok) {
		++failures;
		std::printf("  FAIL  %s\n", what);
	}
}
}

int RunDeepLinkIpcTests() {
	using namespace ygo::deep_link_ipc;
	check(IsAcceptableMessage("fedelex://tavolo?host=a&port=1&pass=x&nome=y"), "tavolo link accepted");
	check(IsAcceptableMessage("FEDELEX://lista?torneo=ABC"), "scheme is case-insensitive");
	check(!IsAcceptableMessage(""), "empty rejected");
	check(!IsAcceptableMessage("fedelex://"), "bare scheme rejected");
	check(!IsAcceptableMessage("http://x"), "other scheme rejected");
	check(!IsAcceptableMessage("POST / HTTP/1.1"), "http request rejected");
	check(!IsAcceptableMessage("fedelex://lista?a=b\nrm"), "embedded newline rejected");
	check(!IsAcceptableMessage(std::string("fedelex://") + std::string(kMaxLength, 'a')), "oversized rejected");
	check(IsAcceptableMessage(std::string("fedelex://") + std::string(kMaxLength - 10, 'a')), "max size accepted");
	check(!SendToRunningClient("http://x"), "sender refuses non-fedelex");

	std::string got;
	std::atomic<bool> have{ false };
	{
		Listener l;
		if(!l.Start([&](std::string m) { got = std::move(m); have = true; })) {
			std::printf("deep_link_ipc_tests: port busy, round trip skipped\n");
		} else {
			check(SendToRunningClient("fedelex://lista?torneo=XYZ"), "round trip answered OK");
			check(have && got == "fedelex://lista?torneo=XYZ", "listener received the link");
		}
	}
	check(!SendToRunningClient("fedelex://lista?torneo=XYZ"), "no listener -> sender reports failure");
	std::printf("deep_link_ipc_tests: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
