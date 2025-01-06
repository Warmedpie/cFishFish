#pragma once
#include "Chess.h"

using namespace chess;

enum nodeType : int {
	EXACT = 0, ALL, CUT, THREAT_PREVENTION, NOTYPE
};

struct entry {
	int depth;
	nodeType type;
	Move best;
	int score;
};

class TranspositionTable {

private:

	std::unordered_map<uint64_t, entry> transposition_table;


public:

	void transposition_entry(uint64_t h, entry e);
	entry transposition_search(uint64_t h);

	inline void clear() {
		transposition_table.clear();
	}

};