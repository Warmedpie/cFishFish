// Search.h : Search for cFishFish.
//
// Iterative deepening -> Principal Variation Search (negamax alpha-beta)
// -> quiescence search at the leaves.
//
// Transposition table (TT.h): cutoffs in non-PV nodes.
// Move ordering: hash move (PV move at the root), then winning / equal
// captures by MVV-LVA, then killer moves, then quiet moves by history
// heuristic, then losing captures (by static exchange evaluation, SEE).
// Quiescence search: losing captures skipped, delta pruning.
// Selectivity: check extension, reverse futility pruning, null-move pruning,
// ProbCut, late move pruning, late move reductions. The last five use the
// "improving" flag (static eval better than on our previous move).

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Chess.h"
#include "Eval.h"
#include "TT.h"

namespace search {

using eval::Score;
using eval::Infinite;
using eval::Mate;

inline constexpr int MaxPly = 128;

// Mate scores are Mate - ply (we mate) or -Mate + ply (we get mated).
inline bool is_mate_score(Score s) { return std::abs(s) >= Mate - MaxPly; }

// Moves until mate for UCI "score mate N" (positive = we mate, negative = we
// get mated). Only meaningful when is_mate_score(s).
inline int mate_in_moves(Score s) {
    return s > 0 ? (Mate - s + 1) / 2 : -(Mate + s) / 2;
}

// Mate scores depend on the distance from the root, but a TT entry can be
// reached at a different ply. Store them relative to the node instead.
inline Score score_to_tt(Score s, int ply) {
    if (s >= Mate - MaxPly) return s + ply;
    if (s <= -(Mate - MaxPly)) return s - ply;
    return s;
}
inline Score score_from_tt(Score s, int ply) {
    if (s >= Mate - MaxPly) return s - ply;
    if (s <= -(Mate - MaxPly)) return s + ply;
    return s;
}

// ---------------------------------------------------------------------------
// Move ordering
// ---------------------------------------------------------------------------
// Order scores, highest searched first. Bands keep each class of move apart:
inline constexpr int OrderHash       = 2'000'000;  // TT move / previous PV move
inline constexpr int OrderCapture    = 1'000'000;  // + MVV-LVA; also queen promotions
inline constexpr int OrderKiller1    =   900'000;
inline constexpr int OrderBadCapture = -500'000;  // + MVV-LVA; captures that lose material
inline constexpr int OrderKiller2    =   800'000;
inline constexpr int MaxHistory      =    16'384;  // quiets: history in [-Max, Max]
inline constexpr int OrderUnderPromo = -1'000'000;  // under-promotions: almost never best

inline bool is_quiet(const chess::Board& board, const chess::Move& m) {
    return m.typeOf() != chess::Move::PROMOTION && m.typeOf() != chess::Move::ENPASSANT &&
           !board.isCapture(m);
}

// MVV-LVA: prefer taking the most valuable victim, then with the least
// valuable attacker (PxQ before QxQ before QxP).
inline int mvv_lva(const chess::Board& board, const chess::Move& m) {
    const int victim = m.typeOf() == chess::Move::ENPASSANT
                           ? eval::PawnValue
                           : eval::PieceValueMG[static_cast<std::size_t>(board.at(m.to()).type().internal())];
    const int attacker = static_cast<int>(board.at(m.from()).type().internal());  // P=0 ... K=5
    return victim * 8 - attacker;
}

// ---------------------------------------------------------------------------
// Static exchange evaluation (SEE)
// ---------------------------------------------------------------------------
// see_ge(board, move, threshold): does `move` win at least `threshold`
// centipawns once all captures on its target square are played out, each
// side always recapturing with its least valuable piece and free to stop?
// Includes x-rays (a rook behind a rook joins in once the front one moves).
// Pins are ignored. Castling, en passant and promotions count as 0.
inline eval::Score see_value(chess::PieceType pt) {
    constexpr std::array<eval::Score, 6> v = {
        eval::PawnValue, eval::KnightValue, eval::BishopValue,
        eval::RookValue, eval::QueenValue, 20000,  // king: capturing it ends the exchange
    };
    return v[static_cast<std::size_t>(pt.internal())];
}

// All pieces of both colours attacking `sq`, given occupancy `occ`.
inline chess::Bitboard attackers_to(const chess::Board& b, chess::Square sq, chess::Bitboard occ) {
    using chess::Color;
    using chess::PieceType;
    using at = chess::attacks;
    const chess::Bitboard queens = b.pieces(PieceType::QUEEN);
    return (at::pawn(Color::BLACK, sq) & b.pieces(PieceType::PAWN, Color::WHITE)) |
           (at::pawn(Color::WHITE, sq) & b.pieces(PieceType::PAWN, Color::BLACK)) |
           (at::knight(sq) & b.pieces(PieceType::KNIGHT)) |
           (at::bishop(sq, occ) & (b.pieces(PieceType::BISHOP) | queens)) |
           (at::rook(sq, occ) & (b.pieces(PieceType::ROOK) | queens)) |
           (at::king(sq) & b.pieces(PieceType::KING));
}

inline bool see_ge(const chess::Board& b, const chess::Move& m, eval::Score threshold) {
    using chess::Bitboard;
    using chess::PieceType;

    if (m.typeOf() != chess::Move::NORMAL) return 0 >= threshold;

    const chess::Square from = m.from(), to = m.to();

    // After our capture we are up `swap` relative to the threshold.
    eval::Score swap = (b.at(to) == chess::Piece::NONE ? 0 : see_value(b.at(to).type())) - threshold;
    if (swap < 0) return false;  // even keeping the victim isn't enough

    // If they recapture our piece for free, are we still at or above it?
    swap = see_value(b.at(from).type()) - swap;
    if (swap <= 0) return true;

    Bitboard occ = b.occ() ^ Bitboard::fromSquare(from) ^ Bitboard::fromSquare(to);
    chess::Color stm = b.at(from).color();
    Bitboard attackers = attackers_to(b, to, occ);
    const Bitboard diag = b.pieces(PieceType::BISHOP) | b.pieces(PieceType::QUEEN);
    const Bitboard orth = b.pieces(PieceType::ROOK) | b.pieces(PieceType::QUEEN);
    int res = 1;

    for (;;) {
        stm = ~stm;
        attackers &= occ;
        const Bitboard mine = attackers & b.us(stm);
        if (mine.empty()) break;
        res ^= 1;

        // Least valuable attacker of the side to move.
        auto take = [&](PieceType pt) -> bool {
            const Bitboard bb = mine & b.pieces(pt);
            if (bb.empty()) return false;
            swap = see_value(pt) - swap;
            occ ^= Bitboard::fromSquare(bb.lsb());
            return true;
        };
        if (take(PieceType::PAWN)) {
            if (swap < res) break;
            attackers |= chess::attacks::bishop(to, occ) & diag;
        } else if (take(PieceType::KNIGHT)) {
            if (swap < res) break;
        } else if (take(PieceType::BISHOP)) {
            if (swap < res) break;
            attackers |= chess::attacks::bishop(to, occ) & diag;
        } else if (take(PieceType::ROOK)) {
            if (swap < res) break;
            attackers |= chess::attacks::rook(to, occ) & orth;
        } else if (take(PieceType::QUEEN)) {
            if (swap < res) break;
            attackers |= (chess::attacks::bishop(to, occ) & diag) | (chess::attacks::rook(to, occ) & orth);
        } else {
            // King: it may only capture if the other side has no attackers left.
            return (attackers & ~b.us(stm)).empty() ? res : res ^ 1;
        }
    }
    return res != 0;
}

// History heuristic: quiet moves that caused beta cutoffs, by side/from/to.
// "Gravity" update keeps values within [-MaxHistory, MaxHistory] and lets
// old information fade as new results come in.
class History {
public:
    int get(chess::Color c, const chess::Move& m) const {
        return table_[static_cast<std::size_t>(c.internal())][sq(m.from())][sq(m.to())];
    }
    void update(chess::Color c, const chess::Move& m, int bonus) {
        int& h = table_[static_cast<std::size_t>(c.internal())][sq(m.from())][sq(m.to())];
        bonus = std::clamp(bonus, -MaxHistory, MaxHistory);
        h += bonus - h * std::abs(bonus) / MaxHistory;
    }

private:
    static std::size_t sq(chess::Square s) { return static_cast<std::size_t>(s.index()); }
    std::array<std::array<std::array<int, 64>, 64>, 2> table_{};
};

// Same gravity update for the 16-bit tables below.
inline void update_stat(std::int16_t& h, int bonus) {
    bonus = std::clamp(bonus, -MaxHistory, MaxHistory);
    const int v = h;
    h = static_cast<std::int16_t>(v + bonus - v * std::abs(bonus) / MaxHistory);
}

// (piece, to-square) key for a move: 0..767, or -1 for "no move" (null move,
// root). Castling uses the king's from/to encoding of the library, which
// is consistent, so it works as a key too.
inline int piece_to(const chess::Board& b, const chess::Move& m) {
    return static_cast<int>(b.at(m.from()).internal()) * 64 + m.to().index();
}

// All move-ordering statistics. Owned by the UCI layer so they survive from
// one move of the game to the next (cleared on ucinewgame); a Searcher made
// without one creates its own.
struct OrderTables {
    History main;  // [color][from][to]
    // Capture history: [moving piece][to][captured type]. Refines MVV-LVA by
    // which captures actually worked in this game.
    std::array<std::array<std::array<std::int16_t, 6>, 64>, 12> capture{};
    // Continuation history: [previous move's piece-to][this move's piece-to].
    // "After the opponent played Nf6, h3 tends to be good." Used with the
    // moves 1 ply ago (counter-move history) and 2 plies ago (follow-up).
    std::vector<std::int16_t> cont = std::vector<std::int16_t>(768 * 768);
    // Countermove: the quiet move that last refuted the opponent's piece-to.
    std::array<chess::Move, 768> counter{};

    std::int16_t& cont_at(int prev, int cur) { return cont[static_cast<std::size_t>(prev * 768 + cur)]; }
    int cont_get(int prev, int cur) const {
        return prev < 0 ? 0 : cont[static_cast<std::size_t>(prev * 768 + cur)];
    }
    std::int16_t& capture_at(const chess::Board& b, const chess::Move& m) {
        const int victim = m.typeOf() == chess::Move::ENPASSANT ? 0 : static_cast<int>(b.at(m.to()).type().internal());
        return capture[static_cast<std::size_t>(b.at(m.from()).internal())][static_cast<std::size_t>(m.to().index())]
                      [static_cast<std::size_t>(victim)];
    }
    int capture_get(const chess::Board& b, const chess::Move& m) const {
        return const_cast<OrderTables*>(this)->capture_at(b, m);
    }
};

// Moves plus their order scores. next() does one step of selection sort:
// only as much sorting as the search actually uses before a cutoff.
struct OrderedMoves {
    chess::Movelist moves;
    std::array<int, 256> scores{};

    int size() const { return static_cast<int>(moves.size()); }

    chess::Move next(int i) {
        int best = i;
        for (int j = i + 1; j < size(); ++j)
            if (scores[static_cast<std::size_t>(j)] > scores[static_cast<std::size_t>(best)]) best = j;
        std::swap(moves[i], moves[best]);
        std::swap(scores[static_cast<std::size_t>(i)], scores[static_cast<std::size_t>(best)]);
        return moves[i];
    }
};

// ---------------------------------------------------------------------------
// Pruning / reduction parameters (untuned starting points, in centipawns
// where applicable). Tune these with SPRT games, not by eye.
// ---------------------------------------------------------------------------

// Reverse futility pruning: at depth <= RfpMaxDepth, a non-PV node whose
// static eval beats beta by RfpMargin per ply of depth is cut off.
inline constexpr int RfpMaxDepth = 8;
inline constexpr int RfpMargin   = 80;

// Null-move pruning: from this depth, reduction R = NmpBase + depth / NmpDiv.
inline constexpr int NmpMinDepth = 3;
inline constexpr int NmpBase     = 3;
inline constexpr int NmpDiv      = 6;

// ---- "Improving" and the heuristics that use it ----------------------------
// improving = our static eval is higher than it was two plies ago (our
// previous move). If improving, the position is trending our way, so we can
// prune more aggressively toward fail-highs and reduce less.
// The SR_* macros exist so A/B test builds can flip features from the
// command line; normal builds use the defaults.
//
// Timed A/B results (25 ms/move with 5 ms overhead, vs. the version before):
//   LMP, Stockfish limit (3+d^2)/(2-imp)     -250-ish (aborted), with checks kept: similar
//   LMP, same limit, depth <= 3 only           -32  (400 games)
//   LMP, limit x2, checks kept                 +23  (400, LOS 95%)   ON
//   then, each on top of that LMP:
//   RFP margin by improving                     -2  (600)            off
//   LMR +1 when not improving                   +3  (600)            off
//   NMP below beta when improving               -1  (600)            off
//   ProbCut                                     -4  (600)            off
//   all four together                           +7  (800, LOS 77%)   off: not proven
// The four are standard small gains in strong engines; they need an SPRT
// run of thousands of games to resolve. Aggressive LMP likely fails here
// because quiet-move ordering is still basic (no continuation history).
#ifndef SR_IMP_RFP
#define SR_IMP_RFP 0
#endif
#ifndef SR_LMP
#define SR_LMP 1
#endif
#ifndef SR_IMP_LMR
#define SR_IMP_LMR 0
#endif
#ifndef SR_IMP_NMP
#define SR_IMP_NMP 0
#endif
#ifndef SR_PROBCUT
#define SR_PROBCUT 0
#endif
#ifndef SR_LMP_KEEP_CHECKS
#define SR_LMP_KEEP_CHECKS 1
#endif
inline constexpr bool UseImprovingRfp = SR_IMP_RFP;  // RFP margin: RfpMargin * (depth - improving)
inline constexpr bool UseLmp          = SR_LMP;      // late move pruning, limit depends on improving
inline constexpr bool UseImprovingLmr = SR_IMP_LMR;  // +1 reduction when not improving
inline constexpr bool UseImprovingNmp = SR_IMP_NMP;  // NMP also when eval >= beta - margin and improving
inline constexpr bool UseProbCut      = SR_PROBCUT;  // ProbCut, beta margin lower when improving
inline constexpr bool LmpKeepChecks   = SR_LMP_KEEP_CHECKS;  // LMP never skips a move that gives check

// Late move pruning: at depth <= LmpMaxDepth, quiet moves after the first
// (3 + depth^2) / (2 - improving) are skipped.
#ifndef SR_LMP_MAX_DEPTH
#define SR_LMP_MAX_DEPTH 8
#endif
#ifndef SR_LMP_SCALE
#define SR_LMP_SCALE 2
#endif
inline constexpr int LmpMaxDepth = SR_LMP_MAX_DEPTH;
inline int lmp_limit(int depth, bool improving) {
    return SR_LMP_SCALE * (3 + depth * depth) / (2 - (improving ? 1 : 0));
}

// ---- Move ordering statistics ------------------------------------------------
// Timed A/B results (25 ms/move, 600 games each):
//   vs. the version before (fresh, small-bonus history every move):
//     capture history                           -7            off
//     continuation history (1 and 2 ply)        +5
//     countermove                               +9
//     history bonus 16d^2+32d instead of d^2    +6
//     keep tables between moves                +28 (LOS 99%)  ON
//   then vs. keep-tables:
//     continuation history + countermove        -7  (with the small d^2 bonus)
//     big bonus                                +21 (LOS 97%)  ON
//     big bonus + cont + counter + history LMR +37 (LOS 99.9%) ON: the package
//     package vs. big bonus alone              +13 (LOS 87%)
//     package + capture history                 -7            off
// Continuation history and history-LMR need the larger bonus: with d^2 the
// values stay too small to matter. Total over the old ordering: about +65.
#ifndef SR_CAPT_HIST
#define SR_CAPT_HIST 0
#endif
#ifndef SR_CONT_HIST
#define SR_CONT_HIST 1
#endif
#ifndef SR_COUNTER
#define SR_COUNTER 1
#endif
#ifndef SR_HIST_LMR
#define SR_HIST_LMR 1
#endif
#ifndef SR_BIG_BONUS
#define SR_BIG_BONUS 1
#endif
#ifndef SR_KEEP_HIST
#define SR_KEEP_HIST 1
#endif
inline constexpr bool UseCaptureHistory = SR_CAPT_HIST;  // capture history on top of MVV
inline constexpr bool UseContHistory    = SR_CONT_HIST;  // 1- and 2-ply continuation history
inline constexpr bool UseCountermove    = SR_COUNTER;    // countermove after the killers
inline constexpr bool UseHistoryLmr     = SR_HIST_LMR;   // reduce good-history quiets less
inline constexpr bool KeepHistory       = SR_KEEP_HIST;  // tables survive between moves
inline constexpr bool UseBigBonus       = SR_BIG_BONUS;  // history bonus 16d^2+32d (cap 1600) instead of d^2
inline constexpr int OrderCounter    = 700'000;

// History bonus for a cutoff at `depth` (penalty = the negative).
inline int history_bonus(int depth) {
    return UseBigBonus ? std::min(16 * depth * depth + 32 * depth, 1600) : depth * depth;
}
#ifndef SR_HIST_LMR_DIV
#define SR_HIST_LMR_DIV 4096
#endif
inline constexpr int HistLmrDivisor  = SR_HIST_LMR_DIV;  // history units per ply of LMR

inline constexpr int NmpImprovingMargin = 40;

// ProbCut: at depth >= ProbCutMinDepth, a capture that beats
// beta + ProbCutMargin (- ProbCutImproving if improving) in a search
// reduced by ProbCutReduction probably beats beta at full depth too.
inline constexpr int ProbCutMinDepth  = 5;
inline constexpr int ProbCutReduction = 4;
inline constexpr int ProbCutMargin    = 200;
inline constexpr int ProbCutImproving = 50;

// Delta pruning (quiescence search): skip a capture if stand pat + the
// captured piece's value + DeltaMargin still can't reach alpha.
inline constexpr int DeltaMargin = 200;

// Late move reductions: from this depth, quiet moves after the first
// LmrMinMoves moves are reduced by the log formula below.
inline constexpr int LmrMinDepth = 3;
inline constexpr int LmrMinMoves = 3;
inline constexpr double LmrBase    = 0.75;
inline constexpr double LmrDivisor = 2.25;

// reduction = LmrBase + ln(depth) * ln(move number) / LmrDivisor
inline int lmr_reduction(int depth, int move_number) {
    static const auto table = [] {
        std::array<std::array<int, 64>, 64> t{};
        for (int d = 1; d < 64; ++d)
            for (int m = 1; m < 64; ++m)
                t[static_cast<std::size_t>(d)][static_cast<std::size_t>(m)] =
                    static_cast<int>(LmrBase + std::log(d) * std::log(m) / LmrDivisor);
        return t;
    }();
    return table[static_cast<std::size_t>(std::min(depth, 63))][static_cast<std::size_t>(std::min(move_number, 63))];
}

// One completed MultiPV line at some depth.
struct Line {
    Score score = 0;
    std::vector<chess::Move> pv;
};

// How the search talks to the outside world (the UCI layer).
struct Hooks {
    // Hard stop, polled every 2048 nodes: GUI stop, node limit, hard time.
    std::function<bool(std::uint64_t nodes)> should_stop;
    // Soft stop, checked before each new depth: depth limit, soft time.
    std::function<bool(int depth)> start_next_iteration;
    // Called once per completed line: depth, seldepth, multipv index (1-based),
    // line, total nodes, hashfull (permille).
    std::function<void(int, int, int, const Line&, std::uint64_t, int)> report;
};

struct Result {
    chess::Move best = chess::Move(chess::Move::NO_MOVE);
    chess::Move ponder = chess::Move(chess::Move::NO_MOVE);
};

class Searcher {
public:
    // `tables`: move-ordering statistics to use and keep updating (the UCI
    // layer passes its own so they carry over between moves); nullptr = fresh.
    Searcher(const chess::Board& board, TranspositionTable& tt, Hooks hooks, OrderTables* tables = nullptr)
        : board_(board), tt_(tt), hooks_(std::move(hooks)),
          own_tables_(tables ? nullptr : std::make_unique<OrderTables>()),
          ot_(tables ? *tables : *own_tables_) {
        moved_.fill(-1);
    }

    // Iterative deepening at the root.
    // `allowed` : root moves to consider (UCI searchmoves); empty = all legal.
    // `multipv` : number of best lines to find at each depth.
    Result iterate(const std::vector<chess::Move>& allowed, int multipv) {
        Result result;

        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board_);
        for (const auto& m : legal)
            if (allowed.empty() || std::find(allowed.begin(), allowed.end(), m) != allowed.end())
                root_moves_.push_back(m);

        if (root_moves_.empty()) return result;  // mate / stalemate

        // Fallback so we always return a legal move, even if stopped at depth 1.
        result.best = root_moves_.front();
        const int lines = std::clamp(multipv, 1, static_cast<int>(root_moves_.size()));

        for (int depth = 1; depth < MaxPly && hooks_.start_next_iteration(depth); ++depth) {
            excluded_.clear();
            std::vector<Line> completed;

            // MultiPV: search the root once per line, excluding the best moves
            // already found at this depth.
            for (int k = 1; k <= lines; ++k) {
                seldepth_ = 0;
                const Score score = pvs(depth, 0, -Infinite, Infinite, false);
                if (stopped_) break;  // partial result: discard

                Line line{score, {pv_[0].begin(), pv_[0].begin() + pv_len_[0]}};
                excluded_.push_back(line.pv.front());
                hooks_.report(depth, seldepth_, k, line, nodes_, tt_.hashfull());
                completed.push_back(std::move(line));
            }

            // Line 1 is the best move. Use it if it finished, even if a later
            // MultiPV line of this depth was cut off.
            if (!completed.empty()) {
                const auto& pv = completed.front().pv;
                result.best = pv[0];
                pv_move_ = pv[0];  // searched first at the root next iteration
                result.ponder = pv.size() > 1 ? pv[1] : chess::Move(chess::Move::NO_MOVE);
            }
            if (stopped_) break;
        }
        return result;
    }

    std::uint64_t nodes() const { return nodes_; }

private:
    // Principal Variation Search.
    // First move: full window. Remaining moves: null window (alpha, alpha+1)
    // to prove they are no better; re-search with the full window if one is.
    // `null_ok`: false right after a null move (no two null moves in a row).
    Score pvs(int depth, int ply, Score alpha, Score beta, bool null_ok) {
        pv_len_[ply] = ply;

        // Check extension: never stop searching while in check.
        const bool in_check = board_.inCheck();
        if (in_check) ++depth;

        if (depth <= 0) return qsearch(ply, alpha, beta);

        if (check_time()) return 0;
        seldepth_ = std::max(seldepth_, ply);

        if (ply > 0 && is_draw()) return 0;
        if (ply >= MaxPly - 1) return eval::evaluate(board_);

        const bool pv_node = (beta - alpha > 1);
        const Score alpha_orig = alpha;
        const std::uint64_t key = board_.hash();

        // TT probe. Cut off only in non-PV nodes so the PV stays complete.
        chess::Move tt_move(chess::Move::NO_MOVE);
        TTEntry entry;
        const bool tt_hit = tt_.probe(key, entry);
        if (tt_hit) {
            tt_move = chess::Move(entry.move);
            if (!pv_node && entry.depth >= depth) {
                const Score s = score_from_tt(entry.score, ply);
                if (entry.bound == Bound::Exact ||
                    (entry.bound == Bound::Lower && s >= beta) ||
                    (entry.bound == Bound::Upper && s <= alpha))
                    return s;
            }
        }

        // Static evaluation, used by the pruning below. Not meaningful in check.
        const Score static_eval = in_check ? -Infinite : eval::evaluate(board_);

        // Improving: is our eval better than on our previous move (two plies
        // ago)? Falls back to four plies if that position was in check.
        eval_stack_[static_cast<std::size_t>(ply)] = in_check ? NoEval : static_eval;
        bool improving = false;
        if (!in_check) {
            if (ply >= 2 && eval_stack_[static_cast<std::size_t>(ply - 2)] != NoEval)
                improving = static_eval > eval_stack_[static_cast<std::size_t>(ply - 2)];
            else if (ply >= 4 && eval_stack_[static_cast<std::size_t>(ply - 4)] != NoEval)
                improving = static_eval > eval_stack_[static_cast<std::size_t>(ply - 4)];
            else
                improving = true;
        }

        if (!pv_node && !in_check) {
            // Reverse futility pruning (static null move): at shallow depth,
            // if we are so far above beta that even losing margin * depth
            // wouldn't bring us back under it, assume the node fails high.
            // When improving, a smaller margin is enough.
            const int rfp_depth = depth - (UseImprovingRfp && improving ? 1 : 0);
            if (depth <= RfpMaxDepth && !is_mate_score(beta) &&
                static_eval - RfpMargin * rfp_depth >= beta)
                return static_eval;

            // Null-move pruning: give the opponent a free move. If a reduced
            // search still fails high, a real move would too. Skipped without
            // pieces (pawn endgames), where zugzwang makes passing an
            // advantage and the assumption breaks. When improving, also tried
            // with the eval slightly below beta.
            const bool nmp_eval_ok =
                static_eval >= beta ||
                (UseImprovingNmp && improving && static_eval >= beta - NmpImprovingMargin);
            if (null_ok && depth >= NmpMinDepth && nmp_eval_ok && !is_mate_score(beta) &&
                board_.hasNonPawnMaterial(board_.sideToMove())) {
                const int r = NmpBase + depth / NmpDiv;
                moved_[static_cast<std::size_t>(ply)] = -1;
                board_.makeNullMove();
                const Score score = -pvs(depth - 1 - r, ply + 1, -beta, -beta + 1, false);
                board_.unmakeNullMove();
                if (stopped_) return 0;
                if (score >= beta) return is_mate_score(score) ? beta : score;  // don't trust null-move mates
            }

            // ProbCut: if a good capture beats a raised beta in a much
            // shallower search, it very likely beats beta at full depth.
            if (UseProbCut && depth >= ProbCutMinDepth && !is_mate_score(beta)) {
                const Score pc_beta = beta + ProbCutMargin - (improving ? ProbCutImproving : 0);
                // Skip if the TT already says this position is below pc_beta.
                const bool tt_says_low = tt_hit && entry.depth >= depth - ProbCutReduction + 1 &&
                                         score_from_tt(entry.score, ply) < pc_beta;
                if (!tt_says_low) {
                    OrderedMoves caps;
                    chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(caps.moves, board_);
                    score_moves(caps, tt_move, ply);
                    for (int i = 0; i < caps.size(); ++i) {
                        const chess::Move move = caps.next(i);
                        if (!see_ge(board_, move, pc_beta - static_eval)) continue;
                        make(move, ply);
                        Score score = -qsearch(ply + 1, -pc_beta, -pc_beta + 1);
                        if (score >= pc_beta)
                            score = -pvs(depth - 1 - ProbCutReduction, ply + 1, -pc_beta, -pc_beta + 1, true);
                        board_.unmakeMove(move);
                        if (stopped_) return 0;
                        if (score >= pc_beta) {
                            tt_.store(key, move, score_to_tt(score, ply), depth - ProbCutReduction, Bound::Lower);
                            return score;
                        }
                    }
                }
            }
        }

        // PV move: at the root, the previous iteration's best move is
        // searched first even if its TT entry has been overwritten.
        if (ply == 0 && tt_move == chess::Move::NO_MOVE) tt_move = pv_move_;

        OrderedMoves list;
        chess::movegen::legalmoves(list.moves, board_);

        if (list.moves.empty())
            return board_.inCheck() ? -Mate + ply : 0;  // checkmate : stalemate

        score_moves(list, tt_move, ply);

        Score best = -Infinite;
        chess::Move best_move(chess::Move::NO_MOVE);
        int moves_searched = 0;

        // Quiet moves searched so far that didn't cause a cutoff: they get a
        // history penalty if a later quiet move does.
        std::array<chess::Move, 64> quiets_tried;
        int quiet_count = 0;
        std::array<chess::Move, 32> captures_tried;
        int capture_count = 0;

        for (int i = 0; i < list.size(); ++i) {
            const chess::Move move = list.next(i);
            if (ply == 0 && !root_move_allowed(move)) continue;
            const bool quiet = is_quiet(board_, move);

            // Late move pruning: at shallow depth, once enough moves have been
            // searched, skip the remaining quiet moves (ordered last, rarely
            // best). Fewer are searched when not improving.
            const bool lmp_skip = UseLmp && ply > 0 && !in_check && quiet && depth <= LmpMaxDepth &&
                                  best > -(Mate - MaxPly) && moves_searched >= lmp_limit(depth, improving);
            if (lmp_skip && !LmpKeepChecks) continue;

            const int move_hist = quiet ? quiet_history(move, ply) : 0;  // before the move is made
            make(move, ply);
            const bool gives_check = board_.inCheck();
            if (lmp_skip && !gives_check) {  // (LmpKeepChecks) checks are still searched
                board_.unmakeMove(move);
                continue;
            }
            const int new_depth = depth - 1;
            Score score;
            if (moves_searched == 0) {
                score = -pvs(new_depth, ply + 1, -beta, -alpha, true);
            } else {
                // Late move reductions: well-ordered moves late in the list
                // rarely matter, so search them shallower first and only
                // re-search at full depth if they beat alpha.
                int r = 0;
                if (depth >= LmrMinDepth && moves_searched >= LmrMinMoves && quiet && !in_check &&
                    !gives_check) {
                    r = lmr_reduction(depth, moves_searched + 1);
                    if (pv_node) --r;
                    if (UseImprovingLmr && !improving) ++r;
                    if (UseHistoryLmr) r -= move_hist / HistLmrDivisor;
                    r = std::clamp(r, 0, new_depth - 1);
                }

                score = -pvs(new_depth - r, ply + 1, -alpha - 1, -alpha, true);
                if (score > alpha && r > 0)  // reduced search surprised us
                    score = -pvs(new_depth, ply + 1, -alpha - 1, -alpha, true);
                if (score > alpha && score < beta)  // PVS re-search, full window
                    score = -pvs(new_depth, ply + 1, -beta, -alpha, true);
            }
            board_.unmakeMove(move);
            ++moves_searched;

            if (stopped_) return 0;

            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    best_move = move;
                    update_pv(ply, move);
                    if (alpha >= beta) {  // fail high (beta cutoff)
                        if (quiet) on_quiet_cutoff(move, ply, depth, quiets_tried, quiet_count);
                        else if (UseCaptureHistory && is_capture_stat(move))
                            update_stat(ot_.capture_at(board_, move), history_bonus(depth));
                        if (UseCaptureHistory)  // captures that were tried first didn't work
                            for (int k = 0; k < capture_count; ++k)
                                update_stat(ot_.capture_at(board_, captures_tried[static_cast<std::size_t>(k)]),
                                            -history_bonus(depth));
                        break;
                    }
                }
            }
            if (quiet && quiet_count < static_cast<int>(quiets_tried.size()))
                quiets_tried[static_cast<std::size_t>(quiet_count++)] = move;
            else if (!quiet && is_capture_stat(move) && capture_count < static_cast<int>(captures_tried.size()))
                captures_tried[static_cast<std::size_t>(capture_count++)] = move;
        }

        // Don't store the root while excluding MultiPV moves: that score is
        // for "best move except the ones already found", not the position.
        if (!(ply == 0 && !excluded_.empty())) {
            const Bound bound = best >= beta        ? Bound::Lower
                              : best > alpha_orig   ? Bound::Exact
                                                    : Bound::Upper;
            tt_.store(key, best_move, score_to_tt(best, ply), depth, bound);
        }
        return best;
    }

    // Quiescence search: keep searching captures until the position is quiet,
    // so the static eval isn't taken in the middle of an exchange.
    Score qsearch(int ply, Score alpha, Score beta) {
        pv_len_[ply] = ply;

        if (check_time()) return 0;
        seldepth_ = std::max(seldepth_, ply);

        if (is_draw()) return 0;
        if (ply >= MaxPly - 1) return eval::evaluate(board_);

        const bool pv_node = (beta - alpha > 1);
        const Score alpha_orig = alpha;
        const std::uint64_t key = board_.hash();

        // Any stored result (depth >= 0) is at least as good as a qsearch.
        TTEntry entry;
        if (!pv_node && tt_.probe(key, entry)) {
            const Score s = score_from_tt(entry.score, ply);
            if (entry.bound == Bound::Exact ||
                (entry.bound == Bound::Lower && s >= beta) ||
                (entry.bound == Bound::Upper && s <= alpha))
                return s;
        }

        const bool in_check = board_.inCheck();
        OrderedMoves list;
        Score best;
        Score stand_pat = -Infinite;

        if (in_check) {
            // No standing pat in check: every evasion must be tried.
            chess::movegen::legalmoves(list.moves, board_);
            if (list.moves.empty()) return -Mate + ply;
            best = -Infinite;
        } else {
            // Stand pat: the side to move can usually do at least as well as
            // the static eval by making a quiet move.
            best = stand_pat = eval::evaluate(board_);
            if (best >= beta) {
                tt_.store(key, chess::Move(chess::Move::NO_MOVE), score_to_tt(best, ply), 0,
                          Bound::Lower);
                return best;
            }
            alpha = std::max(alpha, best);
            chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(list.moves, board_);
        }
        score_moves(list, chess::Move(chess::Move::NO_MOVE), ply);

        chess::Move best_move(chess::Move::NO_MOVE);
        for (int i = 0; i < list.size(); ++i) {
            const chess::Move move = list.next(i);

            if (!in_check) {
                // Losing captures (SEE < 0) and under-promotions are ordered
                // last, with negative scores: from the first one on, stop.
                if (list.scores[static_cast<std::size_t>(i)] < 0) break;

                // Delta pruning: even winning the captured piece outright
                // (plus a safety margin) wouldn't lift us to alpha.
                if (move.typeOf() != chess::Move::PROMOTION) {
                    const Score gain = move.typeOf() == chess::Move::ENPASSANT
                                           ? eval::PawnValue
                                           : see_value(board_.at(move.to()).type());
                    if (stand_pat + gain + DeltaMargin <= alpha) continue;
                }
            }

            make(move, ply);
            const Score score = -qsearch(ply + 1, -beta, -alpha);
            board_.unmakeMove(move);

            if (stopped_) return 0;

            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    best_move = move;
                    if (alpha >= beta) break;
                }
            }
        }

        const Bound bound = best >= beta      ? Bound::Lower
                          : best > alpha_orig ? Bound::Exact
                                              : Bound::Upper;
        tt_.store(key, best_move, score_to_tt(best, ply), 0, bound);
        return best;
    }

    // Assign order scores to every move in the list.
    void score_moves(OrderedMoves& list, const chess::Move& tt_move, int ply) const {
        const auto& killers = killers_[static_cast<std::size_t>(ply)];
        for (int i = 0; i < list.size(); ++i) {
            const chess::Move m = list.moves[i];
            int score;
            if (m == tt_move) {
                score = OrderHash;
            } else if (m.typeOf() == chess::Move::PROMOTION) {
                const bool queen = m.promotionType() == chess::PieceType::QUEEN;
                const int capture = board_.isCapture(m) ? mvv_lva(board_, m) : 0;
                score = queen ? OrderCapture + eval::QueenValue * 8 + capture
                              : OrderUnderPromo + capture;
            } else if (!is_quiet(board_, m)) {
                const int base = see_ge(board_, m, 0) ? OrderCapture : OrderBadCapture;
                if (UseCaptureHistory) {
                    const int victim = m.typeOf() == chess::Move::ENPASSANT
                                           ? eval::PawnValue
                                           : eval::PieceValueMG[static_cast<std::size_t>(board_.at(m.to()).type().internal())];
                    score = base + victim * 8 + ot_.capture_get(board_, m) / 16;
                } else {
                    score = base + mvv_lva(board_, m);
                }
            } else if (m == killers[0]) {
                score = OrderKiller1;
            } else if (m == killers[1]) {
                score = OrderKiller2;
            } else if (UseCountermove && ply > 0 && moved_[static_cast<std::size_t>(ply - 1)] >= 0 &&
                       m == ot_.counter[static_cast<std::size_t>(moved_[static_cast<std::size_t>(ply - 1)])]) {
                score = OrderCounter;
            } else {
                score = quiet_history(m, ply);
            }
            list.scores[static_cast<std::size_t>(i)] = score;
        }
    }

    // A quiet move caused a beta cutoff: remember it as a killer for this
    // ply, reward it in the history table, and penalize the quiet moves
    // searched before it that failed to cut off.
    void on_quiet_cutoff(const chess::Move& move, int ply, int depth,
                         const std::array<chess::Move, 64>& tried, int tried_count) {
        auto& killers = killers_[static_cast<std::size_t>(ply)];
        if (killers[0] != move) {
            killers[1] = killers[0];
            killers[0] = move;
        }
        const int bonus = history_bonus(depth);
        update_quiet(move, ply, bonus);
        for (int i = 0; i < tried_count; ++i) update_quiet(tried[static_cast<std::size_t>(i)], ply, -bonus);
        if (UseCountermove && ply > 0 && moved_[static_cast<std::size_t>(ply - 1)] >= 0)
            ot_.counter[static_cast<std::size_t>(moved_[static_cast<std::size_t>(ply - 1)])] = move;
    }

    // Piece-to keys of the moves 1 and 2 plies before `ply` (-1 if none).
    int prev_move(int ply, int back) const {
        return ply >= back ? moved_[static_cast<std::size_t>(ply - back)] : -1;
    }

    // Order score of a quiet move: butterfly history plus, if enabled, the
    // continuation history after the last two moves.
    int quiet_history(const chess::Move& m, int ply) const {
        int h = ot_.main.get(board_.sideToMove(), m);
        if (UseContHistory) {
            const int pt = piece_to(board_, m);
            h += ot_.cont_get(prev_move(ply, 1), pt) + ot_.cont_get(prev_move(ply, 2), pt);
        }
        return h;
    }

    void update_quiet(const chess::Move& m, int ply, int bonus) {
        ot_.main.update(board_.sideToMove(), m, bonus);
        if (UseContHistory) {
            const int pt = piece_to(board_, m);
            for (int back = 1; back <= 2; ++back) {
                const int prev = prev_move(ply, back);
                if (prev >= 0) update_stat(ot_.cont_at(prev, pt), bonus);
            }
        }
    }

    // Captures tracked by capture history (not promotions: those have their
    // own band and piece type changes).
    static bool is_capture_stat(const chess::Move& m) { return m.typeOf() != chess::Move::PROMOTION; }

    // Make a move and remember its piece-to key for continuation history.
    void make(const chess::Move& m, int ply) {
        moved_[static_cast<std::size_t>(ply)] = piece_to(board_, m);
        board_.makeMove(m);
    }

    bool check_time() {
        if ((++nodes_ & 2047) == 0 && hooks_.should_stop(nodes_)) stopped_ = true;
        return stopped_;
    }

    // Repetition (a single repeat counts), fifty-move rule, dead material.
    bool is_draw() const {
        return board_.isRepetition(1) || board_.isHalfMoveDraw() || board_.isInsufficientMaterial();
    }

    bool root_move_allowed(const chess::Move& m) const {
        return std::find(root_moves_.begin(), root_moves_.end(), m) != root_moves_.end() &&
               std::find(excluded_.begin(), excluded_.end(), m) == excluded_.end();
    }

    // Triangular PV table: pv_[ply] holds the best line starting at ply.
    void update_pv(int ply, const chess::Move& move) {
        pv_[ply][ply] = move;
        for (int i = ply + 1; i < pv_len_[ply + 1]; ++i) pv_[ply][i] = pv_[ply + 1][i];
        pv_len_[ply] = std::max(pv_len_[ply + 1], ply + 1);
    }

    eval::EvalBoard board_;  // keeps its evaluation up to date as moves are made
    TranspositionTable& tt_;
    Hooks hooks_;

    std::vector<chess::Move> root_moves_;
    std::vector<chess::Move> excluded_;  // MultiPV: root moves already used
    chess::Move pv_move_ = chess::Move(chess::Move::NO_MOVE);  // best root move so far

    // Killer moves: two quiet moves per ply that recently caused cutoffs.
    std::array<std::array<chess::Move, 2>, MaxPly + 1> killers_{};
    std::unique_ptr<OrderTables> own_tables_;  // only when none was passed in
    OrderTables& ot_;
    // moved_[ply]: piece-to key of the move made at `ply` (-1 = null move).
    std::array<int, MaxPly + 1> moved_{};

    // Static eval per ply, for the "improving" flag (NoEval when in check).
    static constexpr Score NoEval = Infinite + 1;
    std::array<Score, MaxPly + 1> eval_stack_{};

    std::array<std::array<chess::Move, MaxPly + 1>, MaxPly + 1> pv_{};
    std::array<int, MaxPly + 1> pv_len_{};

    std::uint64_t nodes_ = 0;
    int seldepth_ = 0;
    bool stopped_ = false;
};

// perft: counts leaf nodes to verify move generation against known values.
inline std::uint64_t perft(chess::Board& board, int depth) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    if (depth <= 1) return static_cast<std::uint64_t>(moves.size());
    std::uint64_t total = 0;
    for (const auto& m : moves) {
        board.makeMove(m);
        total += perft(board, depth - 1);
        board.unmakeMove(m);
    }
    return total;
}

}  // namespace search
