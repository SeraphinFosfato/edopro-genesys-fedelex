#include "launcher_logic.h"

namespace ygo::launcher {

bool SimulatorShouldRelaunchLauncher(bool has_provenance_mark) {
	return !has_provenance_mark;
}

ManifestAcceptance DecideManifestAcceptance(int incoming_version, int stored_version) {
	if(incoming_version > stored_version)
		return ManifestAcceptance::Accept;
	if(incoming_version == stored_version)
		return ManifestAcceptance::AlreadyCurrent;
	return ManifestAcceptance::RejectRollback;
}

ReplaceDecision DecideReplace(const std::string& installed_sha256, const std::string& manifest_sha256) {
	if(installed_sha256.empty() || installed_sha256 != manifest_sha256)
		return ReplaceDecision::Replace;
	return ReplaceDecision::Keep;
}

SwapOutcome DecideSwap(bool downloaded_sha256_matches,
						bool executable_bit_set_after_chmod,
						bool new_binary_launches) {
	if(!downloaded_sha256_matches)
		return SwapOutcome::RollbackToOld;
	if(!executable_bit_set_after_chmod)
		return SwapOutcome::FixExecutableBit;
	if(!new_binary_launches)
		return SwapOutcome::RollbackToOld;
	return SwapOutcome::InstallNew;
}

}
