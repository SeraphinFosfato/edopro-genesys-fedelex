#ifndef REPLAY_OSCURA_H
#define REPLAY_OSCURA_H

// FASE 98C: porting C++ di design/spike-replay-oscurato/oscura.py (+ yrpx.py).
// Dato un .yrpX intero, produce la versione "vista dal posto N": via il
// pacchetto 231 (.yrp1 con mazzi e risposte), seme azzerato, e nel flusso le
// informazioni nascoste dell'avversario mascherate con la stessa regola del
// server (StripMessageForTeam / SerializeSingleQuery). Si sostituiscono campi,
// non si tolgono pacchetti. Nessuna dipendenza da gframe/irrlicht/curl: si
// collauda in tests/ confrontando con l'uscita di oscura.py.
//
// Deterrente, non garanzia (CLAUDE.md "Onesta' sui deterrenti"): l'host puo'
// sempre barare nella propria partita; qui si evita solo di salvare su disco
// un file che contiene le carte nascoste dell'avversario.

#include <cstdint>
#include <vector>

namespace ygo::replay_oscura {

enum class Result {
	Ok,
	Unsupported, // non e' un YRPX LUA64 1v1 normale (compat, .yrp1, single/hand test): out non toccato
	Malformed,   // intestazione o lunghezze incoerenti: out non toccato
};

// seat: 0 o 1 = posto del giocatore che salva; 2 = nessun posto (spettatore:
// tutto cio' che non e' pubblico viene nascosto).
Result Obscure(const std::vector<uint8_t>& blob, int seat, std::vector<uint8_t>& out);

}

#endif //REPLAY_OSCURA_H
