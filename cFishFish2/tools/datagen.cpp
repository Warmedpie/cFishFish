// datagen.cpp : self-play data generation for the Texel tuner.
//
// Plays fast games (fixed node budget per move) from random 8-ply openings
// and writes quiet positions labelled with the game result:
//     <FEN> | <result>        result from White's view: 1.0, 0.5 or 0.0
//
// "Quiet" = side to move not in check and the search's best move is not a
// capture or promotion, so the static eval is meaningful for the position.
// Opening plies (random moves) and positions with mate-range scores are
// skipped. Games are adjudicated once one side's score stays beyond
// +/-AdjudicateCp for several plies, or drawn after MaxPlies.
//
//   datagen <out.txt> <games> [nodes_per_move=8000] [seed=1]
//
// Portable C++ (single-threaded): run several copies with different seeds
// to use more cores, then concatenate the outputs.

#include <cstdio>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../cFishFish2/uci.h"

constexpr int OpeningPlies = 8;
constexpr int MaxPlies = 300;
constexpr int AdjudicateCp = 1200;
constexpr int AdjudicatePlies = 4;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: datagen <out.txt> <games> [nodes_per_move] [seed]\n");
        return 1;
    }
    const int games = std::atoi(argv[2]);
    const std::uint64_t nodes = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8000;
    std::mt19937 rng(argc > 4 ? static_cast<unsigned>(std::atoi(argv[4])) : 1u);
    std::ofstream out(argv[1], std::ios::app);

    search::TranspositionTable tt(8);
    long written = 0;

    for (int g = 0; g < games; ++g) {
        chess::Board board;
        bool ok = true;
        for (int i = 0; i < OpeningPlies && ok; ++i) {
            chess::Movelist ml;
            chess::movegen::legalmoves(ml, board);
            if (ml.empty()) ok = false;
            else board.makeMove(ml[static_cast<int>(rng() % static_cast<unsigned>(ml.size()))]);
        }
        if (!ok) continue;

        tt.clear();
        std::vector<std::string> fens;
        double result = 0.5;
        int streak_white = 0, streak_black = 0;

        for (int ply = 0; ply < MaxPlies; ++ply) {
            const auto [reason, res] = board.isGameOver();
            if (res != chess::GameResult::NONE) {
                if (res == chess::GameResult::DRAW) result = 0.5;
                else result = board.sideToMove() == chess::Color::WHITE ? 0.0 : 1.0;  // side to move lost
                break;
            }

            int score = 0;
            search::Hooks h;
            h.should_stop = [&](std::uint64_t n) { return n >= nodes; };
            h.start_next_iteration = [](int d) { return d < search::MaxPly - 1; };
            h.report = [&](int, int, int, const search::Line& l, std::uint64_t, int) { score = l.score; };
            auto searcher = std::make_unique<search::Searcher>(board, tt, std::move(h));
            const search::Result r = searcher->iterate({}, 1);
            if (r.best == chess::Move::NO_MOVE) break;

            const int white_score = board.sideToMove() == chess::Color::WHITE ? score : -score;
            const bool quiet = !board.inCheck() && !board.isCapture(r.best) &&
                               r.best.typeOf() != chess::Move::PROMOTION &&
                               r.best.typeOf() != chess::Move::ENPASSANT;
            if (quiet && std::abs(score) < search::Mate - search::MaxPly) fens.push_back(board.getFen());

            // Adjudication.
            streak_white = white_score >= AdjudicateCp ? streak_white + 1 : 0;
            streak_black = white_score <= -AdjudicateCp ? streak_black + 1 : 0;
            if (streak_white >= AdjudicatePlies) { result = 1.0; break; }
            if (streak_black >= AdjudicatePlies) { result = 0.0; break; }

            board.makeMove(r.best);
        }

        for (const auto& f : fens) out << f << " | " << result << '\n';
        written += static_cast<long>(fens.size());
        if ((g + 1) % 100 == 0) {
            std::fprintf(stderr, "games %d  positions %ld\n", g + 1, written);
            out.flush();
        }
    }
    std::fprintf(stderr, "done: %d games, %ld positions\n", games, written);
}
