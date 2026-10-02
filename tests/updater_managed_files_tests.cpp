// Cancello 4 di FASE 60 (PHASES.md, D239, design/blocco-online.md §1): a
// test that proves ocgcore.dll is never renamed nor deleted by the
// updater's Unzip()/DeleteOld() step, on any platform. Unzip() itself needs
// irrlicht's archive filesystem (UnzipArchive) and real file I/O, so it has
// no standalone target here — same discipline as every other
// *_tests.cpp in this suite (see tests/premake5.lua's header comment). What
// IS pure, and what both functions were changed to actually call, is
// UpdaterManagedPaths() (gframe/updater_managed_files.h): the list of files
// the updater stashes-and-restores or deletes .old copies of. Testing that
// pure function is testing the real decision, not a reimplementation of it.

#include <cstdio>
#include <cstring>
#include "updater_managed_files.h"

// Called from banlist_tests.cpp's main(), same convention as every other
// *_tests.cpp file in this suite.
int RunUpdaterManagedFilesTests();

using namespace ygo;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* what) {
	++checks;
	if(!condition) {
		++failures;
		std::printf("  FAIL  %s\n", what);
	}
}

bool MentionsCore(const epro::path_string& p) {
	// Deliberately dumb substring search, not a path-aware comparison: the
	// property this guards is "no path shape, however written, slips an
	// ocgcore reference past this function", so the check must be at least
	// as blunt as a regression could be subtle.
#ifdef UNICODE
	return p.find(L"ocgcore") != epro::path_string::npos || p.find(L"ocg") != epro::path_string::npos;
#else
	return p.find("ocgcore") != epro::path_string::npos || p.find("ocg") != epro::path_string::npos;
#endif
}

void test_only_the_executable_is_managed() {
	// Several shapes of exe path, including ones that existed on Windows
	// before FASE 60 (static ygopro.exe) and after (renamed ygoprodll.exe),
	// and a Linux one for good measure. None of them should ever cause a
	// second, core-shaped entry to appear.
	const epro::path_string cases[] = {
		EPRO_TEXT("C:\\Games\\EDOPro\\ygopro.exe"),
		EPRO_TEXT("C:\\Games\\EDOPro\\ygoprodll.exe"),
		EPRO_TEXT("/home/user/edopro/ygoprodll"),
		EPRO_TEXT(""),
	};
	for(const auto& exe_path : cases) {
		auto managed = UpdaterManagedPaths(exe_path);
		check(managed.size() == 1, "UpdaterManagedPaths must return exactly one entry: the executable");
		if(managed.size() == 1)
			check(managed[0] == exe_path, "the single managed entry must be the executable path itself, unchanged");
		for(const auto& p : managed)
			check(!MentionsCore(p), "no managed path may mention the core file (ocgcore), on any platform");
	}
}

} // namespace

int RunUpdaterManagedFilesTests() {
	std::printf("-- updater_managed_files_tests --\n");
	test_only_the_executable_is_managed();
	std::printf("  %d/%d checks passed\n", checks - failures, checks);
	return failures;
}
