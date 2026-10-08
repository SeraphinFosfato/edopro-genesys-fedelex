#ifndef SINGLE_INSTANCE_LOCK_H
#define SINGLE_INSTANCE_LOCK_H

#include <string>

// FASE 75, design/launcher.md, PHASES.md "il link del tavolo", cancello 3:
// "se il simulatore e' gia' aperto non se ne apre un secondo". The brief
// left the exact mechanism to whoever implements ("forma a scelta, locale
// alla macchina"), flagging only ONE thing as a real point of risalita: a
// NEW long-lived network listener that the launcher talks to in order to
// hand an already-running simulator the deep link directly. This class is
// deliberately NOT that — it answers a narrower, already-settled question
// ("is any fedelex simulator alive for this data directory right now?"),
// the same way gframe/client_updater.cpp's ClientUpdater::FileLock already
// answers "is another update in progress?" for a different lock file:
// flock() on POSIX, an exclusive CreateFile() handle on Windows, held for
// as long as the owning process lives and released automatically by the
// OS on ANY exit path (clean, crashed, killed) — never a message exchanged
// with another process, never a thing to keep listening.
//
// Used in two places:
//   - gframe/gframe.cpp acquires one for the whole lifetime of the
//     simulator process (every launch, deep-linked or not — the lock
//     itself gates nothing there, it only exists to be OBSERVED).
//   - launcher/main.cpp, only when it is about to act on a deep link,
//     tries to acquire a SEPARATE, short-lived instance of this same class
//     on the identical path: success means no simulator is holding it (ok
//     to proceed), failure means one already is (NotifyUser() + do not
//     spawn a second one, per the brief's own fallback option). The probe
//     instance is destroyed again immediately either way — it never keeps
//     the lock, it only tests it.
//
// Not a security boundary (design/launcher.md's "onesta sui deterrenti"
// applies here too, even though this file lives in gframe/, not
// launcher/): deleting the lock file by hand, or simply never linking
// against this class, defeats it completely. It exists only to avoid
// accidentally opening a second simulator window from one tournament
// invite link clicked twice, not to defend against anything adversarial.
namespace ygo {

// Shared by gframe/gframe.cpp (the simulator, which holds this for its
// whole lifetime) and launcher/main.cpp (which only ever probes it, see
// this header's own comment above) — one name, defined once, so the two
// sides can never drift onto two different lock files relative to the same
// data directory.
constexpr const char* kTournamentLockFileName = "fedelex-tournament.lock";

class SingleInstanceLock {
public:
	explicit SingleInstanceLock(const std::string& lock_path);
	~SingleInstanceLock();
	SingleInstanceLock(const SingleInstanceLock&) = delete;
	SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

	bool Acquired() const { return acquired; }

private:
	bool acquired{ false };
#if defined(_WIN32)
	void* handle{ nullptr }; // HANDLE — kept as void* so this header never needs <windows.h>
#else
	int fd{ -1 };
#endif
};

}

#endif //SINGLE_INSTANCE_LOCK_H
