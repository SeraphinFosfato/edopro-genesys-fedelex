#include "config.h"
#include <event2/thread.h>
#include <IrrlichtDevice.h>
#include <IGUIButton.h>
#include <IGUICheckBox.h>
#include <IGUIEditBox.h>
#include <IGUIWindow.h>
#include <IGUIEnvironment.h>
#include <ISceneManager.h>
#include <optional>
#include "client_updater.h"
#include "cli_args.h"
#include "config.h"
#include "data_handler.h"
#include "logging.h"
#include "game.h"
#include "log.h"
#include "joystick_wrapper.h"
#include "utils_gui.h"
#include "fmt.h"
#include "curl.h"
#include "launcher_logic.h"
#if EDOPRO_MACOS
#include "osx_menu.h"
#endif
#if EDOPRO_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif EDOPRO_LINUX
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

bool is_from_discord = false;
bool open_file = false;
epro::path_string open_file_name = EPRO_TEXT("");
bool show_changelog = false;
ygo::Game* ygo::mainGame = nullptr;
ygo::ImageDownloader* ygo::gImageDownloader = nullptr;
ygo::DataManager* ygo::gDataManager = nullptr;
ygo::SoundManager* ygo::gSoundManager = nullptr;
ygo::GameConfig* ygo::gGameConfig = nullptr;
ygo::RepoManager* ygo::gRepoManager = nullptr;
ygo::DeckManager* ygo::gdeckManager = nullptr;
ygo::ClientUpdater* ygo::gClientUpdater = nullptr;
JWrapper* gJWrapper = nullptr;

namespace {
void CheckArguments(const args_t& args) {
	if(args[LAUNCH_PARAM::MUTE].enabled) {
		ygo::GUIUtils::SetCheckbox(ygo::mainGame->device, ygo::mainGame->tabSettings.chkEnableSound, false);
		ygo::GUIUtils::SetCheckbox(ygo::mainGame->device, ygo::mainGame->tabSettings.chkEnableMusic, false);
	}
}

// FASE 64 cancello 5 / design/launcher.md §4, D244: if this binary was NOT
// started by the launcher (no -from-launcher mark — a shortcut built before
// the launcher existed, a double-click on bin/ygoprodll, a dev run), relaunch
// the launcher instead of running unprotected. ygo::launcher::
// SimulatorShouldRelaunchLauncher() is the pure policy (already covered by
// tests/launcher_logic_tests.cpp); everything here is just the I/O around it.
//
// If no launcher binary is found next to the program directory (this is not
// a real install — a bare build, a CI artifact run directly, a developer's
// build/bin), this logs and returns false: running unprotected is strictly
// better than refusing to start at all (design/launcher.md §4, "deve
// comunque funzionare").
inline bool RelaunchLauncherIfNeeded(const args_t& args) {
	if(!ygo::launcher::SimulatorShouldRelaunchLauncher(args[LAUNCH_PARAM::FROM_LAUNCHER].enabled))
		return false;
#if EDOPRO_WINDOWS || EDOPRO_LINUX
	// The simulator lives at PROGRAM_DIR/bin/<exe> (design/launcher.md §4) —
	// its own exe folder's PARENT is PROGRAM_DIR, where the launcher sits.
	// GetExeFolder() is PROGRAM_DIR/bin (design/launcher.md §4); GetFilePath()
	// strips one more path component, same helper GetExeFolder() itself is
	// built from (GetFilePath(GetExePath())) — see utils.cpp.
	// GetExeFolder() ends with '/', and GetFilePath() only strips what comes
	// after the last '/': without dropping it first the "parent" was bin/
	// itself, and the launcher was looked for in bin/ (seen in error.log).
	auto exe_folder = ygo::Utils::GetExeFolder();
	while(!exe_folder.empty() && (exe_folder.back() == EPRO_TEXT('/') || exe_folder.back() == EPRO_TEXT('\\')))
		exe_folder.pop_back();
	auto program_dir = ygo::Utils::GetFilePath(exe_folder);
#if EDOPRO_WINDOWS
	auto launcher_path = epro::format(EPRO_TEXT("{}/ygopro.exe"), program_dir);
#else
	auto launcher_path = epro::format(EPRO_TEXT("{}/fedelex-launcher"), program_dir);
#endif
	if(!ygo::Utils::FileExists(launcher_path)) {
		ygo::ErrorLog("Avviato senza launcher e nessun launcher trovato in {}: proseguo senza (nessuna installazione reale).",
					 ygo::Utils::ToUTF8IfNeeded(launcher_path));
		return false;
	}
#if EDOPRO_WINDOWS
	STARTUPINFO si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	epro::path_string command = epro::format(EPRO_TEXT("\"{}\""), launcher_path);
	if(!CreateProcess(launcher_path.data(), &command[0], nullptr, nullptr, false, 0, nullptr, nullptr, &si, &pi)) {
		ygo::ErrorLog("Impossibile rilanciare il launcher ({}).", ygo::Utils::ToUTF8IfNeeded(launcher_path));
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
#else
	const auto* launcher_cstr = launcher_path.data();
	auto pid = vfork();
	if(pid == 0) {
		execl(launcher_cstr, launcher_cstr, nullptr);
		_exit(EXIT_FAILURE); // execl only returns on failure
	}
	if(pid < 0) {
		ygo::ErrorLog("Impossibile rilanciare il launcher ({}).", ygo::Utils::ToUTF8IfNeeded(launcher_path));
		return false;
	}
#endif
	return true;
#else
	// No launcher on this platform yet (iOS/Android/macOS) — nothing to
	// relaunch into.
	return false;
#endif
}

inline void ThreadsStartup() {
#if EDOPRO_WINDOWS
	const WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	auto wsaret = WSAStartup(wVersionRequested, &wsaData);
	if(wsaret != 0)
		throw std::runtime_error(epro::format("Failed to initialize WinSock ({})!", wsaret));
	if(evthread_use_windows_threads() < 0)
		throw std::runtime_error("Failed initialize libevent!");
#else
	if(evthread_use_pthreads() < 0)
		throw std::runtime_error("Failed initialize libevent!");
#endif
	auto res = curl_global_init(CURL_GLOBAL_SSL);
	if(res != CURLE_OK)
		throw std::runtime_error(epro::format("Curl error: ({}) {}", res, curl_easy_strerror(res)));
}
struct ThreadsCleaner {
	~ThreadsCleaner() {
		curl_global_cleanup();
		libevent_global_shutdown();
#if EDOPRO_WINDOWS
		WSACleanup();
#endif
	}
};

//clang below version 11 (llvm version 8) has a bug with brace class initialization
//where it can't properly deduce the destructors of its members
//https://reviews.llvm.org/D45898
//https://bugs.llvm.org/show_bug.cgi?id=28280
//add a workaround to construct the game object indirectly
//to avoid constructing it with brace initialization
#if defined(__clang_major__) && __clang_major__ <= 10
class Game {
	ygo::Game game;
public:
	Game() :game() {};
	ygo::Game* operator&() { return &game; }
};
#else
using Game = ygo::Game;
#endif

}

#if EDOPRO_WINDOWS
#define ADMIN_STR "administrator"
#else
#define ADMIN_STR "root"
#endif

int edopro_main(const args_t& args) {
	std::puts(EDOPRO_VERSION_STRING_DEBUG);
	// FASE 64 cancello 5: before anything else — this is the one-time
	// hand-off, never a loop (RelaunchLauncherIfNeeded only fires when
	// -from-launcher is ABSENT, and the launcher it spawns always passes
	// that mark to whatever it in turn execs — design/launcher.md §4,
	// ygo::launcher::SimulatorShouldRelaunchLauncher).
	if(RelaunchLauncherIfNeeded(args))
		return EXIT_SUCCESS;
	if(ygo::Utils::IsRunningAsAdmin() && !args[LAUNCH_PARAM::WANTS_TO_RUN_AS_ADMIN].enabled) {
		constexpr auto err = "Attempted to run the game as " ADMIN_STR ".\n"
			"You should NEVER have to run the game with elevated priviledges.\n"
			"If for some reason you REALLY want to do that, launch the game with the option \"-i-want-to-be-admin\""sv;
		epro::print("{}\n", err);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", err);
		return EXIT_FAILURE;
	}
	{
		const auto& workdir = args[LAUNCH_PARAM::WORK_DIR];
		const epro::path_stringview dest = workdir.enabled ? workdir.argument : ygo::Utils::GetExeFolder();
		if(!ygo::Utils::SetWorkingDirectory(dest)) {
			const auto err = epro::format("failed to change directory to: {} ({})",
										 ygo::Utils::ToUTF8IfNeeded(dest), ygo::Utils::GetLastErrorString());
			ygo::ErrorLog(err);
			epro::print("{}\n", err);
			ygo::GUIUtils::ShowErrorWindow("Initialization fail", err);
			return EXIT_FAILURE;
		}
	}
	{
		const auto& userdir = args[LAUNCH_PARAM::USER_STORAGE_DIRECTORY];
		const epro::path_stringview dir = userdir.enabled ? userdir.argument : EPRO_TEXT("./");
		ygo::Utils::SetUserStorageDirectory(dir);
	}
	ygo::Utils::SetupCrashDumpLogging();
	std::optional<ThreadsCleaner> cleaner;
	try {
		ThreadsStartup();
		cleaner.emplace();
	} catch(const std::exception& e) {
		epro::stringview text(e.what());
		ygo::ErrorLog(text);
		epro::print("{}\n", text);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", text);
		return EXIT_FAILURE;
	}
	show_changelog = args[LAUNCH_PARAM::CHANGELOG].enabled;
	ygo::ClientUpdater updater(args[LAUNCH_PARAM::OVERRIDE_UPDATE_URL].argument);
	ygo::gClientUpdater = &updater;
	std::unique_ptr<ygo::DataHandler> data{ nullptr };
	try {
		data = std::make_unique<ygo::DataHandler>();
		ygo::gImageDownloader = data->imageDownloader.get();
		ygo::gDataManager = data->dataManager.get();
		ygo::gSoundManager = data->sounds.get();
		ygo::gGameConfig = data->configs.get();
		ygo::gRepoManager = data->gitManager.get();
		ygo::gdeckManager = data->deckManager.get();
	}
	catch(const std::exception& e) {
		epro::stringview text(e.what());
		ygo::ErrorLog(text);
		epro::print("{}\n", text);
		ygo::GUIUtils::ShowErrorWindow("Initialization fail", text);
		return EXIT_FAILURE;
	}
	if (!data->configs->noClientUpdates)
		updater.CheckUpdates();
#if EDOPRO_WINDOWS
	if(!data->configs->showConsole) {
		FILE* fDummy;
		freopen_s(&fDummy, "NUL", "r", stdin);
		freopen_s(&fDummy, "NUL", "w", stderr);
		freopen_s(&fDummy, "NUL", "w", stdout);
		FreeConsole();
	}
#endif
#if EDOPRO_MACOS
	EDOPRO_SetupMenuBar([]() {
		ygo::gGameConfig->fullscreen = !ygo::gGameConfig->fullscreen;
		ygo::mainGame->gSettings.chkFullscreen->setChecked(ygo::gGameConfig->fullscreen);
	});
#endif
	srand(static_cast<uint32_t>(time(nullptr)));
	std::unique_ptr<JWrapper> joystick{ nullptr };
	bool firstlaunch = true;
	bool reset = false;
	do {
		Game _game{};
		ygo::mainGame = &_game;
		std::swap(data->tmp_device, ygo::mainGame->device);
		try {
			ygo::mainGame->Initialize();
		}
		catch(const std::exception& e) {
			epro::stringview text(e.what());
			ygo::ErrorLog(text);
			epro::print("{}\n", text);
			ygo::GUIUtils::ShowErrorWindow("Assets load fail", text);
			return EXIT_FAILURE;
		}
		if(firstlaunch) {
			joystick = std::make_unique<JWrapper>(ygo::mainGame->device.get());
			gJWrapper = joystick.get();
			firstlaunch = false;
			CheckArguments(args);
		}
		reset = ygo::mainGame->MainLoop();
		std::swap(data->tmp_device, ygo::mainGame->device);
		if(reset) {
			auto device = data->tmp_device;
			device->setEventReceiver(nullptr);
			auto driver = device->getVideoDriver();
			/*the gles drivers have an additional cache, that isn't cleared when the textures are removed,
			since it's not a big deal clearing them, as they'll be reused, they aren't cleared*/
			/*driver->removeAllTextures();*/
			driver->removeAllHardwareBuffers();
			driver->removeAllOcclusionQueries();
			device->getSceneManager()->clear();
			auto env = device->getGUIEnvironment();
			env->clear();
		}
	} while(reset);
	return EXIT_SUCCESS;
}
