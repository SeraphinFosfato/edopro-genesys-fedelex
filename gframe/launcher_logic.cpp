#include "launcher_logic.h"

#include <cstdio>

namespace ygo::launcher {

namespace {

// Decimal MB (1,000,000 bytes), one decimal digit, Italian comma — matches
// how every other MB figure in this UI is written (design/decisioni.md
// D247 point 2's own example, "12,3 / 66,1 MB"). Nothing downstream parses
// this back into a number (it only ever reaches a dialog's text), so the
// unit base is a display choice, not a wire format — MiB would be just as
// defensible; decimal MB was picked because it is what du/release notes in
// this repo already use elsewhere (bundle_linux.sh's `du -h`).
std::string FormatMegabytes(long long bytes) {
	if(bytes < 0)
		bytes = 0;
	double mb = static_cast<double>(bytes) / 1000000.0;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1f", mb);
	std::string out(buf);
	for(char& c : out) {
		if(c == '.')
			c = ',';
	}
	return out;
}

}

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

std::string FormatDownloadProgress(long long downloaded_bytes, long long total_bytes) {
	if(downloaded_bytes < 0)
		downloaded_bytes = 0;
	if(total_bytes <= 0)
		return FormatMegabytes(downloaded_bytes) + " MB";
	if(downloaded_bytes > total_bytes)
		downloaded_bytes = total_bytes;
	return FormatMegabytes(downloaded_bytes) + " / " + FormatMegabytes(total_bytes) + " MB";
}

int DownloadProgressPercent(long long downloaded_bytes, long long total_bytes) {
	if(total_bytes <= 0)
		return 0;
	if(downloaded_bytes < 0)
		downloaded_bytes = 0;
	if(downloaded_bytes > total_bytes)
		downloaded_bytes = total_bytes;
	double pct = (static_cast<double>(downloaded_bytes) / static_cast<double>(total_bytes)) * 100.0;
	int rounded = static_cast<int>(pct + 0.5);
	if(rounded < 0)
		rounded = 0;
	if(rounded > 100)
		rounded = 100;
	return rounded;
}

}
