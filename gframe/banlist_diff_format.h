#ifndef BANLIST_DIFF_FORMAT_H
#define BANLIST_DIFF_FORMAT_H

// Turns a banlist::Diff into display text. banlist_diff.h decides WHAT
// changed and in what order; this decides how it reads on screen. Split out
// of game.cpp/deck_con.cpp because both need it: the "staging is ready"
// notification (game.cpp) and the "view banlist changes" menu entry
// (deck_con.cpp). design/banlist-distribution.md, "Il diff" and "La
// notifica".

#include <string>
#include "banlist_diff.h"

namespace ygo {

// Full, four-groups-in-order text for the "view banlist changes" menu
// entry — stays consultable after the notification popup is closed.
std::wstring FormatBanlistDiff(const banlist::Diff& diff);

// Short summary for the "staging is ready" notification (wQuery).
std::wstring FormatBanlistNotification(const banlist::Diff& diff, int staged_version);

}

#endif //BANLIST_DIFF_FORMAT_H
