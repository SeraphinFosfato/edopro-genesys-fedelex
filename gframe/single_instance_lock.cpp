#include "single_instance_lock.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ygo {

#if defined(_WIN32)
SingleInstanceLock::SingleInstanceLock(const std::string& lock_path) {
	// Same technique as ClientUpdater::FileLock (gframe/client_updater.cpp):
	// CREATE_ALWAYS with the default (exclusive) sharing mode. No
	// FILE_SHARE_* flags means the OS itself refuses a second CreateFile()
	// on the same path from ANY process while this handle is open — no
	// named mutex, no handle-inheritance bookkeeping needed across a
	// CreateProcessW() the way a mutex would require, because the probing
	// side here (launcher/main.cpp) never spawns a child while holding
	// this handle open; it checks, then closes, then decides.
	std::wstring wpath(lock_path.begin(), lock_path.end());
	HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ,
						   0, nullptr, CREATE_ALWAYS,
						   FILE_ATTRIBUTE_HIDDEN, nullptr);
	if(h == INVALID_HANDLE_VALUE) {
		acquired = false;
		return;
	}
	handle = h;
	acquired = true;
}

SingleInstanceLock::~SingleInstanceLock() {
	if(handle)
		CloseHandle(static_cast<HANDLE>(handle));
}
#else
SingleInstanceLock::SingleInstanceLock(const std::string& lock_path) {
	// O_CLOEXEC deliberately: this process never execs into anything that
	// should inherit the lock (the simulator holds its own instance for
	// its own lifetime; the launcher's probe instance is short-lived and
	// destroyed well before it would ever spawn/exec a child).
	fd = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
	if(fd < 0) {
		acquired = false;
		return;
	}
	if(flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		fd = -1;
		acquired = false;
		return;
	}
	acquired = true;
}

SingleInstanceLock::~SingleInstanceLock() {
	if(fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
	// Deliberately NOT ygo::Utils::FileDelete(lock_path) here, unlike
	// ClientUpdater::FileLock's destructor: a short-lived PROBE instance
	// (launcher/main.cpp) destructs constantly, and unlinking the path out
	// from under a simulator that is still holding its OWN flock on the
	// now-detached inode would make the next probe's open(O_CREAT) create a
	// brand new file — whose fresh flock always succeeds, silently
	// reporting "nothing running" while a simulator is very much alive.
	// Leaving an empty lock file on disk is harmless; deleting it is not.
}
#endif

}
