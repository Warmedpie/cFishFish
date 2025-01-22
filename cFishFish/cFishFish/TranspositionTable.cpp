#include "TranspositionTable.h"
/*
entry TranspositionTable::transposition_search(uint64_t h) {
	if (this->transposition_table.count(h)) {
		return this->transposition_table[h];
	}
	return no_hash;
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
*/

entry TranspositionTable::transposition_search_no_adjust(uint64_t h) {
	return this->table[h];
}

entry TranspositionTable::transposition_search(uint64_t h) {
	uint64_t key = get_index(h);
	if (this->table[key].hash == h) {
		return this->table[key];
	}
	return no_hash;
}

void TranspositionTable::transposition_entry(uint64_t h, entry e) {
	uint64_t key = get_index(h);
	if (this->table[key].hash != h) {
		this->table[key] = e;
		this->table[key].hash = h;
	}
	else {
		if (this->table[key].depth < e.depth) {
			this->table[key] = e;
			this->table[key].hash = h;
		}
	}
}