#ifndef LAUNCHER_LOGIC_H
#define LAUNCHER_LOGIC_H

// Pure decision functions for the launcher (FASE 64, design/launcher.md,
// D244). Same discipline as update_verify.h: no gframe dependency, no I/O,
// no network, no curl, no irrlicht — every function here takes the facts a
// real run would have gathered elsewhere (a hash already computed, a stat()
// already done, a file already read) and returns a decision, so the risky
// part of this component (cancello 2 of launcher.md: "il ciclo
// launcher->simulatore->launcher e' impossibile per test") is testable
// without ever spawning a process or touching a disk.
//
// This header intentionally says nothing about HOW a hash is computed, a
// file is replaced, or a process is spawned — that is launcher/*.cpp's job,
// and it is deliberately NOT part of this standalone-testable module (see
// tests/premake5.lua's own rule: "se un test ha bisogno di irrlicht o curl
// per girare, la cosa che sta testando e' nel file sbagliato").

#include <string>

namespace ygo::launcher {

// --- Cancello 2 / §4 of launcher.md: the cycle guard -----------------------
//
// The launcher always starts the simulator with a provenance mark (an
// argument or an environment variable — launcher/*.cpp's choice, out of
// scope here). If the simulator is started WITHOUT that mark (a player
// double-clicking bin/ygoprodll directly, or a shortcut that bypassed the
// launcher), it relaunches the launcher exactly once instead of running
// unprotected — never a second time, because the launcher it relaunches
// into will in turn pass the mark to the simulator IT spawns. This function
// is the one-line policy both ends have to not reinvent: "do I have the
// mark, yes or no". The impossibility of a cycle is a property of this
// function always being called with the mark already set after a launcher
// handoff — it is not something the function itself needs to track state
// for.
bool SimulatorShouldRelaunchLauncher(bool has_provenance_mark);

// --- Cancello 2: anti-rollback for the launcher's OWN state -----------------
//
// D244 point 4: the launcher keeps the last manifest `version` it accepted
// (launcher-state.json, data directory) and refuses a manifest whose
// version is lower than that — same rule, same shape as
// ygo::update::CompareVersion, reused here under the launcher's own name so
// callers in launcher/*.cpp do not have to reach into the update_verify
// namespace to express "is this manifest newer than the one I already
// accepted". `stored_version` is 0 for "no state file yet" (first run),
// which is never higher than a real manifest's version (>= 1 in practice),
// so a first run always accepts.
enum class ManifestAcceptance {
	Accept,        // strictly newer than what launcher-state.json records — proceed
	AlreadyCurrent,// identical — nothing to do, not an error
	RejectRollback // lower — refuse, log, do not touch launcher-state.json
};

ManifestAcceptance DecideManifestAcceptance(int incoming_version, int stored_version);

// --- Cancello 2: replace or keep a single managed file ----------------------
//
// D244 point 4: "cosa installare lo dice il file, non un numero" — the
// launcher compares the SHA-256 of what is already on disk against the
// manifest's, and replaces only on a mismatch. `installed_sha256` is empty
// when the file does not exist yet (first install) or could not be hashed
// (missing read permission, etc.) — both cases must replace, same as a
// mismatch, never "keep" on a question mark.
enum class ReplaceDecision {
	Keep,   // on-disk file already matches the manifest — do nothing
	Replace // missing, unreadable, or hash mismatch — download and swap
};

ReplaceDecision DecideReplace(const std::string& installed_sha256, const std::string& manifest_sha256);

// --- Cancello 2 / cancello 4 of PHASES.md FASE 64: verified swap -----------
//
// D244 point 5: download to bin/<name>.new, verify its SHA-256, set the
// executable bit, re-stat it, move the old one aside to bin/<name>.old (one
// copy only), rename the new one into place. This function is the decision
// at EACH checkpoint of that sequence — never the I/O itself. Every input is
// a fact the caller already measured:
//   - downloaded_sha256_matches: the freshly downloaded bytes hash to what
//     the manifest promised (false => the download is corrupt or was
//     tampered with in flight — never install it, no amount of retrying the
//     executable-bit step helps).
//   - executable_bit_set_after_chmod: re-stat() after chmod() confirmed the
//     bit stuck (false on at least one real filesystem class — see
//     launcher.md cancello 4, "senza bit lo rimette e lo dice" — a caller
//     that gets false here tries ONE corrective chmod and calls this
//     function again with the corrected value, it does not loop silently).
//   - new_binary_launches: the replacement was actually exec()'d once (or,
//     on Windows, started and did not immediately exit with a load error)
//     and came up — false means "exists, executable, wrong hash never
//     happened, and it still doesn't run", the case the `.old` rollback
//     exists for.
enum class SwapOutcome {
	InstallNew,       // every check passed — the new file is live, keep the .old backup per D244.5
	FixExecutableBit, // hash is right, bit did not stick — caller chmod()s again before re-asking
	RollbackToOld,    // either the download didn't verify, or the verified new file won't launch — restore .old and say so
};

SwapOutcome DecideSwap(bool downloaded_sha256_matches,
						bool executable_bit_set_after_chmod,
						bool new_binary_launches);

}

#endif //LAUNCHER_LOGIC_H
