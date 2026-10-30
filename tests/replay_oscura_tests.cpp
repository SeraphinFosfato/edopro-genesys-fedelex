// FASE 98C: replay_oscura.cpp (porting di design/spike-replay-oscurato/oscura.py).
// Due parti: casi sintetici sempre attivi, e il confronto con l'uscita di
// oscura.py sui duelli veri SE la variabile d'ambiente EDO_OSCURA_DIR punta a
// design/spike-replay-oscurato del vault (in/ e out/ non stanno in questo repo
// pubblico: contengono nomi di giocatori reali). Senza la variabile la seconda
// parte e' saltata e lo dice.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "lzma/LzmaLib.h"
#include "replay_oscura.h"

int RunReplayOscuraTests();

namespace {
int failures = 0;
void check(bool c, const char* what) {
	if(!c) { ++failures; std::printf("FAIL: %s\n", what); }
}
using Bytes = std::vector<uint8_t>;
void put32(Bytes& b, uint32_t v) { for(int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }
void packet(Bytes& body, uint8_t m, const Bytes& d) { body.push_back(m); put32(body, static_cast<uint32_t>(d.size())); body.insert(body.end(), d.begin(), d.end()); }

// YRPX non compresso, intestazione estesa, flag LUA64|NEWREPLAY|DUEL64|EXT.
Bytes make(const Bytes& packets) {
	Bytes h(72, 0);
	const uint32_t id = 0x58707279, flag = 0x10 | 0x20 | 0x100 | 0x200;
	std::memcpy(&h[0], &id, 4); std::memcpy(&h[8], &flag, 4);
	for(int i = 40; i < 72; ++i) h[i] = 0xAB; // seme
	Bytes body;
	for(int s = 0; s < 2; ++s) { put32(body, 1); body.insert(body.end(), 40, 0); }
	body.insert(body.end(), 8, 0);
	body.insert(body.end(), packets.begin(), packets.end());
	h.insert(h.end(), body.begin(), body.end());
	return h;
}

Bytes read(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(f), {});
}
// Corpo decompresso e intestazione senza i props (dipendono dall'encoder).
bool split(const Bytes& blob, Bytes& header, Bytes& body) {
	if(blob.size() < 72) return false;
	uint32_t flag, ds;
	std::memcpy(&flag, &blob[8], 4); std::memcpy(&ds, &blob[16], 4);
	const size_t hl = (flag & 0x200) ? 72 : 32;
	header.assign(blob.begin(), blob.begin() + hl);
	std::memset(&header[24], 0, 8);
	body.resize(ds);
	size_t dl = ds, sl = blob.size() - hl;
	return LzmaUncompress(body.data(), &dl, blob.data() + hl, &sl, blob.data() + 24, 5) == SZ_OK && dl == ds;
}
}

int RunReplayOscuraTests() {
	using namespace ygo::replay_oscura;
	{ // DRAW dell'avversario: la carta coperta va a 0, quella scoperta resta; il proprio draw non cambia.
		Bytes draw{ 1 }; put32(draw, 2); put32(draw, 111); put32(draw, 0xA); put32(draw, 222); put32(draw, 0x5);
		Bytes pk; packet(pk, 90, draw); packet(pk, 231, Bytes{ 1, 2, 3 });
		Bytes out;
		check(Obscure(make(pk), 0, out) == Result::Ok, "Ok");
		Bytes expect = draw; std::memset(&expect[5], 0, 4);
		Bytes tail(out.begin() + 72 + 96, out.end());
		Bytes want; packet(want, 90, expect);
		check(tail == want, "carta coperta avversaria azzerata, scoperta intatta, pacchetto 231 tolto");
		check(std::all_of(out.begin() + 40, out.begin() + 72, [](uint8_t c) { return c == 0; }), "seme azzerato");
		Bytes own;
		check(Obscure(make(pk), 1, own) == Result::Ok, "Ok seat 1");
		Bytes want1; packet(want1, 90, draw);
		check(Bytes(own.begin() + 72 + 96, own.end()) == want1, "il proprio draw (posto 1) non cambia");
		check(Obscure(out, 0, own) == Result::Ok && own == out, "idempotente: file gia' oscurato restituito identico");
		Bytes spect;
		check(Obscure(make(pk), 2, spect) == Result::Ok, "Ok spettatore");
		check(Bytes(spect.begin() + 72 + 96, spect.end()) == want, "spettatore: nascosto come l'avversario");
	}
	{ // Rifiuti: non si indovina.
		Bytes out, junk(10, 0);
		check(Obscure(junk, 0, out) == Result::Malformed, "troppo corto");
		Bytes draw{ 1 }; put32(draw, 100000); // conteggio assurdo
		Bytes pk; packet(pk, 90, draw);
		check(Obscure(make(pk), 0, out) == Result::Malformed, "lunghezze incoerenti");
		auto yrp1 = make(Bytes{}); yrp1[0] = 'y'; yrp1[3] = '1';
		check(Obscure(yrp1, 0, out) == Result::Unsupported, ".yrp1 non coperto");
	}
	if(const char* dir = std::getenv("EDO_OSCURA_DIR")) {
		const char* names[] = { "Gatto_VS_Seraphin", "Nouvelles_VS_VexRaven", "Test 1" };
		for(auto* n : names) for(int seat = 0; seat < 2; ++seat) {
			const std::string base = dir;
			auto in = read(base + "/in/" + n + ".yrpX");
			auto ref = read(base + "/out/" + n + ".g" + std::to_string(seat) + ".yrpX");
			Bytes out, h1, b1, h2, b2;
			const bool ok = !in.empty() && !ref.empty() && Obscure(in, seat, out) == Result::Ok && split(out, h1, b1) && split(ref, h2, b2);
			check(ok, n);
			if(ok) {
				check(h1 == h2, "intestazione uguale a oscura.py (esclusi i props)");
				check(b1 == b2, "corpo decompresso identico a oscura.py");
				Bytes again;
				check(Obscure(ref, seat, again) == Result::Ok && again == ref, "un file gia' oscurato torna identico byte per byte (sha256 del server)");
				std::printf("%s g%d: %zu byte di corpo identici a oscura.py\n", n, seat, b1.size());
			}
		}
	} else {
		std::printf("replay_oscura: EDO_OSCURA_DIR non impostata, confronto con oscura.py SALTATO\n");
	}
	return failures;
}
