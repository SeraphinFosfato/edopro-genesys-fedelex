#include "cloud_decks.h"
#include <cstdio>

namespace ygo {

CloudDeckAction DecideCloudDeck(bool local_exists, long long local_mtime, long long cloud_updated_at, bool same_content) {
	if(!local_exists)
		return CloudDeckAction::Download;
	if(same_content)
		return CloudDeckAction::Skip;
	return cloud_updated_at > local_mtime ? CloudDeckAction::Download : CloudDeckAction::KeepLocal;
}

long long ParseCloudTimestamp(const std::string& iso) {
	int y, mo, d, h, mi, s;
	char tail = 0;
	if(std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &tail) != 7)
		return -1;
	if(mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60 || (tail != '.' && tail != 'Z'))
		return -1;
	// giorni dal 1970 (algoritmo civil-from-days di Howard Hinnant)
	y -= mo <= 2;
	const long long era = (y >= 0 ? y : y - 399) / 400;
	const long long yoe = y - era * 400;
	const long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long long days = era * 146097 + doe - 719468;
	return days * 86400 + h * 3600 + mi * 60 + s;
}

bool CloudDeckNameIsSafeFileName(const std::string& name) {
	if(name.empty() || name.size() > 64 || name[0] == '.' || name.back() == '.' || name.back() == ' ')
		return false;
	for(const unsigned char c : name)
		if(c < 0x20 || std::string("/\\:*?\"<>|").find(static_cast<char>(c)) != std::string::npos)
			return false;
	return true;
}

namespace {
const std::string kCloudPrefix = "[Cloud] ";
}

std::string CloudDeckFileName(const std::string& slot) {
	return kCloudPrefix + slot;
}

std::string CloudDeckSlotName(const std::string& deck_name) {
	if(deck_name.compare(0, kCloudPrefix.size(), kCloudPrefix) == 0)
		return deck_name.substr(kCloudPrefix.size());
	return deck_name;
}

}
