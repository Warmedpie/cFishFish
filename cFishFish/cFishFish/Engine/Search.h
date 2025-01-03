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

	int order_score[11] = { 82, 337, 365, 477, 1025, 82, 337, 365, 477, 1025,0 };

	int history_hueristic[64][64];
	Move killer_move[64];

public:

	inline void setup(Board* b, int search_time) {
		start_time = std::chrono::steady_clock::now();
		this->search_time = search_time;
		this->board = b;
		this->nodes = 0;
		TT.clear();

		for (int i = 0; i < 64; i++) {
			killer_move[i] = 0;
			for (int j = 0; j < 64; j++) {
				history_hueristic[i][j] = 0;
			}
		}

	}

	inline bool checkTimeout() {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		int t = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
		int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return (total_ms > search_time);
			
	}

	int PVS(int alpha, int beta, int depth, int plydeep);
	int negamax(int alpha, int beta, int depth, int plydeep);
	int qSearch(int alpha, int beta, int depth);

	bool nodeTableBreak(entry node, int depth, int alpha, int beta);

	int mateScore(int plydeep);
	bool hasLegalMoves();

	std::vector<ScoredMove> orderAll(Move best, int depth);
	std::vector<ScoredMove> orderCaptures(Move best);
	std::vector<ScoredMove> orderQuiet(Move best, int depth);
	std::vector<ScoredMove> orderChecks();

	inline Move bestMove() {
		return TT.transposition_search(board->zobrist()).best;
	}

	inline std::string pv() {
		Move best = TT.transposition_search(board->zobrist()).best;

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

		int t = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
		int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return total_ms;
	}

	inline int getNodes() {
		return this->nodes;
	}

	inline int nps() {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		int t = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
		int total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

		return (nodes / (total_ms + 1)) * 1000;
	}

	inline int tb_hits() {
		return this->TB_hits;
	}

	inline void hault() {
		this->search_time = 0;
	}

};