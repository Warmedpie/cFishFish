#pragma once
#include <fstream>
#include <regex>
#include <set>

#include "Chess.h"

using namespace chess;

class Book {

private:
	std::unordered_map<uint64_t, std::vector<Move>> book;

public:
	inline Book() {
		std::fstream file;
		file.open("book.txt");
		std::string line;

		while (std::getline(file, line))
		{

			Board b;
			std::vector<std::string> moveList = split(line);

			if (book.contains(b.zobrist())) {
				book[b.zobrist()].push_back(uci::uciToMove(b, moveList[0]));
			}
			else {
				book[b.zobrist()] = { uci::uciToMove(b, moveList[0]) };
			}
			for (int i = 0; i < moveList.size() - 1; i++) {

				Move m = uci::uciToMove(b, moveList[i]);

				b.makeMove(m);

				if (book.contains(b.zobrist())) {
					book[b.zobrist()].push_back(uci::uciToMove(b, moveList[i + 1]));
				}
				else {
					book[b.zobrist()] = { uci::uciToMove(b, moveList[i + 1]) };
				}

			}

		}

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


	inline Move probeBook(uint64_t hash) {
		if (book.contains(hash)) {

			int i = rand() % book[hash].size();
			return book[hash][i];
		}

		return 0;
	}

};