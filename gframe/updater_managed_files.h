#ifndef UPDATER_MANAGED_FILES_H
#define UPDATER_MANAGED_FILES_H

#include <vector>
#include "text_types.h"

namespace ygo {

// FASE 60 (D239, design/blocco-online.md §1): the only file ClientUpdater's
// unzip/cleanup step is allowed to stash-and-restore (Unzip, around a
// scompattamento) or delete-on-startup (DeleteOld) is the executable our own
// update manifest ships. Before FASE 60, the Windows branch of those two
// functions also stashed/restored/deleted Utils::GetCorePath()
// (ocgcore.dll), because the core was linked into ygopro.exe and the
// updater's own zip format assumed it might have to ship one. It never
// ships one now, on any platform: the core comes from the Project Ignis
// repository (RepoManager::LoadCoreFromRepos), "un solo scrittore per
// file" (D239).
//
// Pulled out as a pure function — no irrlicht, no real filesystem, no
// #ifdef on EDOPRO_WINDOWS/EDOPRO_LINUX — so the invariant "the updater
// never manages the core file" can be asserted by a standalone test
// (tests/updater_managed_files_tests.cpp) for every path shape, not only
// read off which #if branches happen to remain in client_updater.cpp.
inline std::vector<epro::path_string> UpdaterManagedPaths(const epro::path_string& exe_path) {
	return { exe_path };
}

} // namespace ygo

#endif // UPDATER_MANAGED_FILES_H
