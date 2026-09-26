-- Standalone test workspace. Deliberately NOT included from the root
-- premake5.lua: that file is shared with upstream (edo9300/edopro) and every
-- line we add to it is a merge conflict waiting for the next rebase. A
-- separate workspace costs one extra command and nothing else.
--
--   premake5 --file=tests/premake5.lua gmake2
--   make -C tests/build config=release
--   ./bin/banlist_tests   -- targetdir is "../bin" relative to this script,
--                            i.e. <repo root>/bin, not tests/bin
--
-- What can be linked here is exactly what has no gframe dependency: the
-- verification module, the diff module, the vendored Ed25519, and the shared
-- hash fold. If a test ever needs irrlicht or curl to run, the thing it is
-- testing is in the wrong file.

workspace "banlist_tests"
	configurations { "Debug", "Release" }
	location "build"
	targetdir "../bin"

project "banlist_tests"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	warnings "Extra"
	-- ../ocgcore: only for network.h's transitive include of ocgapi.h
	-- (dllinterface.h -> ocgapi.h), pulled in solely for the FASE 34
	-- cancello-2 HostInfo layout guard (banlist_tests.cpp). Nothing here
	-- calls into ocgcore or links against it.
	includedirs { "../gframe", "../ocgcore" }
	files {
		"banlist_tests.cpp",
		"banlist_diff_tests.cpp",
		"title_tests.cpp",
		"update_tests.cpp",
		"../gframe/banlist_verify.cpp",
		"../gframe/banlist_diff.cpp",
		"../gframe/title_verify.cpp",
		"../gframe/title_state.cpp",
		"../gframe/update_verify.cpp",
		"../gframe/sha256.cpp",
		"../gframe/tweetnacl/*.c",
	}

	filter "action:not vs*"
		enablewarnings "pedantic"
	filter "configurations:Debug"
		symbols "On"
	filter "configurations:Release"
		optimize "On"
	filter {}
