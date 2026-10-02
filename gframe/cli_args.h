#ifndef CLI_ARGS_H
#define CLI_ARGS_H

#include <array>
#include "text_types.h"

enum LAUNCH_PARAM {
	WORK_DIR,
	MUTE,
	CHANGELOG,
	DISCORD,
	OVERRIDE_UPDATE_URL,
	WANTS_TO_RUN_AS_ADMIN,
	REPOS_READ_ONLY,
	ONLY_CLONE_REPOS,
	USER_STORAGE_DIRECTORY,
	// FASE 64 / design/launcher.md §4, D244: the provenance mark the
	// launcher always passes to the simulator it spawns. Used ONLY by
	// ygo::launcher::SimulatorShouldRelaunchLauncher() in gframe.cpp's
	// edopro_main() to tell "started by the launcher" from "started
	// directly" (a shortcut, a double-click on bin/ygoprodll, a dev run) —
	// never read anywhere else.
	FROM_LAUNCHER,
	COUNT,
};


struct Option {
	bool enabled{ false };
	epro::path_stringview argument;
};

using args_t = std::array<Option, LAUNCH_PARAM::COUNT>;

extern args_t cli_args;

#endif //CLI_ARGS_H
