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
	uint64_t hash;
};

class TranspositionTable {

private:

	std::unordered_map<uint64_t, entry> transposition_table;

	entry no_hash = { -1, NOTYPE, 0, 0, 0 };

	uint64_t table_byte_size = 128 * 1024 * 1024;
	std::vector<entry> table;
	uint64_t get_index(uint64_t key) {
		return key % table.size();
	}


public:

	void transposition_entry(uint64_t h, entry e);
	entry transposition_search(uint64_t h);

	inline void clear() {
		transposition_table.clear();

		for (int i = 0; i < table.size(); i++)
			if (table[i].depth != -1)
				table[i].depth = 0;
	}

	void set_table(uint64_t bytes = 0) {
		if (bytes > 0)
			this->table_byte_size = bytes;

		uint64_t length = table_byte_size / sizeof(entry);

		table.clear();

		for (int i = 0; i < length; i++)
			table.push_back({ -1, NOTYPE, 0, 0, 0 });
	}

};