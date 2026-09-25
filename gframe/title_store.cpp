#include "title_store.h"

#include <iterator>
#include <mutex>
#include "compiler_features.h"
#include "file_stream.h"
#include "fmt.h"
#include "logging.h"
#include "title_verify.h"
#include "utils.h"

#if EDOPRO_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace ygo {

TitleStore* gTitleStore = nullptr;

namespace {

// Same helpers, same shapes, as banlist_updater.cpp's anonymous namespace —
// not shared across the two translation units for the same reason neither
// duplicates the other's payload struct: each stays self-contained rather
// than introducing a shared utility header for a handful of lines.
bool ReadFileToString(const epro::path_string& path, std::string& out) {
	FileStream in{ path, FileStream::in | FileStream::binary };
	if(in.fail())
		return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

epro::path_string JoinPath(epro::path_stringview folder, epro::path_stringview name) {
	return epro::format(EPRO_TEXT("{}{}"), folder, name);
}

}

void TitleStore::LoadFromDisk() {
	std::string user_ref;
	if(ReadFileToString(JoinPath(STORE_FOLDER, USER_REF_NAME), user_ref) && !user_ref.empty()) {
		std::lock_guard<epro::mutex> lock(mutex_);
		bound_user_ref_ = user_ref;
		has_bound_user_ref_ = true;
	}
	// Order doesn't matter between these two: both go through the same
	// D104 check against whatever user_ref.txt just established (or, on a
	// fresh install with no user_ref.txt yet, against nothing — the first
	// of the two to load wins the binding, same rule as a live check-in).
	std::string title_doc;
	if(ReadFileToString(JoinPath(STORE_FOLDER, TITLE_NAME), title_doc))
		ApplyVerifiedDocument(title_doc, /*persist=*/false);
	std::string block_doc;
	if(ReadFileToString(JoinPath(STORE_FOLDER, BLOCK_NAME), block_doc))
		ApplyVerifiedDocument(block_doc, /*persist=*/false);
}

bool TitleStore::ApplyResponse(const std::string& raw_document) {
	return ApplyVerifiedDocument(raw_document, /*persist=*/true);
}

bool TitleStore::ApplyVerifiedDocument(const std::string& raw_document, bool persist) {
	std::string error;
	switch(title::PeekType(raw_document)) {
	case title::MessageType::Title: {
		title::TitleFields fields;
		if(title::VerifyAndParseTitle(raw_document, fields, error) != title::VerifyStatus::Ok) {
			ErrorLog("Title check-in: title response rejected, ignored: {}", error);
			return false;
		}
		std::lock_guard<epro::mutex> lock(mutex_);
		if(!BindOrCheck(fields.user_ref))
			return false;
		title_.present = true;
		title_.user_ref = fields.user_ref;
		title_.issued_at = fields.issued_at;
		title_.expires_at = fields.expires_at;
		generation_.fetch_add(1, std::memory_order_relaxed);
		if(persist)
			AtomicWrite(JoinPath(STORE_FOLDER, TITLE_NAME), raw_document);
		return true;
	}
	case title::MessageType::Revocation: {
		title::RevocationFields fields;
		if(title::VerifyAndParseRevocation(raw_document, fields, error) != title::VerifyStatus::Ok) {
			ErrorLog("Title check-in: revocation response rejected, ignored: {}", error);
			return false;
		}
		std::lock_guard<epro::mutex> lock(mutex_);
		if(!BindOrCheck(fields.user_ref))
			return false;
		block_.kind = title::StoredBlock::Kind::Revocation;
		block_.user_ref = fields.user_ref;
		block_.at = fields.revoked_at;
		block_.banned_until = fields.banned_until;
		block_.has_banned_until = fields.has_banned_until;
		generation_.fetch_add(1, std::memory_order_relaxed);
		if(persist)
			AtomicWrite(JoinPath(STORE_FOLDER, BLOCK_NAME), raw_document);
		return true;
	}
	case title::MessageType::Suspension: {
		title::SuspensionFields fields;
		if(title::VerifyAndParseSuspension(raw_document, fields, error) != title::VerifyStatus::Ok) {
			ErrorLog("Title check-in: suspension response rejected, ignored: {}", error);
			return false;
		}
		std::lock_guard<epro::mutex> lock(mutex_);
		if(!BindOrCheck(fields.user_ref))
			return false;
		block_.kind = title::StoredBlock::Kind::Suspension;
		block_.user_ref = fields.user_ref;
		block_.at = fields.suspended_at;
		block_.banned_until.clear();
		block_.has_banned_until = false;
		generation_.fetch_add(1, std::memory_order_relaxed);
		if(persist)
			AtomicWrite(JoinPath(STORE_FOLDER, BLOCK_NAME), raw_document);
		return true;
	}
	case title::MessageType::Unknown:
	default:
		ErrorLog("Title check-in: response has an unrecognized or missing \"type\", ignored");
		return false;
	}
}

bool TitleStore::BindOrCheck(const std::string& user_ref) {
	// D104 decision itself lives in title_state.h (pure, tested standalone
	// in tests/title_tests.cpp) — this just acts on it: reject with a log
	// line, or persist a first-time binding. Caller already holds mutex_.
	if(!title::UserRefAccepted(has_bound_user_ref_, bound_user_ref_, user_ref)) {
		ErrorLog("Title check-in: user_ref '{}' does not match this client's bound '{}', message ignored (D104)",
				user_ref, bound_user_ref_);
		// FASE 27.2-ter: la riga sopra dice cosa e' stato rifiutato, questa
		// dice cosa succede di conseguenza. Senza, un titolo firmato bene
		// veniva scartato a ogni avvio con title.json ancora sul disco, e
		// l'accesso restava NoTitle — che e' escluso dalle notifiche: tre
		// silenzi in fila. Chi ha una lista e non la vede deve poter
		// leggere perche', e il file dove leggerlo e' questo.
		ErrorLog("Title check-in: the cached title is being discarded, the custom point list will not load."
				" Use Logout in the main menu to unbind this client, then log back in.");
		return false;
	}
	if(!has_bound_user_ref_) {
		bound_user_ref_ = user_ref;
		has_bound_user_ref_ = true;
		AtomicWrite(JoinPath(STORE_FOLDER, USER_REF_NAME), user_ref);
	}
	return true;
}

title::AccessState TitleStore::CurrentAccess(std::time_t now) const {
	std::lock_guard<epro::mutex> lock(mutex_);
	return title::ComputeAccess(title_, block_, now);
}

title::StoredTitle TitleStore::Title() const {
	std::lock_guard<epro::mutex> lock(mutex_);
	return title_;
}

title::StoredBlock TitleStore::Block() const {
	std::lock_guard<epro::mutex> lock(mutex_);
	return block_;
}

bool TitleStore::HasBoundUserRef() const {
	std::lock_guard<epro::mutex> lock(mutex_);
	return has_bound_user_ref_;
}

std::string TitleStore::BoundUserRef() const {
	std::lock_guard<epro::mutex> lock(mutex_);
	return bound_user_ref_;
}

void TitleStore::ClearForLogout() {
	{
		std::lock_guard<epro::mutex> lock(mutex_);
		title_ = title::StoredTitle{};
		block_ = title::StoredBlock{};
		has_bound_user_ref_ = false;
		bound_user_ref_.clear();
		generation_.fetch_add(1, std::memory_order_relaxed);
	}
	// Deleting is best-effort, same as the rest of this file's disk I/O: a
	// file that fails to delete (permissions, already gone) leaves stale
	// bytes on disk, but the in-memory state above is what every accessor
	// and LoadFromDisk's D104 check actually reasons about while the
	// process keeps running — a leftover file on disk cannot resurrect a
	// binding LoadFromDisk() will not read again until the next launch.
	Utils::FileDelete(JoinPath(STORE_FOLDER, TITLE_NAME));
	Utils::FileDelete(JoinPath(STORE_FOLDER, BLOCK_NAME));
	Utils::FileDelete(JoinPath(STORE_FOLDER, USER_REF_NAME));
}

bool TitleStore::AtomicWrite(epro::path_stringview path, const std::string& bytes) {
	const auto tmp_path = epro::format(EPRO_TEXT("{}.tmp"), path);
	if(!Utils::CreatePath(tmp_path))
		return false;
	{
		FileStream out{ tmp_path, FileStream::out | FileStream::binary | FileStream::trunc };
		if(out.fail())
			return false;
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if(out.fail())
			return false;
	}
	return AtomicReplace(tmp_path, path);
}

bool TitleStore::AtomicReplace(epro::path_stringview from, epro::path_stringview to) {
#if EDOPRO_WINDOWS
	return MoveFileEx(from.data(), to.data(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
	// NOT Utils::FileMove — that is a bare MoveFile on Windows and fails
	// outright when the destination exists, which is every call here after
	// the first (same note as BanlistUpdater::AtomicReplace).
	return std::rename(from.data(), to.data()) == 0;
#endif
}

}
