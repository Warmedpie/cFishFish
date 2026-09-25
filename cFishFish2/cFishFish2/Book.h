// Book.h : opening book for cFishFish.
//
// The lines in BookData.h are replayed once, on first use, into a table
// keyed by position hash (Zobrist), so transpositions into a book position
// are found too. A move's weight is the number of book lines that play it
// from that position. probe() picks a book move at random, weighted.

#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "BookData.h"
#include "Chess.h"

namespace book {

struct BookMove {
    chess::Move move;
    int weight = 0;
};

class Book {
public:
    static Book& instance() {
        static Book book;
        return book;
    }

    // Legal book moves for this position, most-played first. Empty if the
    // position isn't in the book.
    std::vector<BookMove> moves(const chess::Board& board) const {
        std::vector<BookMove> result;
        const auto it = table_.find(board.hash());
        if (it == table_.end()) return result;

        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        for (const auto& entry : it->second)
            for (const auto& m : legal)
                if (m.move() == entry.move.move()) {  // guards against hash collisions
                    result.push_back(entry);
                    break;
                }
        std::sort(result.begin(), result.end(),
                  [](const BookMove& a, const BookMove& b) { return a.weight > b.weight; });
        return result;
    }

    // A weighted-random book move, or NO_MOVE if out of book.
    chess::Move probe(const chess::Board& board) {
        const auto candidates = moves(board);
        int total = 0;
        for (const auto& c : candidates) total += c.weight;
        if (total == 0) return chess::Move(chess::Move::NO_MOVE);

        int r = std::uniform_int_distribution<int>(0, total - 1)(rng_);
        for (const auto& c : candidates) {
            if (r < c.weight) return c.move;
            r -= c.weight;
        }
        return candidates.front().move;
    }

    std::size_t positions() const { return table_.size(); }

private:
    Book() : rng_(std::random_device{}()) {
        for (const std::string_view line : BookLines) {
            chess::Board board;
            std::istringstream is{std::string(line)};
            for (std::string tok; is >> tok;) {
                const chess::Move m = chess::uci::uciToMove(board, tok);
                if (m == chess::Move::NO_MOVE) break;  // lines are pre-validated; be safe anyway
                add(board.hash(), m);
                board.makeMove(m);
            }
        }
    }

    void add(std::uint64_t key, const chess::Move& m) {
        auto& list = table_[key];
        for (auto& entry : list)
            if (entry.move.move() == m.move()) {
                ++entry.weight;
                return;
            }
        list.push_back({m, 1});
    }

    std::unordered_map<std::uint64_t, std::vector<BookMove>> table_;
    std::mt19937 rng_;
};

}  // namespace book
