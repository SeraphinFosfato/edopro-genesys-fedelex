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
	// FASE 75, design/decisioni.md D251, PHASES.md "il link del tavolo":
	// the fedelex:// table-link URI, forwarded verbatim by the launcher
	// (launcher/main.cpp) when it was itself invoked with one (the OS
	// handing it the URI via the registered x-scheme-handler/fedelex
	// association). Consumed exactly once, in gframe.cpp's CheckArguments()
	// -> ygo::DuelClient::JoinFromDeepLink(), which does the actual parsing
	// (gframe/deep_link.h) — this flag only carries the raw text across the
	// process boundary, same role FROM_LAUNCHER plays for the provenance
	// mark.
	DEEP_LINK,
	COUNT,
};


struct Option {
	bool enabled{ false };
	epro::path_stringview argument;
};

using args_t = std::array<Option, LAUNCH_PARAM::COUNT>;

extern args_t cli_args;

#endif //CLI_ARGS_H
