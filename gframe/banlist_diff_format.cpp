#include "banlist_diff_format.h"

#include "fmt.h"
#include "utils.h"

namespace ygo {

namespace {

// design/banlist-distribution.md, "Il diff": "Per ogni voce: nome, vecchio
// → nuovo, e la riga di reason se il JSON la porta."
std::wstring FormatEntry(const banlist::DiffEntry& entry) {
	std::wstring line = L"  " + Utils::ToUnicodeIfNeeded(entry.name) + L": ";
	if(entry.has_old && entry.has_new) {
		line += epro::format(L"{} → {} pt", entry.old_points, entry.new_points);
		if(entry.old_limit != entry.new_limit)
			line += epro::format(L" ({} → {} copie)", entry.old_limit, entry.new_limit);
	} else if(entry.has_new) {
		line += epro::format(L"nuova, {} pt, max {} copie", entry.new_points, entry.new_limit);
	} else {
		line += epro::format(L"uscita dalla lista (era {} pt)", entry.old_points);
	}
	// Reason only ever comes from the staged side (banlist_diff.cpp) — an
	// absent one here means the JSON genuinely had none, never a gap this
	// code fills in.
	if(entry.has_reason)
		line += L"\n    " + Utils::ToUnicodeIfNeeded(entry.reason);
	return line;
}

void AppendGroup(std::wstring& out, const wchar_t* title, const banlist::DiffGroup& group) {
	if(group.entries.empty())
		return;
	if(!out.empty())
		out += L"\n\n";
	out += title;
	out += L"\n";
	for(const auto& entry : group.entries) {
		out += FormatEntry(entry);
		out += L"\n";
	}
	if(group.omitted > 0)
		out += epro::format(L"  ...e altre {}\n", group.omitted);
}

}

std::wstring FormatBanlistDiff(const banlist::Diff& diff) {
	std::wstring out;
	AppendGroup(out, L"Più care", diff.more_expensive);
	AppendGroup(out, L"Più economiche", diff.cheaper);
	AppendGroup(out, L"Nuove in lista", diff.new_entries);
	AppendGroup(out, L"Uscite dalla lista", diff.removed_entries);
	if(out.empty())
		out = L"Nessuna modifica in sospeso.";
	return out;
}

std::wstring FormatBanlistNotification(const banlist::Diff& diff, int staged_version) {
	return epro::format(
		L"La point list è stata aggiornata alla versione {}.\n"
		L"{} più care, {} più economiche, {} nuove, {} uscite dalla lista.\n"
		L"Si applica al prossimo avvio. Riavviare ora?",
		staged_version,
		diff.more_expensive.entries.size() + diff.more_expensive.omitted,
		diff.cheaper.entries.size() + diff.cheaper.omitted,
		diff.new_entries.entries.size() + diff.new_entries.omitted,
		diff.removed_entries.entries.size() + diff.removed_entries.omitted);
}

}
