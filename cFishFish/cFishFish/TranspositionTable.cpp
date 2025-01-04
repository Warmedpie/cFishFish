#include "TranspositionTable.h"

entry TranspositionTable::transposition_search(uint64_t h) {
	if (this->transposition_table.count(h)) {
		return { this->transposition_table[h] };
	}
	return { -1,NOTYPE,0,0 };
}

void TranspositionTable::transposition_entry(uint64_t h, entry e) {
	if (!this->transposition_table.count(h)) {
		transposition_table[h] = e;
	}
	else {
		if (transposition_table[h].depth < e.depth) {
			transposition_table[h] = e;
		}
	}
}