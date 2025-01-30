#pragma once

#include "Chess.h"
#include "Search.h"
#include "Book.h"
#include "Eval.h"
#include "TranspositionTable.h"

#include <string>
#include <regex>
#include <future>

class UCI {

private:
	Board* board = new Board;
	Book book;

	int multiPv = 1;
	int hash_size = 128;
	bool debug = false;
	Search search;
	Move prev = 0;

	int last_depth = 0;

public:

	UCI() {
		search.table_size(hash_size * 1024 * 1024);
	}

	inline void parse(std::string command) {

		std::vector<std::string> arguments = split(command);

		for (int i = 0; i < arguments.size(); i++) {

			std::string argument = arguments[i];

			std::transform(argument.begin(), argument.end(), argument.begin(),
				[](unsigned char c) { return std::toupper(c); });

			if (argument == "UCI") {

				std::cout << "id name cFishFish v1.0" << std::endl;
				std::cout << "id author Warmedpie" << std::endl;
				std::cout << "option name MultiPV type spin default 1 min 1 max 10" << std::endl;
				std::cout << "option name Hash type spin min 2 max 4096 default 128" << std::endl;
				std::cout << "uciok" << std::endl;

				continue;
			}

			if (argument == "DEBUG") {
				if (arguments.size() > i + 1) {

					std::transform(arguments[i + 1].begin(), arguments[i + 1].end(), arguments[i + 1].begin(),
						[](unsigned char c) { return std::toupper(c); });

					if (arguments[i + 1] == "ON") {
						debug = true;
						i++;
					}
					else if (arguments[i + 1] == "OFF") {
						debug = false;
						i++;
					}
				}

				continue;
			}

			if (argument == "ISREADY") {
				std::cout << "readyok" << std::endl;

				continue;
			}

			if (argument == "SETOPTION") {
				if (arguments.size() > i + 1) {

					std::transform(arguments[i + 1].begin(), arguments[i + 1].end(), arguments[i + 1].begin(),
						[](unsigned char c) { return std::toupper(c); });

					if (arguments[i + 1] == "NAME") {
						if (arguments.size() > i + 4) {

							std::transform(arguments[i + 2].begin(), arguments[i + 2].end(), arguments[i + 2].begin(),
								[](unsigned char c) { return std::toupper(c); });

							std::transform(arguments[i + 3].begin(), arguments[i + 3].end(), arguments[i + 3].begin(),
								[](unsigned char c) { return std::toupper(c); });

							std::transform(arguments[i + 4].begin(), arguments[i + 4].end(), arguments[i + 4].begin(),
								[](unsigned char c) { return std::toupper(c); });

							if (arguments[i + 2] == "MULTIPV" && arguments[i + 3] == "VALUE") {
								multiPv = std::min(std::stoi(arguments[i + 4]), 10);

								if (multiPv < 1)
									multiPv = 1;
							}

							if (arguments[i + 2] == "HASH" && arguments[i + 3] == "VALUE") {
								hash_size = std::min(std::stoi(arguments[i + 4]), 4096);

								if (hash_size < 2)
									hash_size = 2;

								search.table_size(hash_size * 1024 * 1024);
							}
						}
					}
				}

				continue;
			}

			if (argument == "REGISTER") {
				continue;
			}

			if (argument == "UCINEWGAME") {
				*board = Board();

				continue;
			}

			if (argument == "POSITION") {
				if (arguments.size() > i + 1) {

					std::transform(arguments[i + 1].begin(), arguments[i + 1].end(), arguments[i + 1].begin(),
						[](unsigned char c) { return std::toupper(c); });

					if (arguments[i + 1] == "FEN") {
						*board = Board(arguments[2] + " " + arguments[3] + " " + arguments[4] + " " + arguments[5] + " " + arguments[6] + " " + arguments[7]);
						i = 7;
						if (arguments.size() > i + 1) {

							std::transform(arguments[i + 1].begin(), arguments[i + 1].end(), arguments[i + 1].begin(),
								[](unsigned char c) { return std::toupper(c); });

							if (arguments[++i] == "MOVES") {
								for (int q = ++i; q < arguments.size(); q++) {
									Move m = uci::uciToMove(*board, arguments[q]);

									Movelist moves;
									movegen::legalmoves(moves, *board);

									if (contains(moves, m)) {
										board->makeMove(m);
									}
									else {
										std::cout << "info illegal!" << std::endl;
										return;
									}

									prev = m;
								}
							}
						}
					}
					else if (arguments[i + 1] == "STARTPOS") {

						*board = Board();
						i++;

						if (arguments.size() > i + 1) {

							std::transform(arguments[i + 1].begin(), arguments[i + 1].end(), arguments[i + 1].begin(),
								[](unsigned char c) { return std::toupper(c); });

							if (arguments[++i] == "MOVES") {

								for (int q = ++i; q < arguments.size(); q++) {
									Move m = uci::uciToMove(*board, arguments[q]);

									Movelist moves;
									movegen::legalmoves(moves, *board);

									if (contains(moves, m)) {
										board->makeMove(m);
									}
									else {
										std::cout << "info illegal!" << std::endl;
										return;
									}

									prev = m;
								}
							}
						}

					}

				}

				std::cout << "info ok!" << std::endl;

				continue;
			}

			if (argument == "STATIC") {
				std::cout << Eval::evaluate(board, 1251) << std::endl;

				continue;
			}

			if (argument == "ORDER") {

				search.set_board(board);

				Move best = search.bestMove(0);

				std::vector<ScoredMove> moves = search.orderAll(best, last_depth);

				for (int i = 0; i < moves.size(); i++) {
					std::cout << moves[i].move << ":" << moves[i].score << std::endl;
				}
			}


			if (argument == "GO") {
				int depth = 32;
				int search_time = 999999999;
				int smart_time = 0;
				bool mateOnly = false;

				i++;

				while (i < arguments.size()) {

					std::transform(arguments[i].begin(), arguments[i].end(), arguments[i].begin(),
						[](unsigned char c) { return std::toupper(c); });

					if (arguments[i] == "DEPTH") {
						i++;
						if (i < arguments.size())
							depth = std::stoi(arguments[i++]);

					}
					else if (arguments[i] == "MOVETIME") {
						i++;
						if (i < arguments.size())
							search_time = std::stoi(arguments[i++]);
					}
					else if (arguments[i] == "INFINITE" || arguments[i] == "PONDER") {
						i++;
					}
					else if (arguments[i] == "MATE") {
						mateOnly = true;
						i++;
						if (i < arguments.size())
							depth = std::stoi(arguments[i++]);
					}
					else if (arguments[i] == "wtime") {
						i++;
						if (i < arguments.size() && board->sideToMove() == Color::WHITE)
							smart_time += std::stoi(arguments[i++]) / 600;
					}
					else if (arguments[i] == "btime") {
						i++;
						if (i < arguments.size() && board->sideToMove() == Color::BLACK)
							smart_time += std::stoi(arguments[i++]) / 600;
					}
					else if (arguments[i] == "winc") {
						i++;
						if (i < arguments.size())
							smart_time += std::stoi(arguments[i++]);
					}
					else if (arguments[i] == "binc") {
						i++;
						if (i < arguments.size())
							smart_time += std::stoi(arguments[i++]);
					}

				}

				if (smart_time > 0 && search_time == 999999999) {
					search_time = smart_time;
				}

				Move book_move = book.probeBook(board->zobrist());
				if (book_move != 0) {
					std::cout << "bestmove " << uci::moveToUci(book_move) << std::endl;

					return;
				}

				//Create a new thread for calculation
				std::thread t1([&](int depth, int search_time) {
					//This logic needs to be moved into a thread.
					search.setup(board, search_time);

					Movelist moves;
					movegen::legalmoves<>(moves, *board);

					if (moves.size() == 1) {
						std::cout << "bestmove " << uci::moveToUci(moves[0]) << std::endl;
						return;
					}
						 

					Move best_move = 0;
					for (int i = 3; i <= depth; i++) {

						std::vector<Move> ignore = {};
						int score = 0;
						for (int multi = 0; multi < multiPv; multi++) {
							score = search.entryPoint(-9999999, 9999999, i, 0, ignore, prev);

							if (score == -312312 || score == 312312)
								break;

							Move best = search.bestMove(multi);

							int mate_score = mateDisplayScore(score);

							if (score < 0)
								mate_score *= -1;

							if (mate_score == 0)
								std::cout << "info multipv " << multi + 1 << " depth " << i << " score cp " << score << " time " << search.time() << " nodes " << search.getNodes() << " nps " << search.nps() << " tbhits " << search.tb_hits() << " pv " << search.pv(multi) << std::endl;
							else
								std::cout << "info multipv " << multi + 1 << " depth " << i << " score mate " << mate_score << " time " << search.time() << " nodes " << search.getNodes() << " nps " << search.nps() << " tbhits " << search.tb_hits() << " pv " << search.pv(multi) << std::endl;

							ignore.push_back(best);

							last_depth = i;
						}

						if (score == -312312 || score == 312312)
							break;

						best_move = search.bestMove(0);
					}

					std::cout << "bestmove " << uci::moveToUci(best_move) << std::endl;

					}, depth, search_time);

				t1.detach();

			}

			if (argument == "STOP") {
				search.hault();
				continue;
			}

			if (argument == "QUIT") {
				exit(0);
			}


			i++;

		}

	}

	inline int mateDisplayScore(int score) {

		if (std::abs(score) > 98999) {
			return (1000000 - std::abs(score)) / 2;
		}

		return 0;

	}

	inline std::vector<std::string> split(const std::string& s) {

		std::vector<std::string> toReturn;

		std::regex e = std::regex{ "\\s+" };
		std::sregex_token_iterator i(s.begin(), s.end(), e, -1);
		std::sregex_token_iterator end;
		while (i != end) {
			toReturn.push_back(*i++);
		}

		return toReturn;

	}

	inline bool contains(Movelist legal, Move m) {
		for (int i = 0; i < legal.size(); i++) {
			if (m.from() == legal[i].from() && m.to() == legal[i].to())
				return true;
		}

		return false;
	}

};