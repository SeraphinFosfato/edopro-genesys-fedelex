#include "title_verify.h"

#include <cstring>
#include <vector>
#include <nlohmann/json.hpp>
#include "title_keys.h"
// See the matching comment in banlist_verify.cpp: tweetnacl.h has no
// extern "C" guard of its own.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

namespace ygo::title {

namespace {

// Minimal, strict base64 decoder (standard alphabet, '=' padding) — the
// title API's "signature" field is always Node's Buffer#toString("base64")
// (src/util/titleKey.ts): padded, standard alphabet, never base64url. Written
// locally rather than reusing gframe/Base64.h: that header pulls in
// text_types.h, and this whole module's one hard rule is staying linkable
// with zero gframe headers (title_verify.h's own note explains why).
bool Base64Decode(const std::string& in, std::vector<uint8_t>& out) {
	out.clear();
	if(in.empty() || in.size() % 4 != 0)
		return false;
	auto decode_char = [](char c) -> int {
		if(c >= 'A' && c <= 'Z') return c - 'A';
		if(c >= 'a' && c <= 'z') return c - 'a' + 26;
		if(c >= '0' && c <= '9') return c - '0' + 52;
		if(c == '+') return 62;
		if(c == '/') return 63;
		return -1;
	};
	out.reserve(in.size() / 4 * 3);
	for(size_t i = 0; i < in.size(); i += 4) {
		int pad = 0;
		uint32_t chunk = 0;
		for(int j = 0; j < 4; ++j) {
			const char c = in[i + static_cast<size_t>(j)];
			if(c == '=') {
				// Padding may only trail the string, in its final quartet —
				// '=' anywhere else is not valid base64, not a byte to guess.
				if(i + 4 != in.size())
					return false;
				++pad;
				chunk <<= 6;
				continue;
			}
			if(pad > 0)
				return false; // a real character after padding has started
			const int v = decode_char(c);
			if(v < 0)
				return false;
			chunk = (chunk << 6) | static_cast<uint32_t>(v);
		}
		if(pad > 2)
			return false;
		out.push_back(static_cast<uint8_t>((chunk >> 16) & 0xff));
		if(pad < 2)
			out.push_back(static_cast<uint8_t>((chunk >> 8) & 0xff));
		if(pad < 1)
			out.push_back(static_cast<uint8_t>(chunk & 0xff));
	}
	return true;
}

bool ReadRequiredString(const nlohmann::json& obj, const char* key, std::string& out) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return false;
	out = it->get<std::string>();
	return true;
}

// Parses just enough to hand VerifyAndParse* a usable nlohmann::json object,
// or nothing at all — a malformed body must never reach the per-type parsers
// as anything but "no object here".
bool ParseObject(const std::string& document, nlohmann::json& out) {
	nlohmann::json j = nlohmann::json::parse(document, nullptr, false);
	if(j.is_discarded() || !j.is_object())
		return false;
	out = std::move(j);
	return true;
}

}

MessageType PeekType(const std::string& document) {
	nlohmann::json j;
	if(!ParseObject(document, j))
		return MessageType::Unknown;
	std::string type;
	if(!ReadRequiredString(j, "type", type))
		return MessageType::Unknown;
	if(type == "title") return MessageType::Title;
	if(type == "revocation") return MessageType::Revocation;
	if(type == "suspension") return MessageType::Suspension;
	return MessageType::Unknown;
}

bool VerifyWithDomainUsing(const std::string& domain, const std::string& canonical,
						  const std::string& signature_base64,
						  const uint8_t* const* keys, size_t key_count) {
	std::vector<uint8_t> signature;
	if(!Base64Decode(signature_base64, signature) || signature.size() != ED25519_SIGNATURE_SIZE)
		return false;

	// Byte-identical to signWithDomain on the bot (src/util/titleKey.ts):
	// message = domain + "\n" + canonical, UTF-8.
	std::string message;
	message.reserve(domain.size() + 1 + canonical.size());
	message += domain;
	message += '\n';
	message += canonical;

	// Same combined-form trick as banlist_verify.cpp's VerifySignatureWith:
	// TweetNaCl's crypto_sign_open only knows signature-then-message
	// concatenated, and a detached Ed25519 signature is byte-identical to
	// those same 64 bytes.
	std::vector<unsigned char> signed_message(ED25519_SIGNATURE_SIZE + message.size());
	std::memcpy(signed_message.data(), signature.data(), ED25519_SIGNATURE_SIZE);
	if(!message.empty())
		std::memcpy(signed_message.data() + ED25519_SIGNATURE_SIZE, message.data(), message.size());
	std::vector<unsigned char> opened(signed_message.size());
	unsigned long long opened_len = 0;
	for(size_t i = 0; i < key_count; ++i) {
		if(crypto_sign_open(opened.data(), &opened_len, signed_message.data(),
							static_cast<unsigned long long>(signed_message.size()), keys[i]) == 0) {
			// First trusted key that verifies, not only the first entry: what
			// makes rotating operational -> reserve a bot-side release
			// instead of a reinstall for every player (title_keys.h).
			return true;
		}
	}
	return false;
}

bool VerifyWithDomain(const std::string& domain, const std::string& canonical,
					 const std::string& signature_base64) {
	return VerifyWithDomainUsing(domain, canonical, signature_base64, TRUSTED_KEYS,
								 sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus VerifyAndParseTitleWith(const std::string& document, TitleFields& out, std::string& error,
									 const uint8_t* const* keys, size_t key_count) {
	nlohmann::json j;
	if(!ParseObject(document, j)) {
		error = "not valid JSON";
		return VerifyStatus::MalformedJson;
	}
	try {
		TitleFields fields;
		if(!ReadRequiredString(j, "user_ref", fields.user_ref)) {
			error = "user_ref is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "issued_at", fields.issued_at)) {
			error = "issued_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "expires_at", fields.expires_at)) {
			error = "expires_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		std::string signature_b64;
		if(!ReadRequiredString(j, "signature", signature_b64)) {
			error = "signature is missing or not a string";
			return VerifyStatus::BadSignature;
		}

		// Byte-identical to canonicalTitle on the bot (src/api/titles.ts).
		const std::string canonical = fields.user_ref + "\n" + fields.issued_at + "\n" + fields.expires_at;
		if(!VerifyWithDomainUsing(TITLE_DOMAIN, canonical, signature_b64, keys, key_count)) {
			error = "signature does not verify against any trusted title key";
			return VerifyStatus::BadSignature;
		}

		out = std::move(fields);
		return VerifyStatus::Ok;
	} catch(...) {
		error = "unexpected structure while reading the title";
		return VerifyStatus::SchemaViolation;
	}
}

VerifyStatus VerifyAndParseTitle(const std::string& document, TitleFields& out, std::string& error) {
	return VerifyAndParseTitleWith(document, out, error, TRUSTED_KEYS, sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus VerifyAndParseRevocationWith(const std::string& document, RevocationFields& out, std::string& error,
										  const uint8_t* const* keys, size_t key_count) {
	nlohmann::json j;
	if(!ParseObject(document, j)) {
		error = "not valid JSON";
		return VerifyStatus::MalformedJson;
	}
	try {
		RevocationFields fields;
		if(!ReadRequiredString(j, "user_ref", fields.user_ref)) {
			error = "user_ref is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "revoked_at", fields.revoked_at)) {
			error = "revoked_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}

		// banned_until is `string | null` on the wire (RevocationBody in
		// titles.ts) — ALWAYS present as one or the other, never absent. A
		// missing key here means this client and the bot disagree about the
		// shape, exactly the kind of silent divergence this module exists to
		// catch rather than paper over with a default.
		auto banned_it = j.find("banned_until");
		if(banned_it == j.end()) {
			error = "banned_until key is missing";
			return VerifyStatus::SchemaViolation;
		}
		if(banned_it->is_null()) {
			fields.banned_until.clear();
			fields.has_banned_until = false;
		} else if(banned_it->is_string()) {
			fields.banned_until = banned_it->get<std::string>();
			fields.has_banned_until = true;
		} else {
			error = "banned_until is present but neither a string nor null";
			return VerifyStatus::SchemaViolation;
		}

		std::string signature_b64;
		if(!ReadRequiredString(j, "signature", signature_b64)) {
			error = "signature is missing or not a string";
			return VerifyStatus::BadSignature;
		}

		// Byte-identical to canonicalRevocation on the bot: the JSON null
		// case folds to "" BEFORE the newline join — same as the bot's own
		// `fields.banned_until ?? ""` — not a separate step this function
		// could get out of sync with, since fields.banned_until already
		// holds "" whenever has_banned_until is false.
		const std::string canonical = fields.user_ref + "\n" + fields.revoked_at + "\n" + fields.banned_until;
		if(!VerifyWithDomainUsing(REVOCATION_DOMAIN, canonical, signature_b64, keys, key_count)) {
			error = "signature does not verify against any trusted title key";
			return VerifyStatus::BadSignature;
		}

		out = std::move(fields);
		return VerifyStatus::Ok;
	} catch(...) {
		error = "unexpected structure while reading the revocation";
		return VerifyStatus::SchemaViolation;
	}
}

VerifyStatus VerifyAndParseRevocation(const std::string& document, RevocationFields& out, std::string& error) {
	return VerifyAndParseRevocationWith(document, out, error, TRUSTED_KEYS, sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

VerifyStatus VerifyAndParseSuspensionWith(const std::string& document, SuspensionFields& out, std::string& error,
										  const uint8_t* const* keys, size_t key_count) {
	// PROVISIONAL (D90, FASE 11 inerte): the bot never emits this message
	// today, so this shape has not been negotiated against a real payload —
	// only against design/fork-edopro/access-control.md §9's sketch. It is
	// implemented now because the client that will have to understand it is
	// the one shipping today (§10): waiting until the bot side exists would
	// mean an emergency release instead of ten quiet lines. When FASE 11's
	// bot contract is written, this function — and the two-field canonical
	// form below — is the first thing to re-check against it.
	nlohmann::json j;
	if(!ParseObject(document, j)) {
		error = "not valid JSON";
		return VerifyStatus::MalformedJson;
	}
	try {
		SuspensionFields fields;
		if(!ReadRequiredString(j, "user_ref", fields.user_ref)) {
			error = "user_ref is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		if(!ReadRequiredString(j, "suspended_at", fields.suspended_at)) {
			error = "suspended_at is missing or not a string";
			return VerifyStatus::SchemaViolation;
		}
		std::string signature_b64;
		if(!ReadRequiredString(j, "signature", signature_b64)) {
			error = "signature is missing or not a string";
			return VerifyStatus::BadSignature;
		}

		const std::string canonical = fields.user_ref + "\n" + fields.suspended_at;
		if(!VerifyWithDomainUsing(SUSPENSION_DOMAIN, canonical, signature_b64, keys, key_count)) {
			error = "signature does not verify against any trusted title key";
			return VerifyStatus::BadSignature;
		}

		out = std::move(fields);
		return VerifyStatus::Ok;
	} catch(...) {
		error = "unexpected structure while reading the suspension";
		return VerifyStatus::SchemaViolation;
	}
}

VerifyStatus VerifyAndParseSuspension(const std::string& document, SuspensionFields& out, std::string& error) {
	return VerifyAndParseSuspensionWith(document, out, error, TRUSTED_KEYS, sizeof(TRUSTED_KEYS) / sizeof(TRUSTED_KEYS[0]));
}

}
