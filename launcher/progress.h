#ifndef FEDELEX_LAUNCHER_PROGRESS_H
#define FEDELEX_LAUNCHER_PROGRESS_H

// The update window itself (FASE 66, design/decisioni.md D247, completing
// design/launcher.md). Everything this class does is real I/O (a child
// process and a pipe on Linux, a native window on Windows) — same
// discipline as launcher/main.cpp's own docstring: this is NOT where the
// decision logic lives (that is gframe/launcher_logic.h's
// FormatDownloadProgress/DownloadProgressPercent, which ARE unit-tested),
// this is only the thin orchestration around showing it.
//
// Lifecycle (D247 points 1-2):
//   1. Construct ONLY when something is actually about to be downloaded —
//      never "just in case" (D247 point 1: "una finestra compare solo
//      quando c'e' qualcosa da scaricare"). The caller (launcher/main.cpp)
//      decides this; the constructor itself does no such check.
//   2. Update() once per curl progress tick while a file is downloading.
//   3. Finish() right before the simulator is spawned. The destructor
//      calls Finish() too, so a caller that forgets is still safe.
//
// The one rule every implementation obeys without exception (D247 point 4,
// design/launcher.md §7 "non bloccare mai"): a window that cannot be
// created, dies mid-update, or never responds NEVER blocks the update and
// NEVER turns into a failure. Every method below is allowed to silently do
// nothing — that is the correct behaviour, not a missing error path.
#include <string>

namespace ygo::launcher {

class ProgressWindow {
public:
	// `data_dir` is where launcher.log lives (same file main.cpp already
	// writes to) — used only to log what this window decided to do
	// (zenity vs. notify-send vs. nothing), never to read or write
	// anything else.
	explicit ProgressWindow(const std::string& data_dir);
	~ProgressWindow();

	ProgressWindow(const ProgressWindow&) = delete;
	ProgressWindow& operator=(const ProgressWindow&) = delete;

	// Called from curl's progress callback (launcher/main.cpp's Fetch()),
	// potentially many times per second — must be cheap and must never
	// block on a window that stopped reading.
	void Update(long long downloaded_bytes, long long total_bytes);

	// Shows "Avvio..." and closes the window. Idempotent: calling it twice
	// (explicitly, then again from the destructor) is a no-op the second
	// time.
	void Finish();

private:
#if defined(_WIN32)
	// HWNDs, kept as void* so this header never has to include
	// <windows.h> (same reasoning as every other header in this fork that
	// touches Windows types only inside its own .cpp).
	void* hwnd_ = nullptr;
	void* progress_bar_ = nullptr;
	void* label_ = nullptr;
#else
	int write_fd_ = -1;      // zenity's stdin, or -1 if not using zenity
	long pid_ = -1;          // pid_t, widened to avoid pulling <sys/types.h> into the header
	bool notify_send_begun_ = false; // fallback path: one notify-send at Begin, one at Finish
	std::string data_dir_;
#endif
};

}

#endif //FEDELEX_LAUNCHER_PROGRESS_H
