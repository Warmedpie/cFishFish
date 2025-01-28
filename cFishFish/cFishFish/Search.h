#pragma once

#include <chrono>

#include "Chess.h"
#include "Eval.h"
#include "TranspositionTable.h"
#include "ScoredMove.h"

using namespace chess;

class Search {
private:

	//Search components
	Board* board;
	TranspositionTable TT;

	//Stats
	int nodes = 0;
	int TB_hits = 0;

	//Timing
	std::chrono::steady_clock::time_point start_time;
	int search_time = 0;

	int order_score[13] = { 82, 337, 365, 477, 1025, 0, 82, 337, 365, 477, 1025, 0, 0 };

	int history_hueristic[64][64];
	Move killer_move[64];

	Move multipv[10] = { 0,0,0,0,0,0,0,0,0,0 };

public:

	struct MOVE {
		Move m;
		bool isCapture;
	};

	inline void set_board(Board* b) {
		this->board = b;
	}

	inline void setup(Board* b, int search_time) {
		start_time = std::chrono::steady_clock::now();
		this->search_time = search_time;
		this->board = b;
		this->nodes = 0;
		TT.clear();

		for (int i = 0; i < 64; i++) {
			if (i < 10)
				multipv[i] = 0;

			killer_move[i] = 0;
			for (int j = 0; j < 64; j++) {
				history_hueristic[i][j] = 0;
			}
		}

	}

	inline bool checkTimeout() {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		int total_ms = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return (total_ms > search_time);

	}

	int entryPoint(int alpha, int beta, int depth, int plydeep, std::vector<Move> ignore, Move prev);
	int PVS(int alpha, int beta, int depth, int plydeep, MOVE prev);
	int qSearch(int alpha, int beta, int depth);

	bool nodeTableBreak(entry node, int depth, int alpha, int beta);

	int mateScore(int plydeep);
	bool hasLegalMoves();

	std::vector<ScoredMove> orderAll(Move best, int depth, Move counter = 0);
	std::vector<ScoredMove> orderCaptures(Move best);
	std::vector<ScoredMove> orderQuiet(Move best, int depth, Move threat = 0, Move counter = 0);
	std::vector<ScoredMove> orderChecks();

	inline Move bestMove(int n) {
		return multipv[n];
	}

	inline std::string pv(int n) {
		Move best = bestMove(n);

		std::string toReturn;
		toReturn += uci::moveToUci(best) + " ";

		Board b = Board(*board);

		b.makeMove(best);
		int i = 0;
		while (i++ < 20) {
			Move best = TT.transposition_search(b.zobrist()).best;

			if (best == 0)
				break;

			toReturn += uci::moveToUci(best) + " ";
			b.makeMove(best);

		}

		return toReturn;
	}

	inline int time() {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		int total_ms = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return total_ms;
	}

	inline int getNodes() {
		return this->nodes;
	}

	inline int nps() {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		int total_ms = (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return (nodes / (total_ms + 1)) * 1000;
	}

	inline int tb_hits() {
		return this->TB_hits;
	}

	inline void hault() {
		this->search_time = 0;
	}

	inline bool contains(std::vector<Move> ignore, Move m) {
		for (int i = 0; i < ignore.size(); i++) {
			if (m.from() == ignore[i].from())
				return true;
		}

		return false;
	}

	inline void table_size(int size) {
		TT.set_table(size);
	}

};