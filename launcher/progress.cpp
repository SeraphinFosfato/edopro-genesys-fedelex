// design/decisioni.md D247, FASE 66. See progress.h for the lifecycle
// contract. Split in two halves by #if, same style as launcher/main.cpp
// itself (one file, platform branches inline) rather than two separate
// translation units — the two halves share nothing (not even a helper),
// so a split file would only have added a premake filter for no benefit.

#include "progress.h"
#include "launcher_logic.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ygo::launcher {

namespace {
void LogLine(const std::string& data_dir, const std::string& line) {
	std::fprintf(stderr, "[fedelex-launcher] %s\n", line.c_str());
	if(data_dir.empty())
		return;
	std::ofstream log(data_dir + "/launcher.log", std::ios::app);
	if(log)
		log << line << '\n';
}
}

#if !defined(_WIN32)

namespace {

// Same probe discipline as main.cpp's NotifyUser: capped at a couple of
// seconds, never the real dialog — found necessary the same way (FASE 64
// cancello 4): a graphical-looking environment with no reachable
// display/session can make `command -v` itself hang trying to talk to
// something.
//
// `sh -c 'command -v NAME'`, not a bare `command -v NAME`: see the matching
// (longer) comment on main.cpp's NotifyUser, found while writing this
// phase's fake-zenity test — `timeout` execs its argument directly rather
// than through a shell, and `command` is a shell builtin with no file on
// PATH, so the bare form always failed and this probe always reported
// "absent".
bool CommandExists(const char* name) {
	std::string cmd = "timeout 2 sh -c 'command -v ";
	cmd += name;
	cmd += "' >/dev/null 2>&1";
	return std::system(cmd.c_str()) == 0;
}

bool HasGraphicalSession() {
	const char* display = std::getenv("DISPLAY");
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	return (display && *display) || (wayland && *wayland);
}

// fork()+execvp() a command and never wait for it (D247 point 5 — a
// detached notifier is what makes "non si chiude da solo" true: a
// `system()`/`popen()` call here would mean THIS process is the one
// blocking, which is exactly the bug D247 opens with, just moved one
// level up). execvp (not execv): the two commands used here (notify-send,
// zenity) are found on PATH, not at a path this code resolved itself.
bool SpawnDetachedCommand(const std::vector<std::string>& argv) {
	if(argv.empty())
		return false;
	pid_t pid = fork();
	if(pid < 0)
		return false;
	if(pid == 0) {
		std::vector<char*> cargv;
		cargv.reserve(argv.size() + 1);
		for(auto& a : argv)
			cargv.push_back(const_cast<char*>(a.c_str()));
		cargv.push_back(nullptr);
		execvp(cargv[0], cargv.data());
		_exit(127); // execvp only returns on failure
	}
	return true;
}

}

ProgressWindow::ProgressWindow(const std::string& data_dir) : data_dir_(data_dir) {
	// D247 point 4: zenity only if BOTH a graphical session is reachable
	// AND zenity is actually on PATH — "mai una condizione per
	// aggiornare": if neither holds, every method below is already a
	// correct, silent no-op, and the download proceeds exactly the same.
	if(HasGraphicalSession() && CommandExists("zenity")) {
		int fds[2];
		if(pipe(fds) == 0) {
			pid_t pid = fork();
			if(pid == 0) {
				dup2(fds[0], STDIN_FILENO);
				close(fds[0]);
				close(fds[1]);
				execlp("zenity", "zenity", "--progress",
					   "--title=EDOPro (Fedelex custom)",
					   "--text=Aggiornamento in corso...",
					   "--percentage=0", "--auto-close", "--no-cancel",
					   static_cast<char*>(nullptr));
				_exit(127);
			} else if(pid > 0) {
				close(fds[0]);
				write_fd_ = fds[1];
				pid_ = pid;
			} else {
				close(fds[0]);
				close(fds[1]);
			}
		}
	}

	if(write_fd_ >= 0) {
		LogLine(data_dir_, "finestra di aggiornamento: zenity --progress (pid " + std::to_string(pid_) + ")");
		return;
	}

	// Fallback (D247 point 4): no graphical toolkit beyond what is already
	// there (D244.1, "mai una libreria grafica nel launcher") — one
	// notification at the start, one at the end, never a progress bar.
	if(CommandExists("notify-send")) {
		SpawnDetachedCommand({"notify-send", "EDOPro (Fedelex custom)", "Aggiornamento in corso..."});
		notify_send_begun_ = true;
	}
	LogLine(data_dir_, notify_send_begun_
		? "finestra di aggiornamento: nessuna sessione grafica/zenity, notify-send di avvio inviata"
		: "finestra di aggiornamento: nessuna (zenity e notify-send entrambi assenti/non raggiungibili)");
}

void ProgressWindow::Update(long long downloaded_bytes, long long total_bytes) {
	if(write_fd_ < 0)
		return; // no zenity window — nothing to push to (the notify-send fallback has no running counter).
	const std::string text = FormatDownloadProgress(downloaded_bytes, total_bytes);
	const int percent = DownloadProgressPercent(downloaded_bytes, total_bytes);
	const std::string line = std::to_string(percent) + "\n# Aggiornamento in corso... " + text + "\n";
	// zenity's --progress protocol: a bare "NN\n" line sets the bar, a
	// "# text\n" line replaces the label. Both in one write() — zenity
	// reads line by line regardless of how they arrive in one buffer.
	//
	// D247 point 4 / launcher.md §7: a write() to a pipe whose reader has
	// already exited must never take this process down with it. SIGPIPE
	// is ignored once, in launcher/main.cpp's main() — see the comment
	// there — specifically so this write() returns -1/EPIPE instead of
	// raising a signal. Without that, a fake zenity that "dies right
	// away" (FASE 66 cancello 2's own test) would kill the launcher on
	// the very next Update(), which is the opposite of "l'aggiornamento
	// finisce lo stesso".
	ssize_t written = write(write_fd_, line.data(), line.size());
	if(written < 0) {
		close(write_fd_);
		write_fd_ = -1;
		LogLine(data_dir_, "finestra di aggiornamento: zenity non risponde piu', continuo senza.");
	}
}

void ProgressWindow::Finish() {
	if(write_fd_ >= 0) {
		const std::string line = "100\n# Avvio...\n";
		write(write_fd_, line.data(), line.size()); // best effort, see Update()'s comment
		close(write_fd_);
		write_fd_ = -1;
		// Deliberately NOT waitpid()'d: D247 point 2 says the window
		// "si chiude da sola" once its stdin reaches EOF (which the
		// close() above just caused) or it reads "100" with
		// --auto-close — either way that is zenity's own business from
		// here, and waiting for its exit status would reintroduce
		// exactly the blocking this design avoids everywhere else. One
		// non-blocking reap attempt costs nothing and avoids leaving an
		// avoidable zombie around if it already exited.
		if(pid_ > 0) {
			int status = 0;
			waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
		}
		return;
	}
	if(notify_send_begun_) {
		SpawnDetachedCommand({"notify-send", "EDOPro (Fedelex custom)", "Avvio..."});
		notify_send_begun_ = false; // idempotent: a second Finish() (dtor after an explicit call) sends nothing more.
	}
}

ProgressWindow::~ProgressWindow() {
	Finish();
}

#else // defined(_WIN32)

namespace {

constexpr wchar_t kClassName[] = L"FedelexLauncherProgress";

LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	// Cosmetic window only (D247 point 1: it never gates the update) — the
	// close button is disabled by simply swallowing WM_CLOSE; the window
	// is destroyed only from Finish()/the destructor, never by the user.
	if(msg == WM_CLOSE)
		return 0;
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// Drains whatever is already queued, never blocks (no GetMessage). Called
// from the constructor and from every Update()/Finish() so the window
// stays responsive without a dedicated UI thread — curl's own progress
// callback already ticks often enough to keep this from looking frozen.
void PumpPendingMessages() {
	MSG msg;
	while(PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

}

ProgressWindow::ProgressWindow(const std::string& /*data_dir*/) {
	INITCOMMONCONTROLSEX icc{};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&icc);

	static bool class_registered = false;
	if(!class_registered) {
		WNDCLASSW wc{};
		wc.lpfnWndProc = ProgressWndProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = kClassName;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE) + 1);
		RegisterClassW(&wc); // a failure here (e.g. already registered by a previous instance in the same process) is not fatal — CreateWindowExW below is the real check.
		class_registered = true;
	}

	HWND hwnd = CreateWindowExW(
		WS_EX_DLGMODALFRAME, kClassName, L"EDOPro (Fedelex custom)",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 380, 130,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if(!hwnd)
		return; // D247 point 4: a window that fails to create is a silent no-op, never a failed update.

	HWND label = CreateWindowExW(0, L"STATIC", L"Aggiornamento in corso...",
		WS_CHILD | WS_VISIBLE, 16, 16, 340, 20,
		hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
	HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
		WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 16, 48, 340, 24,
		hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
	if(bar) {
		SendMessageW(bar, PBM_SETRANGE32, 0, 100);
		SendMessageW(bar, PBM_SETPOS, 0, 0);
	}

	hwnd_ = hwnd;
	progress_bar_ = bar;
	label_ = label;
	ShowWindow(hwnd, SW_SHOW);
	PumpPendingMessages();
}

void ProgressWindow::Update(long long downloaded_bytes, long long total_bytes) {
	if(!hwnd_)
		return;
	const int percent = DownloadProgressPercent(downloaded_bytes, total_bytes);
	const std::string text = "Aggiornamento in corso... " + FormatDownloadProgress(downloaded_bytes, total_bytes);
	if(progress_bar_)
		SendMessageW(reinterpret_cast<HWND>(progress_bar_), PBM_SETPOS, static_cast<WPARAM>(percent), 0);
	if(label_) {
		// ASCII-only text (digits, comma, '/', "MB", the fixed Italian
		// words above) — the same narrow-to-wide widening main.cpp
		// already uses for MessageBoxW's own strings, never a real
		// codepage conversion.
		std::wstring wtext(text.begin(), text.end());
		SetWindowTextW(reinterpret_cast<HWND>(label_), wtext.c_str());
	}
	PumpPendingMessages();
}

void ProgressWindow::Finish() {
	if(!hwnd_)
		return;
	if(progress_bar_)
		SendMessageW(reinterpret_cast<HWND>(progress_bar_), PBM_SETPOS, 100, 0);
	if(label_)
		SetWindowTextW(reinterpret_cast<HWND>(label_), L"Avvio...");
	PumpPendingMessages();
	// D247 point 2: "si chiude da sola quando il simulatore parte" — the
	// caller calls Finish() right before spawning the simulator, so
	// destroying the window here (rather than leaving it up for a fixed
	// delay nobody specified) IS that behaviour: the window disappearing
	// is the signal the handoff happened, same role as zenity's
	// --auto-close on the Linux side.
	DestroyWindow(reinterpret_cast<HWND>(hwnd_));
	hwnd_ = nullptr;
	progress_bar_ = nullptr;
	label_ = nullptr;
}

ProgressWindow::~ProgressWindow() {
	Finish();
}

#endif

}
