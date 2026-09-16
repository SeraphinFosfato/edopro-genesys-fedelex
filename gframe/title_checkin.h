#ifndef TITLE_CHECKIN_H
#define TITLE_CHECKIN_H

// Background check-in loop against the title API (D78-D80, D102). Talks to
// TitleStore (title_store.h) with whatever it gets back; the network and
// scheduling live here, the verification and the access decision do not.
//
// Cadence (design/fork-edopro/access-control.md §2): once immediately at
// startup, then every 15 minutes at rest or every 5 minutes while
// dInfo.isInDuel — this class does not know what a duel is (no game.h, no
// irrlicht dependency), so the main thread tells it via SetInDuel() at
// whatever call sites already handle entering/leaving a duel.
//
// D102 is enforced right here, at the network boundary: ONLY a completed
// HTTP round-trip whose status is 200 or 403 ever reaches
// TitleStore::ApplyResponse(). A transport failure (unreachable, timeout,
// TLS error) or any other status (401/404/429/5xx) is a plain no-op — never
// even handed to the store, so there is no code path in this class that
// could accidentally let an unsigned response degrade the player's access.

#include <atomic>
#include <string>
#include "epro_condition_variable.h"
#include "epro_mutex.h"
#include "epro_thread.h"

#ifndef TITLE_API_URL
#define TITLE_API_URL "https://cuoremeccanico.quoll-ruffe.ts.net/v1/title"
#endif

namespace ygo {

class TitleCheckin {
public:
	explicit TitleCheckin(std::string override_url = {});
	// A detached-in-spirit worker still POSTing while the process tears
	// down is how a half-applied response outlives the run (same reasoning
	// as BanlistUpdater's destructor) — join it here instead.
	~TitleCheckin();

	// Spawns the worker: an immediate check-in, then the periodic loop.
	// Non-blocking, same shape as BanlistUpdater::StartCheck.
	void Start();
	// Signals the loop to stop and joins it. Safe to call even if Start()
	// was never called (worker is simply not joinable yet).
	void Join();

	// Called from wherever the main thread already knows a duel just
	// started or ended. Wakes the loop immediately so a transition INTO a
	// duel starts using the 5-minute cadence right away, instead of
	// finishing out whatever was left of a 15-minute rest wait first.
	void SetInDuel(bool in_duel);

private:
	void Loop();
	// One check-in attempt: POSTs the stored credential, and — only for a
	// completed exchange whose status is 200 or 403 — hands the raw body to
	// gTitleStore->ApplyResponse(). Every other outcome is a silent no-op
	// (D102); see the class comment above.
	void CheckOnce();

	struct PostResult {
		bool transport_ok = false;
		long status_code = 0;
		std::string body;
	};
	// POSTs {"credential": credential} as application/json to `url`.
	// transport_ok is false only for a transport-level failure (DNS,
	// connect, TLS, timeout) — a completed exchange that came back 4xx/5xx
	// still sets transport_ok true, with that status_code and whatever body
	// the server sent; CheckOnce is what decides D102's status allowlist,
	// not this function.
	static bool Post(const std::string& url, const std::string& credential, PostResult& out);

	std::string base_url{ TITLE_API_URL };
	epro::thread worker_;
	epro::mutex wake_mutex_;
	epro::condition_variable wake_cv_;
	std::atomic<bool> in_duel_{ false };
	bool stop_ = false; // guarded by wake_mutex_
};

extern TitleCheckin* gTitleCheckin;

}

#endif //TITLE_CHECKIN_H
