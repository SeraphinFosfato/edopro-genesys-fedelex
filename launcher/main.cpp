// fedelex-launcher — the actual executable (FASE 64 cancelli 3-4-5,
// design/launcher.md, design/decisioni.md D244).
//
// This file is deliberately the ONLY place that does real I/O for the
// launcher: everything it decides comes from ygo::update (manifest parsing
// + signature verification, gframe/update_verify.*) and ygo::launcher
// (pure decisions, gframe/launcher_logic.*), both already covered by
// tests/launcher_logic_tests.cpp and tests/update_tests.cpp without needing
// any of what lives here. Nothing here is unit-tested for the same reason
// client_updater.cpp isn't: it is a thin orchestration of network+filesystem
// calls around functions that already are.
//
// What this program does, in order (design/launcher.md §3):
//   1. Find PROGRAM_DIR (own directory) and the simulator at
//      PROGRAM_DIR/bin/<simulator-name>. Missing or not executable => fatal.
//   2. Find DATA_DIR (PROGRAM_DIR/.data-dir if present, else PROGRAM_DIR
//      itself — see the comment on ResolveDataDir()).
//   3. Fetch + verify the signed manifest (UPDATE_URL/.sig). Unreachable or
//      invalid => skip straight to step 5, this is never fatal
//      (design/launcher.md §7, "non bloccare mai").
//   4. For each launcher_files[] entry selected for this platform, replace
//      the installed file if its hash differs, verifying every step
//      (design/decisioni.md D244 point 5) and never leaving PROGRAM_DIR
//      without a working simulator. If, and only if, at least one entry
//      actually needs replacing, this step is accompanied by an update
//      window (design/decisioni.md D247, FASE 66): "Aggiornamento in
//      corso", a running MB counter, then "Avvio..." — see progress.h.
//      Nothing here opens a window when there is nothing to download.
//   5. Run the simulator with the provenance mark.
//
// Linux only has been built and run end to end (cancelli 3-5 of FASE 64
// were tested on this platform; FASE 66's progress window likewise). The
// Windows branch below (FASE 67, design/decisioni.md D249) now implements
// the same step 4 (launcher_files install loop, anti-rollback, the update
// window) with one deliberate difference from Linux: the order of
// swap-vs-probe is reversed (§D249 point 2 — Windows swaps final into place
// BEFORE probing it, Linux probes the downloaded file in place before ever
// touching `final`, see InstallSimulatorFile's two bodies below for why).
// It follows the same contract everywhere else (MessageBoxW instead of a
// desktop notifier, .exe suffix, no chmod) but has NEVER been compiled —
// there is no Windows toolchain in this environment. Flagged honestly in
// design/launcher.md's stato dell'implementazione; do not read "the code
// compiles" as "it works on Windows" (§1.9 of the vault CLAUDE.md) — the
// first real proof is the CI's build-windows job after this is pushed, the
// first real proof of BEHAVIOUR is a Windows tester's launcher.log.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "update_verify.h"
#include "launcher_logic.h"
#include "progress.h"
#include "sha256.h"

#include <curl/curl.h>

#if defined(_WIN32)
#define FEDELEX_WINDOWS 1
#include <windows.h>
#else
#define FEDELEX_WINDOWS 0
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#endif

namespace {

#if FEDELEX_WINDOWS
constexpr const char* kSimulatorName = "ygoprodll.exe";
constexpr const char* kPlatform = ygo::update::PlatformWindows;
#else
constexpr const char* kSimulatorName = "ygoprodll";
constexpr const char* kPlatform = ygo::update::PlatformLinux;
#endif

// D244 point 9: "attesa del manifesto 5 secondi, poi si avvia il
// simulatore com'e' (offline non blocca mai)". Chosen by whoever wrote
// D244, not reopened here.
constexpr long kManifestTimeoutSeconds = 5;

// Not specified anywhere: how long the launcher waits, after exec'ing a
// freshly-downloaded simulator binary, before deciding it "launched"
// (design/launcher.md cancello 3/4, DecideSwap's new_binary_launches).
// There is no cheap self-check flag in the simulator (it would need one
// added to gframe to do this properly — out of scope for this brief, see
// the status note). This is a real, unfaked measurement of one specific
// failure mode: a binary that is corrupt, for the wrong architecture, or
// missing a shared library exits near-instantly with a loader error
// (the exact 2026-09-30 incident design/launcher.md opens with). A binary
// that is fine but just slow to open its window is never mistaken for a
// failure as long as this number stays well under "slow splash screen"
// territory. 1500ms is a judgment call within that criterion (§6.6:
// "valori che si decidono guardando il risultato" — implementer's call),
// not a number someone else should have picked.
constexpr int kLaunchProbeMillis = 1500;

void Log(const std::string& data_dir, const std::string& line) {
	std::fprintf(stderr, "[fedelex-launcher] %s\n", line.c_str());
	if(data_dir.empty())
		return;
	std::ofstream log(data_dir + "/launcher.log", std::ios::app);
	if(log)
		log << line << '\n';
}

#if FEDELEX_WINDOWS
void NotifyUser(const std::string& title, const std::string& message) {
	MessageBoxW(nullptr,
		std::wstring(message.begin(), message.end()).c_str(),
		std::wstring(title.begin(), title.end()).c_str(),
		MB_OK | MB_ICONINFORMATION);
}
#else
// Forward declaration: defined further down (next to the rest of the
// process-handling code), reused here so this function and the simulator
// probe share one fork()+execvp() implementation instead of two.
pid_t SpawnDetached(const std::string& path, const std::vector<std::string>& args);

// D244 point 1: zenity/kdialog if present, else notify-send, always also
// the log file (handled by the caller via Log()). A missing notifier is
// not an error — the log line is the ground truth either way.
//
// design/decisioni.md D247 point 5 (FASE 66): this used to run the
// notifier through `system("timeout 5 " + ...)`, which (a) made THIS
// process wait for the dialog to close and (b) killed the dialog after 5
// seconds if the user had not closed it yet — found on a real machine:
// the message looked like it closed itself, and a user who stepped away
// for a few seconds never saw it. Both of those were the SAME bug (the
// 5-second timeout was there only because the old code waited): spawning
// detached via SpawnDetached() below removes the need for either. The
// `command -v` probe keeps its own short timeout (it is not the dialog,
// just a presence check) for the same screen-less-session reason the
// original comment gave.
//
// `timeout 2 sh -c 'command -v NAME'`, not `timeout 2 command -v NAME`
// (found while testing this phase's fake-zenity harness, FASE 66): `timeout`
// execs its argument directly, it does not run it through a shell — and
// `command` is a shell BUILTIN, not a file anywhere on PATH
// (`type command` says so). `timeout 2 command -v zenity` therefore always
// failed with "impossibile eseguire il comando «command»" (exit 127),
// REGARDLESS of whether zenity/kdialog/notify-send were installed: this
// probe had returned "not found" unconditionally since D244/FASE 64, so
// NotifyUser() has never actually shown a notification in the field. Wrapping
// `command -v NAME` inside `sh -c '...'` makes `timeout` exec a real program
// (sh) that then evaluates the builtin itself.
void NotifyUser(const std::string& title, const std::string& message) {
	if(std::system("timeout 2 sh -c 'command -v zenity' >/dev/null 2>&1") == 0) {
		SpawnDetached("zenity", {"zenity", "--info", "--title=" + title, "--text=" + message});
		return;
	}
	if(std::system("timeout 2 sh -c 'command -v kdialog' >/dev/null 2>&1") == 0) {
		SpawnDetached("kdialog", {"kdialog", "--title=" + title, "--msgbox=" + message});
		return;
	}
	if(std::system("timeout 2 sh -c 'command -v notify-send' >/dev/null 2>&1") == 0) {
		SpawnDetached("notify-send", {"notify-send", title, message});
	}
	// None present: the log file (always written by the caller) is the
	// only record. Never fatal — design/launcher.md §7, "uscire in silenzio" is
	// the one thing to avoid, and the log already covers that.
}
#endif

std::string DirName(const std::string& path) {
	auto pos = path.find_last_of("/\\");
	if(pos == std::string::npos)
		return ".";
	if(pos == 0)
		return "/";
	return path.substr(0, pos);
}

bool FileExists(const std::string& path) {
#if FEDELEX_WINDOWS
	DWORD attrs = GetFileAttributesW(std::wstring(path.begin(), path.end()).c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st{};
	return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool DirExists(const std::string& path) {
#if FEDELEX_WINDOWS
	DWORD attrs = GetFileAttributesW(std::wstring(path.begin(), path.end()).c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st{};
	return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool MakeDir(const std::string& path) {
	if(DirExists(path))
		return true;
#if FEDELEX_WINDOWS
	return CreateDirectoryW(std::wstring(path.begin(), path.end()).c_str(), nullptr) != 0;
#else
	return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool IsExecutable(const std::string& path) {
#if FEDELEX_WINDOWS
	return FileExists(path); // Windows has no POSIX exec bit; presence is the whole check.
#else
	struct stat st{};
	if(::stat(path.c_str(), &st) != 0)
		return false;
	return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
#endif
}

bool SetExecutable(const std::string& path) {
#if FEDELEX_WINDOWS
	return FileExists(path);
#else
	struct stat st{};
	if(::stat(path.c_str(), &st) != 0)
		return false;
	return ::chmod(path.c_str(), st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
#endif
}

bool RemoveFile(const std::string& path) {
	return std::remove(path.c_str()) == 0 || !FileExists(path);
}

// Named RenameFile, not MoveFile: <windows.h> #defines MoveFile to
// MoveFileW (UNICODE build) — a textual macro, so a helper of ours called
// MoveFile is really an overload of the WinAPI MoveFileW. It compiled
// (v0.2.5/v0.2.6 shipped it) only because the parameter types differ; a
// name that the preprocessor does not touch is one trap fewer.
bool RenameFile(const std::string& from, const std::string& to) {
	RemoveFile(to); // D244.5: "una copia sola" — the previous .old, if any, is dropped first.
	return std::rename(from.c_str(), to.c_str()) == 0;
}

std::string ReadFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if(!f)
		return {};
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

bool WriteFile(const std::string& path, const std::string& bytes) {
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if(!f)
		return false;
	f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	return f.good();
}

std::string HashFile(const std::string& path) {
	if(!FileExists(path))
		return {};
	return ygo::Sha256Hex(ReadFile(path));
}

// --- networking -------------------------------------------------------

struct CurlResult {
	bool ok = false;
	std::string body;
};

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	auto* out = static_cast<std::string*>(userdata);
	out->append(ptr, size * nmemb);
	return size * nmemb;
}

// FASE 66 (D247 point 2): curl's own XFERINFOFUNCTION, forwarding straight
// to whatever callback Fetch() was given — no byte-counting logic of its
// own, that is ygo::launcher::FormatDownloadProgress/DownloadProgressPercent's
// job (gframe/launcher_logic.*, unit-tested). Returning nonzero would abort
// the transfer, which this never wants to do on its own.
using ProgressCallback = std::function<void(long long downloaded, long long total)>;

int XferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
	auto* cb = static_cast<ProgressCallback*>(clientp);
	if(cb && *cb)
		(*cb)(static_cast<long long>(dlnow), static_cast<long long>(dltotal));
	return 0;
}

// `on_progress`, when set, is called from curl's own thread of execution
// (synchronously, inside curl_easy_perform — libcurl has no background
// thread) roughly once per received chunk. Left null (the manifest
// doc/.sig fetches below both do) for anything small enough that a
// progress window would be pointless.
CurlResult Fetch(const std::string& url, long timeout_seconds, ProgressCallback on_progress = nullptr) {
	CurlResult result;
	CURL* curl = curl_easy_init();
	if(!curl)
		return result;
	char error_buffer[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
	curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "fedelex-launcher/1");
	// D249 point 1: the launcher never has a cacert.pem of its own (it has
	// no gGameConfig, no ssl_certificate_path, unlike gframe/curl.h's
	// ApplyCurlCertificateConfig() used by the simulator's curl sites) — on
	// Windows it always asks curl to use the OS certificate store instead.
	// Never touches CURLOPT_SSL_VERIFYPEER: verification stays on.
#if FEDELEX_WINDOWS
#if (LIBCURL_VERSION_NUM >= CURL_VERSION_BITS(7,71,0))
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
#endif
	if(on_progress) {
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, XferInfoCallback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &on_progress);
	}
	auto res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	result.ok = (res == CURLE_OK);
	return result;
}

// --- process handling (POSIX only — the Windows equivalent uses
// CreateProcessW/WaitForSingleObject directly at its two call sites below,
// InstallSimulatorFile's Windows body and step 5's final launch; there is
// no shared Windows helper here because CreateProcessW is already a single
// call, unlike fork()+execvp()) ---------

#if !FEDELEX_WINDOWS
// Spawns `path` with `args` (argv[0] included). Returns the child pid, or
// -1 on a fork/exec failure.
//
// execvp, not execv (FASE 66): the simulator-probe call site below already
// passes a full path (`tmp`, which always contains a '/'), so execvp
// behaves exactly like execv there — but it is also now reused by
// NotifyUser() above with a bare command name ("zenity", "notify-send",
// ...) that must be found on PATH. One function, one fork/exec
// implementation, for both uses.
pid_t SpawnDetached(const std::string& path, const std::vector<std::string>& args) {
	pid_t pid = fork();
	if(pid < 0)
		return -1;
	if(pid == 0) {
		std::vector<char*> argv;
		for(auto& a : args)
			argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);
		execvp(path.c_str(), argv.data());
		_exit(127); // execvp only returns on failure
	}
	return pid;
}

// Non-blocking "did it already die" check within `millis` of budget. See
// kLaunchProbeMillis above for what this approximates and why.
bool StillRunningAfter(pid_t pid, int millis) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
	while(std::chrono::steady_clock::now() < deadline) {
		int status = 0;
		pid_t r = waitpid(pid, &status, WNOHANG);
		if(r == pid)
			return false; // exited (whatever the code) before the deadline
		if(r < 0)
			return false; // ECHILD etc — treat as "not running"
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return true; // still alive when the budget ran out — treated as "launched"
}
#endif

// --- the swap for a single launcher_files[] entry ----------------------

struct Destination {
	std::string final_path;
	bool is_simulator = false;
};

Destination ResolveDestination(const ygo::update::LauncherFile& file,
								const std::string& program_dir,
								const std::string& data_dir) {
	Destination dest;
	if(file.role == ygo::update::LauncherFileRole::Simulator) {
		dest.final_path = program_dir + "/bin/" + file.name;
		dest.is_simulator = true;
	} else if(file.role == ygo::update::LauncherFileRole::Strings) {
		// D244 point 3: fixed destination filename, independent of the
		// manifest's own `name` for this entry — the role, not the upload
		// name, decides where it lands.
		MakeDir(data_dir + "/strings");
		dest.final_path = data_dir + "/strings/fedelex.conf";
	}
	return dest;
}

// Downloads+verifies+installs a Strings-role file (no executable bit, no
// launch probe — DecideSwap's state machine is specific to the simulator,
// see launcher_logic.h; for a plain data file "hash matches" is the whole
// contract). `progress` (FASE 66, may be null) gets the same MB-counter
// updates the simulator download gives it — a small file finishes in one
// or two ticks, which is correct: nothing here fakes progress that was not
// measured.
void InstallStringsFile(const ygo::update::LauncherFile& file, const Destination& dest,
						 const std::string& data_dir, ygo::launcher::ProgressWindow* progress) {
	// Same rule as the simulator: the installed file's own hash decides.
	// Without this the file was downloaded and replaced at every launch.
	if(ygo::launcher::DecideReplace(HashFile(dest.final_path), file.sha256) == ygo::launcher::ReplaceDecision::Keep) {
		Log(data_dir, "launcher_files: " + dest.final_path + " gia' aggiornato.");
		return;
	}
	auto fetched = Fetch(file.url, 20, [progress](long long dl, long long total) {
		if(progress)
			progress->Update(dl, total);
	});
	if(!fetched.ok) {
		Log(data_dir, "launcher_files: impossibile scaricare " + file.name + ", mantengo il file esistente.");
		return;
	}
	if(ygo::Sha256Hex(fetched.body) != file.sha256) {
		Log(data_dir, "launcher_files: SHA-256 di " + file.name + " non corrisponde al manifesto, scartato.");
		return;
	}
	std::string tmp = dest.final_path + ".new";
	if(!WriteFile(tmp, fetched.body)) {
		Log(data_dir, "launcher_files: impossibile scrivere " + tmp);
		return;
	}
	if(FileExists(dest.final_path))
		RenameFile(dest.final_path, dest.final_path + ".old");
	if(!RenameFile(tmp, dest.final_path)) {
		Log(data_dir, "launcher_files: impossibile installare " + dest.final_path);
		return;
	}
	Log(data_dir, "launcher_files: " + dest.final_path + " aggiornato.");
}

// Downloads+verifies+installs the simulator. The two bodies below (Linux
// vs Windows, split by #if) do the verification sequence in the OPPOSITE
// order — D249 point 2 — but both answer the same question for main():
// "did this function already launch the simulator, so step 5 must not
// launch it again". In every failure case of either body, dest.final_path
// is left as a file main() can still launch at step 5 — that invariant is
// called out again at each rename below, not just here.
#if !FEDELEX_WINDOWS
// Returns a pid of an already-launched child on success (InstallNew with
// the probe process still alive — that process IS the real launch, never
// spawned twice), or -1 if nothing was launched by this function (Keep, or
// any failure — caller falls back to launching whatever is already at
// dest.final_path).
pid_t InstallSimulatorFile(const ygo::update::LauncherFile& file, const Destination& dest,
							const std::string& data_dir, ygo::launcher::ProgressWindow* progress) {
	std::string installed_hash = HashFile(dest.final_path);
	auto replace = ygo::launcher::DecideReplace(installed_hash, file.sha256);
	if(replace == ygo::launcher::ReplaceDecision::Keep) {
		Log(data_dir, "launcher_files: " + dest.final_path + " gia' aggiornato.");
		return -1;
	}
	auto fetched = Fetch(file.url, 60, [progress](long long dl, long long total) {
		if(progress)
			progress->Update(dl, total);
	});
	if(!fetched.ok) {
		Log(data_dir, "launcher_files: impossibile scaricare " + file.name + ", mantengo il simulatore attuale.");
		return -1;
	}
	bool hash_ok = ygo::Sha256Hex(fetched.body) == file.sha256;
	if(!hash_ok) {
		Log(data_dir, "launcher_files: SHA-256 di " + file.name + " non corrisponde al manifesto, aggiornamento rifiutato.");
		return -1;
	}
	std::string tmp = dest.final_path + ".new";
	if(!WriteFile(tmp, fetched.body)) {
		Log(data_dir, "launcher_files: impossibile scrivere " + tmp);
		return -1;
	}
	bool exec_bit = SetExecutable(tmp);
	if(!exec_bit) {
		// "tries ONE corrective chmod" (launcher_logic.h) — one retry, then give up.
		exec_bit = SetExecutable(tmp);
	}
	if(!exec_bit) {
		Log(data_dir, "launcher_files: impossibile impostare il bit eseguibile su " + tmp + ", rollback.");
		RemoveFile(tmp);
		return -1;
	}
	// Probe: exec the .new binary directly (no rename yet — D244.5's
	// sequence, re-ordered so the untested file never overwrites a working
	// one). Passing -from-launcher so a probe that DOES come up fully is
	// indistinguishable from a normal launch and does not need a second
	// exec.
	pid_t pid = SpawnDetached(tmp, { tmp, "-from-launcher", "-C", data_dir });
	bool launches = pid > 0 && StillRunningAfter(pid, kLaunchProbeMillis);
	auto outcome = ygo::launcher::DecideSwap(hash_ok, exec_bit, launches);
	if(outcome == ygo::launcher::SwapOutcome::InstallNew) {
		if(FileExists(dest.final_path))
			RenameFile(dest.final_path, dest.final_path + ".old");
		RenameFile(tmp, dest.final_path); // same inode as what `pid` is running — safe on POSIX.
		Log(data_dir, "launcher_files: " + dest.final_path + " aggiornato e avviato.");
		return pid;
	}
	// RollbackToOld (or an unresolved FixExecutableBit after the one retry
	// above — treated the same: never ship a binary this launcher could not
	// verify). dest.final_path was never touched, so there is nothing to
	// restore — this IS the rollback, by construction (verify before
	// commit, not commit then undo).
	if(pid > 0) {
		int status = 0;
		waitpid(pid, &status, WNOHANG); // reap if it already exited; leave it alone if (implausibly) still running after a failed probe read.
	}
	RemoveFile(tmp);
	NotifyUser("Aggiornamento EDOPro (Fedelex custom)",
			   "Il nuovo simulatore non si e' avviato correttamente: mantengo la versione attuale.");
	Log(data_dir, "launcher_files: " + file.name + " non si avvia, rollback (nessuna modifica su disco).");
	return -1;
}
#else
// D249 point 2: the opposite order from Linux. Linux can safely probe the
// downloaded file from its own .new path because replacing a running
// executable is safe on POSIX; on Windows, launching the probe from a path
// OTHER than `final` (ygoprodll.new.exe instead of ygoprodll.exe) would ask
// the firewall to authorize a binary that is about to disappear — the
// player would see a prompt for a file that is gone moments later. So
// here: swap first (final -> final.old, .new -> final), THEN probe `final`
// itself. Returns true if the probe succeeded — that probe process IS the
// real launch, main() must not spawn a second one. Returns false
// otherwise (nothing needed replacing, a download/verify step failed
// before anything on disk was touched, or the probe failed and the swap
// was undone) — dest.final_path is a launchable file in every one of
// those cases, see the invariant comment at each rename below.
bool InstallSimulatorFile(const ygo::update::LauncherFile& file, const Destination& dest,
							const std::string& data_dir, ygo::launcher::ProgressWindow* progress) {
	std::string installed_hash = HashFile(dest.final_path);
	auto replace = ygo::launcher::DecideReplace(installed_hash, file.sha256);
	if(replace == ygo::launcher::ReplaceDecision::Keep) {
		Log(data_dir, "launcher_files: " + dest.final_path + " gia' aggiornato.");
		return false;
	}
	auto fetched = Fetch(file.url, 60, [progress](long long dl, long long total) {
		if(progress)
			progress->Update(dl, total);
	});
	if(!fetched.ok) {
		Log(data_dir, "launcher_files: impossibile scaricare " + file.name + ", mantengo il simulatore attuale.");
		return false; // invariant: dest.final_path untouched, still there.
	}
	bool hash_ok = ygo::Sha256Hex(fetched.body) == file.sha256;
	if(!hash_ok) {
		Log(data_dir, "launcher_files: SHA-256 di " + file.name + " non corrisponde al manifesto, aggiornamento rifiutato.");
		return false; // invariant: dest.final_path untouched, still there.
	}
	std::string tmp = dest.final_path + ".new";
	if(!WriteFile(tmp, fetched.body)) {
		Log(data_dir, "launcher_files: impossibile scrivere " + tmp);
		return false; // invariant: dest.final_path untouched, still there.
	}
	// Windows has no POSIX exec bit (IsExecutable/SetExecutable already
	// encode "presence is the whole check" for this platform) — there is
	// nothing to set or retry, unlike the Linux body above. Passed through
	// DecideSwap anyway so both bodies ask the same pure function the same
	// question; it is simply always true here.
	constexpr bool exec_bit = true;
	std::string old_path = dest.final_path + ".old";
	bool had_old = FileExists(dest.final_path); // see step 1 of main(): always true in practice, the simulator is already verified present before step 4 runs — checked anyway, never assumed.
	if(had_old && !RenameFile(dest.final_path, old_path)) {
		// invariant: the move failed, so dest.final_path is exactly what it
		// was before this call (RenameFile only removes its destination,
		// .old, before renaming — see RenameFile's own comment) — still there.
		Log(data_dir, "launcher_files: impossibile spostare " + dest.final_path + " in " + old_path + ", aggiornamento annullato.");
		RemoveFile(tmp);
		return false;
	}
	if(!RenameFile(tmp, dest.final_path)) {
		// Rename failed partway: put .old back FIRST, before anything else
		// — D249's own wording ("la cartella non resta mai senza un
		// simulatore avviabile"). invariant: after this restore,
		// dest.final_path is back to what it was before this call.
		if(had_old)
			RenameFile(old_path, dest.final_path);
		Log(data_dir, "launcher_files: impossibile installare " + dest.final_path + ", ripristinato il precedente.");
		return false;
	}
	// invariant: dest.final_path now holds the NEW bytes — still there,
	// just not yet proven to launch. Probe it directly (never `.new`, which
	// no longer exists after the rename above): same provenance mark as a
	// normal launch, so a probe that fully comes up is indistinguishable
	// from one and needs no second CreateProcessW.
	std::wstring wfinal(dest.final_path.begin(), dest.final_path.end());
	std::wstring wdata(data_dir.begin(), data_dir.end());
	std::wstring cmdline = L"\"" + wfinal + L"\" -from-launcher -C \"" + wdata + L"\"";
	std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
	cmdline_buf.push_back(0);
	STARTUPINFOW si{}; si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	bool spawned = CreateProcessW(wfinal.c_str(), cmdline_buf.data(), nullptr, nullptr, FALSE, 0,
								   nullptr, nullptr, &si, &pi) != 0;
	bool launches = false;
	if(spawned) {
		CloseHandle(pi.hThread);
		// WAIT_TIMEOUT within the budget = still running = launched, same
		// reading as StillRunningAfter() in the Linux body above.
		launches = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(kLaunchProbeMillis)) == WAIT_TIMEOUT;
		CloseHandle(pi.hProcess); // either it IS the real launch (nothing left to join) or it already exited (nothing left to reap) — never waited on further either way.
	}
	auto outcome = ygo::launcher::DecideSwap(hash_ok, exec_bit, launches);
	if(outcome == ygo::launcher::SwapOutcome::InstallNew) {
		Log(data_dir, "launcher_files: " + dest.final_path + " aggiornato e avviato.");
		return true;
	}
	// RollbackToOld (the only other outcome reachable here — hash_ok is
	// already true by construction, and exec_bit is always true on
	// Windows, so FixExecutableBit never triggers in this body). Unlike
	// the Linux body, `final` was ALREADY swapped — so "roll back" means
	// actually undoing it: final -> final.failed, then .old -> final.
	std::string failed_path = dest.final_path + ".failed";
	if(!RenameFile(dest.final_path, failed_path))
		Log(data_dir, "launcher_files: impossibile spostare " + dest.final_path + " in " + failed_path + ".");
	// invariant: whether or not the line above succeeded, dest.final_path
	// must hold a launchable file again after the next line — that is the
	// whole point of this restore, not a best-effort extra.
	if(had_old && !RenameFile(old_path, dest.final_path))
		Log(data_dir, "launcher_files: impossibile ripristinare " + old_path + " in " + dest.final_path + " -- la cartella potrebbe restare senza simulatore.");
	NotifyUser("Aggiornamento EDOPro (Fedelex custom)",
			   "Il nuovo simulatore non si e' avviato correttamente: mantengo la versione attuale.");
	Log(data_dir, "launcher_files: " + file.name + " non si avvia, ripristinato " + dest.final_path + ".");
	return false;
}
#endif

} // namespace

int main(int argc, char** argv) {
	(void)argc; (void)argv;
	curl_global_init(CURL_GLOBAL_DEFAULT);

#if !FEDELEX_WINDOWS
	// FASE 66 / design/decisioni.md D247 point 4: a write() to the update
	// window's pipe after zenity has already exited (died, or never read
	// anything) must come back as an ordinary EPIPE error that
	// progress.cpp's ProgressWindow::Update() can log and recover from —
	// not SIGPIPE's default action, which terminates this process. This
	// is the one global, process-wide setting the whole "a dead window
	// never blocks or breaks the update" rule depends on; it belongs in
	// main(), not in progress.cpp, because it has to be set exactly once,
	// before anything could possibly write to that pipe.
	std::signal(SIGPIPE, SIG_IGN);
#endif

	// Step 1: locate self, PROGRAM_DIR, the simulator.
#if FEDELEX_WINDOWS
	wchar_t self_path_w[MAX_PATH];
	GetModuleFileNameW(nullptr, self_path_w, MAX_PATH);
	std::wstring wself(self_path_w);
	// Limite noto, non chiuso qui (D249): narrowing carattere per carattere,
	// non una vera conversione di codepage — regge le lettere accentate
	// (code page 1252), non un percorso in un altro alfabeto.
	std::string self_path(wself.begin(), wself.end());
#else
	char self_path_buf[4096];
	ssize_t len = readlink("/proc/self/exe", self_path_buf, sizeof(self_path_buf) - 1);
	std::string self_path = (len > 0) ? std::string(self_path_buf, len) : std::string(argv[0] ? argv[0] : "");
#endif
	std::string program_dir = DirName(self_path);
	std::string simulator_path = program_dir + "/bin/" + kSimulatorName;

	// Step 2: DATA_DIR. D244.7/install.sh writes PROGRAM_DIR/.data-dir
	// (one line, the path). Absent (manual/dev run, or a layout the
	// installer did not create) => fall back to PROGRAM_DIR itself: wrong
	// for a real player install but never fatal, and logged so it is never
	// silent (design/launcher.md §7).
	std::string data_dir = program_dir;
	{
		std::string marker = program_dir + "/.data-dir";
		if(FileExists(marker)) {
			std::string contents = ReadFile(marker);
			while(!contents.empty() && (contents.back() == '\n' || contents.back() == '\r'))
				contents.pop_back();
			if(!contents.empty())
				data_dir = contents;
		}
	}
	MakeDir(data_dir);
	Log(data_dir, "avvio, program_dir=" + program_dir + " data_dir=" + data_dir);

	if(!IsExecutable(simulator_path)) {
		std::string msg = "Simulatore non trovato o non eseguibile: " + simulator_path;
		Log(data_dir, msg);
		NotifyUser("EDOPro (Fedelex custom)", msg + "\nReinstalla il client.");
		return EXIT_FAILURE;
	}

	// Step 3: manifest.
	ygo::update::Manifest manifest;
	bool have_manifest = false;
	{
		auto doc = Fetch(FEDELEX_UPDATE_URL, kManifestTimeoutSeconds);
		auto sig = doc.ok ? Fetch(std::string(FEDELEX_UPDATE_URL) + ".sig", kManifestTimeoutSeconds) : CurlResult{};
		if(doc.ok && sig.ok) {
			std::string error;
			auto status = ygo::update::VerifyAndParse(doc.body, sig.body, manifest, error);
			if(status == ygo::update::VerifyStatus::Ok) {
				have_manifest = true;
			} else {
				Log(data_dir, "manifesto rifiutato: " + error);
			}
		} else {
			Log(data_dir, "manifesto irraggiungibile, procedo senza aggiornare.");
		}
	}

	// Step 4: launcher_files, same cycle on every platform now (FASE 67,
	// D249): anti-rollback on the launcher's own state, select+resolve
	// every entry, open the update window only if something needs
	// replacing, install each entry, close the window, launch if the
	// simulator swap already did. The only platform split left is INSIDE
	// the loop (InstallSimulatorFile has two bodies — see its #if above
	// for why the verify/swap order is reversed on Windows) and in how
	// "already launched" is represented (a pid on POSIX, a bool on
	// Windows — CreateProcessW's PROCESS_INFORMATION is not a pid_t).
	if(have_manifest) {
		std::string state_path = data_dir + "/launcher-state.json";
		int stored_version = 0;
		{
			std::string raw = ReadFile(state_path);
			auto pos = raw.find("\"version\"");
			if(pos != std::string::npos) {
				auto colon = raw.find(':', pos);
				if(colon != std::string::npos)
					stored_version = std::atoi(raw.c_str() + colon + 1);
			}
		}
		auto acceptance = ygo::launcher::DecideManifestAcceptance(manifest.version, stored_version);
		if(acceptance == ygo::launcher::ManifestAcceptance::RejectRollback) {
			Log(data_dir, "manifesto version=" + std::to_string(manifest.version) +
						   " piu' vecchio di quello gia' accettato (" + std::to_string(stored_version) + "), ignorato.");
		} else {
			auto files = ygo::update::SelectLauncherFilesForPlatform(manifest.launcher_files, kPlatform);

			// FASE 66 / D247 point 1: resolve every destination and its
			// replace decision BEFORE opening anything. The update window
			// (if any) is constructed exactly once, outside the loop below,
			// and ONLY if at least one entry actually needs replacing —
			// "una finestra compare solo quando c'e' qualcosa da
			// scaricare" is enforced here, not by the window itself (which
			// has no way to know whether it should have existed).
			std::vector<Destination> destinations;
			destinations.reserve(files.size());
			bool any_replace = false;
			for(auto& file : files) {
				auto dest = ResolveDestination(file, program_dir, data_dir);
				if(!dest.final_path.empty() &&
				   ygo::launcher::DecideReplace(HashFile(dest.final_path), file.sha256) == ygo::launcher::ReplaceDecision::Replace)
					any_replace = true;
				destinations.push_back(std::move(dest));
			}

			std::unique_ptr<ygo::launcher::ProgressWindow> progress;
			if(any_replace)
				progress = std::make_unique<ygo::launcher::ProgressWindow>(data_dir);

#if FEDELEX_WINDOWS
			bool simulator_already_launched = false;
#else
			pid_t launched_pid = -1;
#endif
			for(size_t i = 0; i < files.size(); ++i) {
				auto& file = files[i];
				auto& dest = destinations[i];
				if(dest.final_path.empty())
					continue;
				if(dest.is_simulator) {
#if FEDELEX_WINDOWS
					if(InstallSimulatorFile(file, dest, data_dir, progress.get()))
						simulator_already_launched = true;
#else
					pid_t pid = InstallSimulatorFile(file, dest, data_dir, progress.get());
					if(pid > 0)
						launched_pid = pid;
#endif
				} else {
					InstallStringsFile(file, dest, data_dir, progress.get());
				}
			}
			if(acceptance == ygo::launcher::ManifestAcceptance::Accept) {
				std::ofstream state(state_path, std::ios::trunc);
				if(state)
					state << "{\"version\":" << manifest.version << "}\n";
			}
			// D247 point 2: "Avvio..." then the window closes itself, right
			// before control hands off to the simulator either way (the
			// early return just below, or step 5 further down).
			if(progress)
				progress->Finish();
#if FEDELEX_WINDOWS
			if(simulator_already_launched) {
				// The simulator swap's own probe became the real launch —
				// nothing left to do.
				Log(data_dir, "simulatore gia' avviato durante la sostituzione.");
				curl_global_cleanup();
				return EXIT_SUCCESS;
			}
#else
			if(launched_pid > 0) {
				// The simulator swap's own probe became the real launch —
				// nothing left to do.
				Log(data_dir, "simulatore gia' avviato durante la sostituzione (pid " + std::to_string(launched_pid) + ").");
				curl_global_cleanup();
				return EXIT_SUCCESS;
			}
#endif
		}
	}

	// Step 5: normal launch (no update applicable, or the swap above never
	// got to InstallNew/true — either way `simulator_path` is a file this
	// launcher already confirmed is present and executable).
	// -C data_dir: without it the simulator chdirs to its own folder
	// (gframe/gframe.cpp, WORK_DIR default is GetExeFolder()) instead of
	// DATA_DIR — wrong whenever the two differ, which is the common case
	// (resolve_data_dir() in install.sh defaults to a sibling directory,
	// not PROGRAM_DIR/bin). Found while testing cancello 6 end to end in
	// a fake HOME: the old bash wrapper this launcher replaces always
	// passed -C itself.
#if FEDELEX_WINDOWS
	curl_global_cleanup();
	std::wstring wsim(simulator_path.begin(), simulator_path.end());
	std::wstring wdata(data_dir.begin(), data_dir.end());
	std::wstring cmdline = L"\"" + wsim + L"\" -from-launcher -C \"" + wdata + L"\"";
	std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
	cmdline_buf.push_back(0);
	STARTUPINFOW si{}; si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	if(CreateProcessW(wsim.c_str(), cmdline_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return EXIT_SUCCESS;
	}
	Log(data_dir, "impossibile avviare " + simulator_path);
	NotifyUser("EDOPro (Fedelex custom)", "Impossibile avviare il simulatore.");
	return EXIT_FAILURE;
#else
	std::vector<std::string> args = { simulator_path, "-from-launcher", "-C", data_dir };
	std::vector<char*> argv_exec;
	for(auto& a : args)
		argv_exec.push_back(const_cast<char*>(a.c_str()));
	argv_exec.push_back(nullptr);
	curl_global_cleanup();
	execv(simulator_path.c_str(), argv_exec.data());
	// execv only returns on failure.
	Log(data_dir, "impossibile avviare " + simulator_path + ": " + std::strerror(errno));
	NotifyUser("EDOPro (Fedelex custom)", "Impossibile avviare il simulatore: " + std::string(std::strerror(errno)));
	return EXIT_FAILURE;
#endif
}
