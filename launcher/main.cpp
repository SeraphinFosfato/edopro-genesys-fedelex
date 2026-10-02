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
//      without a working simulator.
//   5. Run the simulator with the provenance mark.
//
// Linux only has been built and run end to end (cancelli 3-5 were tested
// on this platform). The Windows branches below follow the same contract
// (MessageBoxW instead of a desktop notifier, .exe suffix, no chmod) but
// have NEVER been compiled — there is no Windows toolchain in this
// environment. Flagged honestly in design/launcher.md's stato
// dell'implementazione; do not read "the code has an #ifdef for it" as "it
// works on Windows" (§1.9 of the vault CLAUDE.md).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "update_verify.h"
#include "launcher_logic.h"
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
// D244 point 1: zenity/kdialog if present, else notify-send, always also
// the log file (handled by the caller via Log()). A missing notifier is
// not an error — the log line is the ground truth either way.
void NotifyUser(const std::string& title, const std::string& message) {
	auto try_run = [](const std::string& cmd) {
		return std::system(cmd.c_str()) == 0;
	};
	auto quote = [](const std::string& s) {
		std::string out = "'";
		for(char c : s) {
			if(c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		out += "'";
		return out;
	};
	// "timeout 5" in front of every notifier call, not just the
	// command -v probe: found empirically (FASE 64 cancello 4, a session
	// with DISPLAY set but no reachable X/DBus session — exactly a
	// screen-less server or a sandboxed test run) that zenity blocks
	// indefinitely trying to reach a display instead of failing fast. A
	// notifier is a comodo (design/launcher.md §7 only requires the log,
	// which the caller always writes); it must never be able to make the
	// launcher itself hang — that would turn "non bloccare mai" into its
	// opposite.
	if(std::system("timeout 2 command -v zenity >/dev/null 2>&1") == 0) {
		try_run("timeout 5 zenity --info --title=" + quote(title) + " --text=" + quote(message) + " 2>/dev/null");
		return;
	}
	if(std::system("timeout 2 command -v kdialog >/dev/null 2>&1") == 0) {
		try_run("timeout 5 kdialog --title=" + quote(title) + " --msgbox=" + quote(message) + " 2>/dev/null");
		return;
	}
	if(std::system("timeout 2 command -v notify-send >/dev/null 2>&1") == 0) {
		try_run("timeout 5 notify-send " + quote(title) + " " + quote(message) + " 2>/dev/null");
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

bool MoveFile(const std::string& from, const std::string& to) {
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

CurlResult Fetch(const std::string& url, long timeout_seconds) {
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
	auto res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	result.ok = (res == CURLE_OK);
	return result;
}

// --- process handling (POSIX only — the Windows equivalent is
// CreateProcessW; not implemented, see the file-level comment) ---------

#if !FEDELEX_WINDOWS
// Spawns `path` with `args` (argv[0] included). Returns the child pid, or
// -1 on a fork/exec failure.
pid_t SpawnDetached(const std::string& path, const std::vector<std::string>& args) {
	pid_t pid = fork();
	if(pid < 0)
		return -1;
	if(pid == 0) {
		std::vector<char*> argv;
		for(auto& a : args)
			argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);
		execv(path.c_str(), argv.data());
		_exit(127); // execv only returns on failure
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
// contract).
void InstallStringsFile(const ygo::update::LauncherFile& file, const Destination& dest,
						 const std::string& data_dir) {
	auto fetched = Fetch(file.url, 20);
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
		MoveFile(dest.final_path, dest.final_path + ".old");
	if(!MoveFile(tmp, dest.final_path)) {
		Log(data_dir, "launcher_files: impossibile installare " + dest.final_path);
		return;
	}
	Log(data_dir, "launcher_files: " + dest.final_path + " aggiornato.");
}

// Downloads+verifies+installs the simulator. Returns a pid of an
// already-launched child on success (InstallNew with the probe process
// still alive — that process IS the real launch, never spawned twice), or
// -1 if nothing was launched by this function (Keep, or any failure —
// caller falls back to launching whatever is already at dest.final_path).
#if !FEDELEX_WINDOWS
pid_t InstallSimulatorFile(const ygo::update::LauncherFile& file, const Destination& dest,
							const std::string& data_dir) {
	std::string installed_hash = HashFile(dest.final_path);
	auto replace = ygo::launcher::DecideReplace(installed_hash, file.sha256);
	if(replace == ygo::launcher::ReplaceDecision::Keep) {
		Log(data_dir, "launcher_files: " + dest.final_path + " gia' aggiornato.");
		return -1;
	}
	auto fetched = Fetch(file.url, 60);
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
			MoveFile(dest.final_path, dest.final_path + ".old");
		MoveFile(tmp, dest.final_path); // same inode as what `pid` is running — safe on POSIX.
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
#endif

} // namespace

int main(int argc, char** argv) {
	(void)argc; (void)argv;
	curl_global_init(CURL_GLOBAL_DEFAULT);

	// Step 1: locate self, PROGRAM_DIR, the simulator.
#if FEDELEX_WINDOWS
	wchar_t self_path_w[MAX_PATH];
	GetModuleFileNameW(nullptr, self_path_w, MAX_PATH);
	std::wstring wself(self_path_w);
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

	// Step 4: launcher_files, only on Linux for now (SpawnDetached/probe is
	// POSIX-only — see InstallSimulatorFile's #if above). Windows installs
	// nothing through this path yet; it still launches the simulator below.
#if !FEDELEX_WINDOWS
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
			pid_t launched_pid = -1;
			for(auto& file : files) {
				auto dest = ResolveDestination(file, program_dir, data_dir);
				if(dest.final_path.empty())
					continue;
				if(dest.is_simulator) {
					pid_t pid = InstallSimulatorFile(file, dest, data_dir);
					if(pid > 0)
						launched_pid = pid;
				} else {
					InstallStringsFile(file, dest, data_dir);
				}
			}
			if(acceptance == ygo::launcher::ManifestAcceptance::Accept) {
				std::ofstream state(state_path, std::ios::trunc);
				if(state)
					state << "{\"version\":" << manifest.version << "}\n";
			}
			if(launched_pid > 0) {
				// The simulator swap's own probe became the real launch —
				// nothing left to do.
				Log(data_dir, "simulatore gia' avviato durante la sostituzione (pid " + std::to_string(launched_pid) + ").");
				curl_global_cleanup();
				return EXIT_SUCCESS;
			}
		}
	}

	// Step 5: normal launch (no update applicable, or the swap above never
	// got to InstallNew — either way `simulator_path` is a file this
	// launcher already confirmed is present and executable).
	// -C data_dir: without it the simulator chdirs to its own folder
	// (gframe/gframe.cpp, WORK_DIR default is GetExeFolder()) instead of
	// DATA_DIR — wrong whenever the two differ, which is the common case
	// (resolve_data_dir() in install.sh defaults to a sibling directory,
	// not PROGRAM_DIR/bin). Found while testing cancello 6 end to end in
	// a fake HOME: the old bash wrapper this launcher replaces always
	// passed -C itself.
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
#else
	// Windows: step 4 (launcher_files install) not implemented yet (see the
	// file-level comment) — launch the simulator as-is.
	curl_global_cleanup();
	std::wstring wsim(simulator_path.begin(), simulator_path.end());
	STARTUPINFOW si{}; si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring wdata(data_dir.begin(), data_dir.end());
	std::wstring cmdline = L"\"" + wsim + L"\" -from-launcher -C \"" + wdata + L"\"";
	std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
	cmdline_buf.push_back(0);
	if(CreateProcessW(wsim.c_str(), cmdline_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return EXIT_SUCCESS;
	}
	NotifyUser("EDOPro (Fedelex custom)", "Impossibile avviare il simulatore.");
	return EXIT_FAILURE;
#endif
}
