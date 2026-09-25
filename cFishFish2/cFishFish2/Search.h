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
// late move reductions.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <functional>
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
    Searcher(const chess::Board& board, TranspositionTable& tt, Hooks hooks)
        : board_(board), tt_(tt), hooks_(std::move(hooks)) {}

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
        if (tt_.probe(key, entry)) {
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

        if (!pv_node && !in_check) {
            // Reverse futility pruning (static null move): at shallow depth,
            // if we are so far above beta that even losing margin * depth
            // wouldn't bring us back under it, assume the node fails high.
            if (depth <= RfpMaxDepth && !is_mate_score(beta) &&
                static_eval - RfpMargin * depth >= beta)
                return static_eval;

            // Null-move pruning: give the opponent a free move. If a reduced
            // search still fails high, a real move would too. Skipped without
            // pieces (pawn endgames), where zugzwang makes passing an
            // advantage and the assumption breaks.
            if (null_ok && depth >= NmpMinDepth && static_eval >= beta && !is_mate_score(beta) &&
                board_.hasNonPawnMaterial(board_.sideToMove())) {
                const int r = NmpBase + depth / NmpDiv;
                board_.makeNullMove();
                const Score score = -pvs(depth - 1 - r, ply + 1, -beta, -beta + 1, false);
                board_.unmakeNullMove();
                if (stopped_) return 0;
                if (score >= beta) return is_mate_score(score) ? beta : score;  // don't trust null-move mates
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

        for (int i = 0; i < list.size(); ++i) {
            const chess::Move move = list.next(i);
            if (ply == 0 && !root_move_allowed(move)) continue;
            const bool quiet = is_quiet(board_, move);

            board_.makeMove(move);
            const bool gives_check = board_.inCheck();
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
                        break;
                    }
                }
            }
            if (quiet && quiet_count < static_cast<int>(quiets_tried.size()))
                quiets_tried[static_cast<std::size_t>(quiet_count++)] = move;
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

            board_.makeMove(move);
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
        const chess::Color us = board_.sideToMove();
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
                score = (see_ge(board_, m, 0) ? OrderCapture : OrderBadCapture) + mvv_lva(board_, m);
            } else if (m == killers[0]) {
                score = OrderKiller1;
            } else if (m == killers[1]) {
                score = OrderKiller2;
            } else {
                score = history_.get(us, m);
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
        const chess::Color us = board_.sideToMove();
        const int bonus = depth * depth;
        history_.update(us, move, bonus);
        for (int i = 0; i < tried_count; ++i) history_.update(us, tried[static_cast<std::size_t>(i)], -bonus);
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
    History history_;

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
