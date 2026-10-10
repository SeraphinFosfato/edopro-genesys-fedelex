// Tests for FASE 75c: gframe/single_instance_lock.h as the simulator
// (gframe/gframe.cpp, path "./" + kTournamentLockFileName after chdir to the
// `-C` directory) and the launcher (launcher/main.cpp, path data_dir + "/" +
// kTournamentLockFileName) use it. What is proven here: the two ways of
// spelling the path name the SAME file, so a lock held by one is seen by the
// other. What is NOT: that edopro_main() really takes it (that needs the
// simulator running, see design/launcher.md "FASE 75c").
//
// POSIX only: the Windows branch (CreateFileW, exclusive share mode) cannot
// run on the Linux CI runner that builds this workspace.

#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#include "single_instance_lock.h"
#endif

// Called from banlist_tests.cpp's main(), same pattern as every other
// *_tests.cpp in this directory.
int RunSingleInstanceLockTests();

#ifdef _WIN32
int RunSingleInstanceLockTests() {
	std::printf("single_instance_lock_tests: skipped on Windows\n");
	return 0;
}
#else
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

std::string MakeTempDir() {
	const char* base = std::getenv("TMPDIR");
	std::string tmpl = std::string(base && *base ? base : "/tmp") + "/sil_tests_XXXXXX";
	return mkdtemp(&tmpl[0]) ? tmpl : std::string();
}

std::string Cwd() {
	char buf[4096];
	return getcwd(buf, sizeof(buf)) ? buf : std::string();
}

// The test process shares its working directory with every other test in
// banlist_tests: always put it back.
struct CwdRestorer {
	std::string saved = Cwd();
	~CwdRestorer() {
		if(!saved.empty() && chdir(saved.c_str()) != 0)
			std::printf("  WARNING: could not restore the working directory\n");
	}
};

const std::string kName = ygo::kTournamentLockFileName;

void test_second_holder_is_refused_until_the_first_lets_go() {
	const auto dir = MakeTempDir();
	check(!dir.empty(), "sanity: a temporary directory must be creatable");
	const auto path = dir + "/" + kName;
	{
		ygo::SingleInstanceLock first(path);
		check(first.Acquired(), "the first holder of a free lock must get it");
		ygo::SingleInstanceLock second(path);
		check(!second.Acquired(), "a second holder must be refused while the first lives (the launcher's probe depends on this)");
	}
	ygo::SingleInstanceLock third(path);
	check(third.Acquired(), "once the holder is gone (exit, crash or kill: the OS releases flock) the lock is free again");
}

void test_simulator_spelling_and_launcher_spelling_are_the_same_file() {
	const auto dir = MakeTempDir();
	CwdRestorer restore;
	check(chdir(dir.c_str()) == 0, "sanity: chdir to the data directory (what the simulator's -C does)");
	// Simulator holds "./name" after the chdir; the launcher probes the
	// absolute data_dir + "/" + name.
	ygo::SingleInstanceLock simulator(std::string("./") + kName);
	check(simulator.Acquired(), "the simulator's own spelling must acquire a free lock");
	ygo::SingleInstanceLock probe(dir + "/" + kName);
	check(!probe.Acquired(), "the launcher's probe, spelled data_dir + \"/\" + name, must see the lock held through \"./\" + name");
}

void test_relative_data_dir_is_the_same_file_too() {
	const auto parent = MakeTempDir();
	CwdRestorer restore;
	check(chdir(parent.c_str()) == 0 && mkdir((parent + "/data").c_str(), 0755) == 0, "sanity: parent with a data subdirectory");
	// The launcher with a relative data_dir ("data") probes "data/name" from
	// its cwd and passes -C data: the simulator then chdirs into it.
	ygo::SingleInstanceLock probe_holder(std::string("data/") + kName);
	check(probe_holder.Acquired(), "sanity: the relative spelling acquires a free lock");
	check(chdir("data") == 0, "sanity: chdir into the relative data directory (the simulator's -C)");
	ygo::SingleInstanceLock simulator(std::string("./") + kName);
	check(!simulator.Acquired(), "with a relative data_dir, \"./\" + name after the chdir must still be the file the launcher spelled data/name");
}

void test_unwritable_directory_is_a_refusal_not_a_crash() {
	ygo::SingleInstanceLock nowhere("/nonexistent-dir-for-sil-tests/" + kName);
	check(!nowhere.Acquired(), "a lock that cannot be created must report not-acquired (edopro_main then logs and carries on)");
}

}

int RunSingleInstanceLockTests() {
	test_second_holder_is_refused_until_the_first_lets_go();
	test_simulator_spelling_and_launcher_spelling_are_the_same_file();
	test_relative_data_dir_is_the_same_file_too();
	test_unwritable_directory_is_a_refusal_not_a_crash();

	std::printf("single_instance_lock_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
#endif
