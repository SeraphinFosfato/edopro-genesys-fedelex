#include "banlist_verify.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "banlist_keys.h"
// tweetnacl.h has no extern "C" guard of its own (it's a plain C library) —
// without this, this TU would ask the linker for a C++-mangled symbol while
// tweetnacl.c (compiled as C) provides an unmangled one.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

namespace ygo::banlist {

namespace {

// The ten closed values of D60. A value outside this set is a schema
// violation, not a value to guess at — the enumeration is closed on the
// producing side (design/banlist-distribution.md), so a new value here
// means the artifact and this client disagree about what the format
// contains.
const std::unordered_set<std::string>& ValidMacros() {
	static const std::unordered_set<std::string> values = {
		"Magia", "Trappola", "Mostro Effetto", "Mostro Normale", "Pendulum",
		"Ritual", "Synchro", "XYZ", "Link", "Fusion",
	};
	return values;
}

bool ReadRequiredString(const nlohmann::json& obj, const char* key, std::string& out) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return false;
	out = it->get<std::string>();
	return true;
}

}

bool VerifySignatureWith(const std::string& document, const std::string& signature,
						 const uint8_t* const* keys, size_t key_count) {
	if(signature.size() != ED25519_SIGNATURE_SIZE)
		return false;
	// TweetNaCl only exposes the combined form crypto_sign/crypto_sign_open
	// work on (signature bytes followed by the message). A detached Ed25519
	// signature is byte-identical to those same 64 bytes, so verifying it is
	// just handing crypto_sign_open the concatenation and checking it
	// accepts — no format conversion involved.
	std::vector<unsigned char> signed_message(ED25519_SIGNATURE_SIZE + document.size());
	std::memcpy(signed_message.data(), signature.data(), ED25519_SIGNATURE_SIZE);
	if(!document.empty())
		std::memcpy(signed_message.data() + ED25519_SIGNATURE_SIZE, document.data(), document.size());
	std::vector<unsigned char> opened(signed_message.size());
	unsigned long long opened_len = 0;
	for(size_t i = 0; i < key_count; ++i) {
		if(crypto_sign_open(opened.data(), &opened_len, signed_message.data(),
							static_cast<unsigned long long>(signed_message.size()), keys[i]) == 0) {
			// Accept on the FIRST trusted key that verifies, not only the
			// first entry of the array: this is what makes rotating from the
			// operational key to the reserve key a release of the list
			// instead of a reinstall for every player.
			return true;
		}
	}
	return false;
}

bool VerifySignature(const std::string& document, const std::string& signature) {
	return VerifySignatureWith(document, signature, TRUSTED_KEYS,
							   sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus Parse(const std::string& document, Payload& out, std::string& error) {
	// allow_exceptions=false: a malformed document comes back as a discarded
	// value instead of a throw. A throw out of a background thread's worker
	// function has nothing to catch it.
	nlohmann::json j = nlohmann::json::parse(document, nullptr, false);
	if(j.is_discarded() || !j.is_object()) {
		error = "not valid JSON";
		return VerifyStatus::MalformedJson;
	}
	// From here on every access is find()+is_*()-guarded before any get<>(),
	// so nothing here should throw — the try/catch is a second net, not the
	// mechanism, in case a future edit adds an unguarded access.
	try {
		Payload payload;

		auto fv_it = j.find("format_version");
		if(fv_it == j.end() || !fv_it->is_number_integer()) {
			// format_version is a plain monotonic integer (D62 removed the
			// MAJOR.MINOR comparison that used to justify anything else). Not
			// a string, not a float, not semver: a non-integer here is a
			// malformed payload, refused exactly like a bad signature.
			error = "format_version is missing or not a plain integer";
			return VerifyStatus::SchemaViolation;
		}
		payload.format_version = fv_it->get<int>();

		if(!ReadRequiredString(j, "generated_at", payload.generated_at)) {
			error = "generated_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "expires_at", payload.expires_at)) {
			error = "expires_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "license", payload.license)) {
			error = "license is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}

		// attribution.{author,url} on the wire, flattened onto Payload's
		// author/url — a mapping choice of this function, not a change to
		// what the artifact contract emits (design/vault-banlist/
		// vault-pipeline.md §10.2).
		auto attribution_it = j.find("attribution");
		if(attribution_it == j.end() || !attribution_it->is_object()) {
			error = "attribution is missing or not an object";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(*attribution_it, "author", payload.author)) {
			error = "attribution.author is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(*attribution_it, "url", payload.url)) {
			error = "attribution.url is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}

		auto entries_it = j.find("entries");
		if(entries_it == j.end() || !entries_it->is_array()) {
			error = "entries is missing or not an array";
			return VerifyStatus::SchemaViolation;
		}

		std::unordered_set<uint32_t> seen_ids;
		payload.entries.reserve(entries_it->size());
		for(const auto& raw_entry : *entries_it) {
			if(!raw_entry.is_object()) {
				error = "an entry is not an object";
				return VerifyStatus::SchemaViolation;
			}
			Entry entry;

			auto id_it = raw_entry.find("id");
			if(id_it == raw_entry.end() || !id_it->is_number_integer()) {
				error = "entry.id is missing or not an integer";
				return VerifyStatus::SchemaViolation;
			}
			const auto raw_id = id_it->get<int64_t>();
			if(raw_id < 0) {
				error = "entry.id is negative";
				return VerifyStatus::SchemaViolation;
			}
			entry.id = static_cast<uint32_t>(raw_id);
			if(!seen_ids.insert(entry.id).second) {
				// A duplicate id is not "the later one wins": it means the
				// publisher's own build produced something it shouldn't
				// have, and guessing which entry is the real one is exactly
				// the kind of silent disagreement this whole module exists
				// to avoid.
				error = "duplicate id " + std::to_string(entry.id);
				return VerifyStatus::SchemaViolation;
			}

			// limit is always present in the artifact and never defaulted
			// client-side (the artifact contract, rule 1): a missing field
			// here is a schema violation, not "assume 3".
			auto limit_it = raw_entry.find("limit");
			if(limit_it == raw_entry.end() || !limit_it->is_number_integer()) {
				error = "entry.limit is missing or not an integer";
				return VerifyStatus::SchemaViolation;
			}
			entry.limit = limit_it->get<int>();
			if(entry.limit < 0 || entry.limit > 3) {
				error = "entry.limit out of range [0,3]";
				return VerifyStatus::SchemaViolation;
			}

			auto points_it = raw_entry.find("points");
			if(points_it == raw_entry.end() || !points_it->is_number_integer()) {
				error = "entry.points is missing or not an integer";
				return VerifyStatus::SchemaViolation;
			}
			entry.points = points_it->get<int>();

			if(!ReadRequiredString(raw_entry, "name", entry.name)) {
				error = "entry.name is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}

			if(!ReadRequiredString(raw_entry, "macro", entry.macro)) {
				error = "entry.macro is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}
			if(ValidMacros().find(entry.macro) == ValidMacros().end()) {
				error = "entry.macro is not one of the ten closed values: " + entry.macro;
				return VerifyStatus::SchemaViolation;
			}

			if(!ReadRequiredString(raw_entry, "source", entry.source)) {
				error = "entry.source is missing or not a string";
				return VerifyStatus::SchemaViolation;
			}

			// reason: absent means has_reason=false and stays that way.
			// Present-but-empty is a schema violation, not "no reason" —
			// the artifact contract has exactly one spelling for "no
			// reason" (the key is absent), so an empty string here means
			// the producer and this client disagree about the encoding.
			auto reason_it = raw_entry.find("reason");
			if(reason_it != raw_entry.end()) {
				if(!reason_it->is_string()) {
					error = "entry.reason is present but not a string";
					return VerifyStatus::SchemaViolation;
				}
				auto reason_value = reason_it->get<std::string>();
				if(reason_value.empty()) {
					error = "entry.reason is present but empty";
					return VerifyStatus::SchemaViolation;
				}
				entry.reason = std::move(reason_value);
				entry.has_reason = true;
			}

			payload.entries.push_back(std::move(entry));
		}

		out = std::move(payload);
		return VerifyStatus::Ok;
	} catch(...) {
		error = "unexpected structure while reading the artifact";
		return VerifyStatus::SchemaViolation;
	}
}

VerifyStatus VerifyAndParse(const std::string& document, const std::string& signature,
							Payload& out, std::string& error) {
	if(!VerifySignature(document, signature)) {
		error = "signature does not verify against any trusted key";
		return VerifyStatus::BadSignature;
	}
	return Parse(document, out, error);
}

VersionDecision CompareVersion(int incoming_version, int active_version) {
	if(incoming_version > active_version)
		return VersionDecision::Accept;
	if(incoming_version == active_version)
		return VersionDecision::AlreadyCurrent;
	return VersionDecision::Rollback;
}

}
