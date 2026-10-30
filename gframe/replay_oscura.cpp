#include "replay_oscura.h"
#include <cstring>
#include "lzma/LzmaLib.h"

// Layout e regole: design/spike-replay-oscurato/oscura.py, riga per riga.
// Codici messaggio e flag sono ripetuti qui (non inclusi da ocgapi_constants.h /
// replay.h) per tenere il modulo senza dipendenze, come tournament_mode.h.

namespace ygo::replay_oscura {
namespace {

constexpr uint32_t YRPX = 0x58707279;
constexpr uint32_t F_COMPRESSED = 0x1, F_TAG = 0x2, F_SINGLE = 0x8, F_LUA64 = 0x10, F_NEWREPLAY = 0x20,
	F_HAND_TEST = 0x40, F_DUEL64 = 0x100, F_EXT = 0x200;
constexpr size_t BASE_HEADER = 32, EXT_HEADER = 72;
constexpr uint32_t MAX_BODY = 256u << 20;

constexpr uint8_t LOC_DECK = 0x01, LOC_GRAVE = 0x10, LOC_OVERLAY = 0x80;
constexpr uint32_t POS_FACEUP = 0x5, POS_FACEDOWN = 0xA;
constexpr uint32_t Q_POS = 0x2, Q_PUBLIC = 0x100000, Q_END = 0x80000000;
// QUERY_CODE ALIAS TYPE LEVEL RANK ATTRIBUTE RACE ATTACK DEFENSE BASE_ATK BASE_DEF STATUS LSCALE RSCALE LINK
constexpr uint32_t PRIVATE_Q[] = { 0x1, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x80000, 0x200000, 0x400000, 0x800000 };

constexpr uint8_t MSG_HINT_ = 2, MSG_UPDATE_DATA_ = 6, MSG_UPDATE_CARD_ = 7, MSG_CONFIRM_CARDS_ = 31, MSG_SHUFFLE_HAND_ = 33,
	MSG_SHUFFLE_EXTRA_ = 39, MSG_MOVE_ = 50, MSG_SET_ = 54, MSG_SPSUMMONING_ = 62, MSG_DRAW_ = 90, OLD_REPLAY_MODE_ = 231;

using Bytes = std::vector<uint8_t>;
struct Bad {};

uint16_t u16(const Bytes& b, size_t p) {
	if(p + 2 > b.size()) throw Bad{};
	return static_cast<uint16_t>(b[p] | (b[p + 1] << 8));
}
uint32_t u32(const uint8_t* b, size_t len, size_t p) {
	if(p + 4 > len) throw Bad{};
	return static_cast<uint32_t>(b[p]) | (static_cast<uint32_t>(b[p + 1]) << 8) | (static_cast<uint32_t>(b[p + 2]) << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
}
uint32_t u32(const Bytes& b, size_t p) { return u32(b.data(), b.size(), p); }
void z32(Bytes& b, size_t p) {
	if(p + 4 > b.size()) throw Bad{};
	std::memset(&b[p], 0, 4);
}
void need(const Bytes& b, size_t n) {
	if(b.size() < n) throw Bad{};
}
bool isPrivate(uint32_t fl) {
	for(auto q : PRIVATE_Q)
		if(q == fl) return true;
	return false;
}

struct Entry { uint32_t flag; Bytes raw; };

// Una Query::Parse. Vuota (size 0) -> empty=true.
size_t QueryTlv(const Bytes& b, size_t p, std::vector<Entry>& out, bool& empty) {
	out.clear();
	empty = u16(b, p) == 0;
	if(empty) return p + 2;
	while(true) {
		const size_t sz = u16(b, p);
		if(sz < 4 || p + 2 + sz > b.size()) throw Bad{};
		const uint32_t fl = u32(b, p + 2);
		out.push_back({ fl, Bytes(b.begin() + p, b.begin() + p + 2 + sz) });
		p += 2 + sz;
		if(fl == Q_END) return p;
	}
}

Bytes StripQuery(const std::vector<Entry>& tlv) {
	uint32_t pos = 0;
	uint8_t pub = 0;
	for(auto& e : tlv) {
		if(e.flag == Q_POS) pos = u32(e.raw, 6);
		if(e.flag == Q_PUBLIC) { need(e.raw, 7); pub = e.raw[6]; }
	}
	const bool keep_all = pub || (pos & POS_FACEUP);
	Bytes out;
	for(auto& e : tlv)
		if(keep_all || !isPrivate(e.flag)) out.insert(out.end(), e.raw.begin(), e.raw.end());
	return out;
}

void Append32(Bytes& b, uint32_t v) {
	for(int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

Bytes DoUpdateData(Bytes b, int N) {
	need(b, 6);
	if(b[0] == N) return b;
	if(6 + static_cast<size_t>(u32(b, 2)) != b.size()) throw Bad{};
	Bytes out;
	std::vector<Entry> tlv;
	for(size_t p = 6; p < b.size();) {
		bool empty;
		p = QueryTlv(b, p, tlv, empty);
		if(empty) { out.push_back(0); out.push_back(0); } else { auto s = StripQuery(tlv); out.insert(out.end(), s.begin(), s.end()); }
	}
	Bytes res(b.begin(), b.begin() + 2);
	Append32(res, static_cast<uint32_t>(out.size()));
	res.insert(res.end(), out.begin(), out.end());
	return res;
}

Bytes DoUpdateCard(Bytes b, int N) {
	need(b, 3);
	if(b[0] == N) return b;
	std::vector<Entry> tlv;
	bool empty;
	if(QueryTlv(b, 3, tlv, empty) != b.size()) throw Bad{};
	Bytes res(b.begin(), b.begin() + 3);
	if(empty) { res.push_back(0); res.push_back(0); } else { auto s = StripQuery(tlv); res.insert(res.end(), s.begin(), s.end()); }
	return res;
}

// Un messaggio di scelta (SELECT_*, ANNOUNCE_*...) non ha qui nessuna regola:
// oscura.py si ferma, il client lo lascia com'e' (riguarda solo le scelte di chi salva).
Bytes Transform(uint8_t m, Bytes b, int N) {
	switch(m) {
	case MSG_UPDATE_DATA_: return DoUpdateData(std::move(b), N);
	case MSG_UPDATE_CARD_: return DoUpdateCard(std::move(b), N);
	case MSG_SET_:
		need(b, 5);
		if(b[4] != N) z32(b, 0);
		break;
	case MSG_SHUFFLE_HAND_:
	case MSG_SHUFFLE_EXTRA_:
		need(b, 1);
		if(b[0] != N) {
			const auto c = u32(b, 1);
			if(c > b.size() || 5 + static_cast<size_t>(c) * 4 > b.size()) throw Bad{};
			for(uint32_t i = 0; i < c; ++i) z32(b, 5 + i * 4);
		}
		break;
	case MSG_MOVE_: {
		need(b, 24);
		const uint8_t con = b[14], loc = b[15];
		const uint32_t pos = u32(b, 20);
		const bool pub = ((loc & (LOC_GRAVE | LOC_OVERLAY)) && !(loc & (LOC_DECK | 0x02))) || !(pos & POS_FACEDOWN);
		if(con != N && !pub) z32(b, 0);
		break;
	}
	case MSG_SPSUMMONING_:
		need(b, 14);
		if(b[4] != N && (u32(b, 10) & POS_FACEDOWN)) z32(b, 0);
		break;
	case MSG_DRAW_:
		need(b, 1);
		if(b[0] != N) {
			const auto c = u32(b, 1);
			if(c > b.size() || 5 + static_cast<size_t>(c) * 8 > b.size()) throw Bad{};
			for(uint32_t i = 0; i < c; ++i)
				if(!(u32(b, 5 + i * 8 + 4) & POS_FACEUP)) z32(b, 5 + i * 8);
		}
		break;
	case MSG_CONFIRM_CARDS_: {
		need(b, 5);
		const auto c = u32(b, 1);
		if(c) {
			if(c > b.size() || 5 + static_cast<size_t>(c) * 10 > b.size()) throw Bad{};
			if(b[0] != N && b[10] == LOC_DECK)
				for(uint32_t i = 0; i < c; ++i) z32(b, 5 + i * 10);
		}
		break;
	}
	case MSG_HINT_:
		need(b, 10);
		if((b[0] == 1 || b[0] == 2 || b[0] == 3 || b[0] == 5) && b[1] != N) std::memset(&b[2], 0, 8);
		break;
	default:
		break;
	}
	return b;
}

}

Result Obscure(const std::vector<uint8_t>& blob, int seat, std::vector<uint8_t>& out) {
	try {
		if(blob.size() < BASE_HEADER) return Result::Malformed;
		const uint32_t id = u32(blob, 0), flag = u32(blob, 8), datasize = u32(blob, 16);
		if(id != YRPX) return Result::Unsupported;
		if(!(flag & F_LUA64) || (flag & (F_SINGLE | F_HAND_TEST))) return Result::Unsupported;
		const size_t hlen = (flag & F_EXT) ? EXT_HEADER : BASE_HEADER;
		if(blob.size() < hlen) return Result::Malformed;

		Bytes body;
		if(flag & F_COMPRESSED) {
			if(datasize > MAX_BODY) return Result::Malformed;
			body.resize(datasize);
			size_t dlen = datasize, slen = blob.size() - hlen;
			if(LzmaUncompress(body.data(), &dlen, blob.data() + hlen, &slen, blob.data() + 24, 5) != SZ_OK || dlen != datasize)
				return Result::Malformed;
		} else {
			body.assign(blob.begin() + hlen, blob.end());
		}

		// ParseNames + ParseParams: il prefisso resta identico.
		size_t p = 0;
		for(int side = 0; side < 2; ++side) {
			uint32_t c;
			if(flag & F_NEWREPLAY) { c = u32(body, p); p += 4; } else c = (flag & F_TAG) ? 2 : 1;
			if(c > 8) return Result::Malformed;
			p += static_cast<size_t>(c) * 40;
		}
		p += (flag & F_DUEL64) ? 8 : 4;
		if(p > body.size()) return Result::Malformed;

		Bytes nbody(body.begin(), body.begin() + p);
		while(p + 5 <= body.size()) { // come ReadNextPacket: un pacchetto troncato chiude il flusso
			const uint8_t m = body[p];
			const size_t len = u32(body, p + 1);
			if(len > body.size() - p - 5) break;
			Bytes pk(body.begin() + p + 5, body.begin() + p + 5 + len);
			p += 5 + len;
			if(m == OLD_REPLAY_MODE_) continue;
			pk = Transform(m, std::move(pk), seat);
			nbody.push_back(m);
			Append32(nbody, static_cast<uint32_t>(pk.size()));
			nbody.insert(nbody.end(), pk.begin(), pk.end());
		}

		// Gia' oscurato (es. la gN firmata dal server nelle stanze da permesso):
		// si restituisce il file ORIGINALE, byte per byte, perche' il suo sha256
		// e' quello registrato dal server e il cloud lo confronta (98B).
		bool seed_zero = true;
		for(size_t i = 40; hlen == EXT_HEADER && i < EXT_HEADER; ++i) seed_zero = seed_zero && blob[i] == 0;
		if(nbody == body && seed_zero) {
			out = blob;
			return Result::Ok;
		}

		Bytes res(blob.begin(), blob.begin() + hlen);
		if(hlen == EXT_HEADER) std::memset(&res[40], 0, 32); // seme
		if(flag & F_COMPRESSED) {
			Bytes comp(nbody.size() + nbody.size() / 3 + 128);
			size_t clen = comp.size(), propsize = 5;
			uint8_t props[5];
			// Stessi parametri di Replay::EndRecord.
			if(LzmaCompress(comp.data(), &clen, nbody.data(), nbody.size(), props, &propsize, 5, 1 << 24, 3, 0, 2, 32, 1) != SZ_OK)
				return Result::Malformed;
			for(int i = 0; i < 4; ++i) res[16 + i] = static_cast<uint8_t>(nbody.size() >> (8 * i));
			std::memcpy(&res[24], props, 5);
			res.insert(res.end(), comp.begin(), comp.begin() + clen);
		} else {
			res.insert(res.end(), nbody.begin(), nbody.end());
		}
		out = std::move(res);
		return Result::Ok;
	} catch(const Bad&) {
		return Result::Malformed;
	}
}

}
