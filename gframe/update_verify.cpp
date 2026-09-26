#include "update_verify.h"

#include <cstring>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "update_keys.h"
// tweetnacl.h has no extern "C" guard of its own (it's a plain C library) —
// without this, this TU would ask the linker for a C++-mangled symbol while
// tweetnacl.c (compiled as C) provides an unmangled one. Same note as
// banlist_verify.cpp.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

namespace ygo::update {

namespace {

bool ReadRequiredString(const nlohmann::json& obj, const char* key, std::string& out) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return false;
	out = it->get<std::string>();
	return true;
}

bool IsAllZero(const uint8_t* key, size_t size) {
	for(size_t i = 0; i < size; ++i) {
		if(key[i] != 0)
			return false;
	}
	return true;
}

// 64 lowercase hex characters, nothing else — the manifest's sha256 field is
// what authorizes installing a file, so its shape is checked strictly rather
// than trusting whatever nlohmann/json happened to parse as a string.
bool IsLowercaseHex64(const std::string& s) {
	if(s.size() != 64)
		return false;
	for(char c : s) {
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	}
	return true;
}

}

bool AnyTrustedKeyConfigured() {
	for(const uint8_t* key : TRUSTED_KEYS) {
		if(!IsAllZero(key, ED25519_PUBLIC_KEY_SIZE))
			return true;
	}
	return false;
}

bool VerifySignatureWith(const std::string& document, const std::string& signature,
						 const uint8_t* const* keys, size_t key_count) {
	if(signature.size() != ED25519_SIGNATURE_SIZE)
		return false;
	// See the header comment: the signed message is the domain label, a
	// newline, and the raw document bytes — not the document alone.
	std::string message;
	message.reserve(std::strlen(UPDATE_DOMAIN) + 1 + document.size());
	message.append(UPDATE_DOMAIN);
	message.push_back('\n');
	message.append(document);

	// Same TweetNaCl combined-form trick as banlist_verify.cpp: a detached
	// signature is byte-identical to the 64 bytes crypto_sign_open expects
	// in front of the message.
	std::vector<unsigned char> signed_message(ED25519_SIGNATURE_SIZE + message.size());
	std::memcpy(signed_message.data(), signature.data(), ED25519_SIGNATURE_SIZE);
	if(!message.empty())
		std::memcpy(signed_message.data() + ED25519_SIGNATURE_SIZE, message.data(), message.size());
	std::vector<unsigned char> opened(signed_message.size());
	unsigned long long opened_len = 0;
	for(size_t i = 0; i < key_count; ++i) {
		if(IsAllZero(keys[i], ED25519_PUBLIC_KEY_SIZE))
			continue; // an unconfigured placeholder key must never "verify" anything
		if(crypto_sign_open(opened.data(), &opened_len, signed_message.data(),
							static_cast<unsigned long long>(signed_message.size()), keys[i]) == 0) {
			return true;
		}
	}
	return false;
}

bool VerifySignature(const std::string& document, const std::string& signature) {
	return VerifySignatureWith(document, signature, TRUSTED_KEYS,
							   sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus Parse(const std::string& document, Manifest& out, std::string& error) {
	// allow_exceptions=false: a malformed document comes back as a discarded
	// value instead of a throw, same reasoning as banlist_verify.cpp — a
	// throw out of a background thread's worker has nothing to catch it.
	nlohmann::json j = nlohmann::json::parse(document, nullptr, false);
	if(j.is_discarded() || !j.is_object()) {
		error = "not valid JSON";
		return VerifyStatus::MalformedJson;
	}
	try {
		Manifest manifest;

		auto version_it = j.find("version");
		if(version_it == j.end() || !version_it->is_number_integer()) {
			error = "version is missing or not a plain integer";
			return VerifyStatus::SchemaViolation;
		}
		manifest.version = version_it->get<int>();

		// min_supported is OPTIONAL (design/client-update.md §9): a manifest
		// that omits it is valid and closes nothing. If present it must be
		// a plain integer and must not exceed `version` — a manifest that
		// claims to require more than it itself publishes is a schema
		// violation, not a floor to enforce.
		auto min_supported_it = j.find("min_supported");
		if(min_supported_it != j.end()) {
			if(!min_supported_it->is_number_integer()) {
				error = "min_supported is present but not a plain integer";
				return VerifyStatus::SchemaViolation;
			}
			const int min_supported = min_supported_it->get<int>();
			if(min_supported > manifest.version) {
				error = "min_supported exceeds the manifest's own version";
				return VerifyStatus::SchemaViolation;
			}
			manifest.min_supported = min_supported;
		}

		auto files_it = j.find("files");
		if(files_it == j.end() || !files_it->is_array()) {
			error = "files is missing or not an array";
			return VerifyStatus::SchemaViolation;
		}

		std::unordered_set<std::string> seen_names;
		manifest.files.reserve(files_it->size());
		for(const auto& raw_file : *files_it) {
			if(!raw_file.is_object()) {
				error = "a files entry is not an object";
				return VerifyStatus::SchemaViolation;
			}
			ManifestFile file;

			if(!ReadRequiredString(raw_file, "name", file.name)) {
				error = "files[].name is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}
			if(file.name.empty()) {
				error = "files[].name is empty";
				return VerifyStatus::SchemaViolation;
			}
			if(!seen_names.insert(file.name).second) {
				// Same reasoning as banlist's duplicate-id check: a
				// duplicate name means the publisher's own build produced
				// something it should not have, not "the later one wins".
				error = "duplicate files[].name " + file.name;
				return VerifyStatus::SchemaViolation;
			}

			if(!ReadRequiredString(raw_file, "url", file.url)) {
				error = "files[].url is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}
			if(file.url.empty()) {
				error = "files[].url is empty";
				return VerifyStatus::SchemaViolation;
			}

			// sha256 is the ONLY thing that authorizes installing this file
			// (design/client-update.md, point 3). Missing or malformed is a
			// schema violation, never "skip the check for this one file".
			if(!ReadRequiredString(raw_file, "sha256", file.sha256)) {
				error = "files[].sha256 is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}
			if(!IsLowercaseHex64(file.sha256)) {
				error = "files[].sha256 for " + file.name + " is not 64 lowercase hex characters";
				return VerifyStatus::SchemaViolation;
			}

			// md5 stays optional: the upstream field the old, unsigned
			// manifest used to carry. Read it if present so client_updater
			// can keep passing it through for any code that still expects
			// the field to exist, but it is never itself a check — see the
			// header comment and design/client-update.md point 3.
			auto md5_it = raw_file.find("md5");
			if(md5_it != raw_file.end()) {
				if(!md5_it->is_string()) {
					error = "files[]." + file.name + ".md5 is present but not a string";
					return VerifyStatus::SchemaViolation;
				}
				file.md5 = md5_it->get<std::string>();
			}

			manifest.files.push_back(std::move(file));
		}

		out = std::move(manifest);
		return VerifyStatus::Ok;
	} catch(...) {
		error = "unexpected structure while reading the manifest";
		return VerifyStatus::SchemaViolation;
	}
}

VerifyStatus VerifyAndParse(const std::string& document, const std::string& signature,
							Manifest& out, std::string& error) {
	if(!VerifySignature(document, signature)) {
		error = "signature does not verify against any trusted key";
		return VerifyStatus::BadSignature;
	}
	return Parse(document, out, error);
}

VersionDecision CompareVersion(int incoming_version, int installed_version) {
	if(incoming_version > installed_version)
		return VersionDecision::Accept;
	if(incoming_version == installed_version)
		return VersionDecision::AlreadyCurrent;
	return VersionDecision::Rollback;
}

bool IsClientSupported(int min_supported, int client_version) {
	if(min_supported <= 0)
		return true; // no floor declared — nothing to enforce
	return client_version >= min_supported;
}

}
