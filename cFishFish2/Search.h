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
#include <bit>
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

// Does `m` give check? Direct attack from the destination, or a discovered
// attack by one of our sliders. Castling and en passant: make the move.
inline bool gives_check(const chess::Board& b, const chess::Move& m) {
    using chess::PieceType;
    if (m.typeOf() == chess::Move::CASTLING || m.typeOf() == chess::Move::ENPASSANT) {
        chess::Board c = b;
        c.makeMove(m);
        return c.inCheck();
    }
    const chess::Color us = b.sideToMove();
    const chess::Square ksq = b.kingSq(~us);
    const std::uint64_t k = 1ULL << ksq.index();
    const std::uint64_t from = 1ULL << m.from().index(), to = 1ULL << m.to().index();
    const chess::Bitboard occ((b.occ().getBits() & ~from) | to);
    const PieceType pt = m.typeOf() == chess::Move::PROMOTION ? m.promotionType() : b.at(m.from()).type();
    std::uint64_t direct = 0;
    if (pt == PieceType::PAWN) direct = chess::attacks::pawn(us, m.to()).getBits();
    else if (pt == PieceType::KNIGHT) direct = chess::attacks::knight(m.to()).getBits();
    else if (pt == PieceType::BISHOP) direct = chess::attacks::bishop(m.to(), occ).getBits();
    else if (pt == PieceType::ROOK) direct = chess::attacks::rook(m.to(), occ).getBits();
    else if (pt == PieceType::QUEEN) direct = chess::attacks::queen(m.to(), occ).getBits();
    if (direct & k) return true;
    const std::uint64_t diag = (b.pieces(PieceType::BISHOP, us) | b.pieces(PieceType::QUEEN, us)).getBits() & ~from;
    const std::uint64_t orth = (b.pieces(PieceType::ROOK, us) | b.pieces(PieceType::QUEEN, us)).getBits() & ~from;
    return (chess::attacks::bishop(ksq, occ).getBits() & diag) || (chess::attacks::rook(ksq, occ).getBits() & orth);
}

// Hash of a bitboard set, for the correction-history tables.
inline std::uint64_t mix_key(std::uint64_t a, std::uint64_t b, std::uint64_t c = 0) {
    std::uint64_t h = a * 0x9E3779B97F4A7C15ULL ^ (b + 0x632BE59BD9B4E019ULL) * 0xC2B2AE3D27D4EB4FULL ^
                      (c + 0x85EBCA77C2B2AE63ULL) * 0x165667B19E3779F9ULL;
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h ^ (h >> 29);
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

    // Correction history (SR_CORR): how far the static eval tends to be off,
    // by pawn structure and by each side's non-pawn pieces, per side to move.
    static constexpr std::size_t CorrSize = 16384;
    std::vector<std::int16_t> corr_pawn = std::vector<std::int16_t>(2 * CorrSize);
    std::vector<std::int16_t> corr_np = std::vector<std::int16_t>(2 * 2 * CorrSize);

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
// ---- Extensions ------------------------------------------------------------
// A/B (vs. the engine without them, 25 ms/move unless noted):
//   singular extension     openings -2 (600), EG suite -2 (600);
//                          at 100 ms/move -12 (450: +133 =169 -148)          off
//   passed-pawn extension  openings +6 (600, LOS 71%), EG suite -5 (600)    off
// Neither is a proven gain here; kept, switched off, for retesting at longer
// time controls or after other search changes.
#ifndef SR_SINGULAR
#define SR_SINGULAR 0
#endif
#ifndef SR_PAWN_EXT
#define SR_PAWN_EXT 0
#endif
// Singular extension: at depth >= SingularMinDepth, if the TT move (a lower
// bound or exact, from a search at most 3 plies shallower) is better than
// every other move by SingularMargin * depth in a half-depth search, it is
// searched one ply deeper. If even the other moves beat beta, cut off
// ("multi-cut").
inline constexpr bool UseSingular = SR_SINGULAR;
inline constexpr int SingularMinDepth = 8;
inline constexpr int SingularMargin = 2;
// Passed-pawn extension: a pawn push to the 7th rank is searched one ply deeper.
inline constexpr bool UsePawnExt = SR_PAWN_EXT;

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

// ---- Search round 2 (ideas from Stockfish 19's search; own implementation
// and values). Each is a switch, tested against the engine before it in three
// levels: L1 300 games @10 ms/move (variants screened), L2 600 @10 ms,
// L3 600 @25 ms (depth-gated features: all levels @25 ms). A feature that
// passes all three becomes the default and the base for the next one.
//   aspiration windows, 12 cp: L1 +84 (8 cp +49, 20 +49, 35 +38), L2 +67,
//     L3 +56                                                         ADOPTED
//   move-loop pruning (futility + SEE + history + razoring): L1 +89
//     (alone: futility +8, SEE +34, history -48, razoring +27), L2 +84,
//     L3 +70                                                         ADOPTED
//   cut-node group: LMR +1 at cut nodes -1, null move only at cut nodes +23
//     (L2 -5), null-move R 4+d/3+eval -2, IIR (25 ms) +15 / L2 +14 / L3 -6  off
//   correction history: L1 +5, L2 +9, L3 -3                             off
//   richer LMR (bad captures, killers, deeper/shallower re-search): L1 -23 off
//   ordering (check bonus, threat escape, 4-ply cont. history): -14; each
//     alone -33 / -22 / -20                                             off
//   mate distance pruning: L1 +14, L2 +10, L3 -10; fail-high blend -26;
//     no check extension -12                                            off
//   singular + negative + double extensions (25 ms): L1 +31, L2 -1, L3 -1 off
// After the first two, the rest were within about +-15 Elo, below what
// 600-game tests resolve; they stay here as switches for longer tests.
#ifndef SR_ASP
#define SR_ASP 1            // aspiration windows at the root
#endif
#ifndef SR_ASP_DELTA
#define SR_ASP_DELTA 12     // initial half-width (cp)
#endif
#ifndef SR_IIR
#define SR_IIR 0            // internal iterative reduction: PV / cut node without a TT move
#endif
#ifndef SR_IIR_DEPTH
#define SR_IIR_DEPTH 6
#endif
#ifndef SR_LMR_CUT
#define SR_LMR_CUT 0        // +1 LMR at expected cut nodes
#endif
#ifndef SR_NMP_CUT
#define SR_NMP_CUT 0        // null move only at expected cut nodes
#endif
#ifndef SR_NMP2
#define SR_NMP2 0           // null-move R = 4 + depth/3 + min((eval-beta)/SR_NMP_EVDIV, 3)
#endif
#ifndef SR_NMP_EVDIV
#define SR_NMP_EVDIV 200
#endif
#ifndef SR_FUTP
#define SR_FUTP 1           // futility pruning of quiet moves (by reduced depth)
#endif
#ifndef SR_FUT_BASE
#define SR_FUT_BASE 100
#endif
#ifndef SR_FUT_MUL
#define SR_FUT_MUL 100
#endif
#ifndef SR_FUT_DEPTH
#define SR_FUT_DEPTH 8
#endif
#ifndef SR_SEEP
#define SR_SEEP 1           // SEE pruning: quiets below -SR_SEE_QUIET*d^2, captures below -SR_SEE_CAP*d
#endif
#ifndef SR_SEE_QUIET
#define SR_SEE_QUIET 20
#endif
#ifndef SR_SEE_CAP
#define SR_SEE_CAP 100
#endif
#ifndef SR_HISTP
#define SR_HISTP 1          // history pruning: quiets with history < -SR_HISTP_MUL*depth
#endif
#ifndef SR_HISTP_MUL
#define SR_HISTP_MUL 2000
#endif
#ifndef SR_HISTP_DEPTH
#define SR_HISTP_DEPTH 4
#endif
#ifndef SR_RAZOR
#define SR_RAZOR 1          // razoring: depth <= 3, eval + margin*depth < alpha -> qsearch
#endif
#ifndef SR_RAZOR_MARGIN
#define SR_RAZOR_MARGIN 250
#endif
#ifndef SR_CORR
#define SR_CORR 0           // correction history (pawn + non-pawn structure)
#endif
#ifndef SR_CORR_WP
#define SR_CORR_WP 64
#endif
#ifndef SR_CORR_WNP
#define SR_CORR_WNP 48
#endif
#ifndef SR_LMR2
#define SR_LMR2 0           // LMR: bad captures too, killers/counter less, deeper/shallower re-search
#endif
#ifndef SR_SE_NEG
#define SR_SE_NEG 0         // singular: negative extension when the TT move isn't singular
#endif
#ifndef SR_SE_DOUBLE
#define SR_SE_DOUBLE 0      // singular: double extension when far below singular beta
#endif
#ifndef SR_ORD2
#define SR_ORD2 0           // quiet ordering: check bonus, threat escape, 4-ply continuation history
#endif
#ifndef SR_ORD2_PARTS
#define SR_ORD2_PARTS 7     // bit 1 check bonus, 2 threat escape, 4 4-ply continuation history
#endif
#ifndef SR_MDP
#define SR_MDP 0            // mate distance pruning
#endif
#ifndef SR_FH_BLEND
#define SR_FH_BLEND 0       // blend a fail-high score towards beta
#endif
#ifndef SR_CHECK_EXT
#define SR_CHECK_EXT 1      // +1 ply in check
#endif
inline constexpr bool UseAsp = SR_ASP;
inline constexpr int AspDelta = SR_ASP_DELTA;
inline constexpr bool UseIir = SR_IIR;
inline constexpr int IirDepth = SR_IIR_DEPTH;
inline constexpr bool UseLmrCut = SR_LMR_CUT;
inline constexpr bool UseNmpCut = SR_NMP_CUT;
inline constexpr bool UseNmp2 = SR_NMP2;
inline constexpr int NmpEvalDiv = SR_NMP_EVDIV;
inline constexpr bool UseFutPrune = SR_FUTP;
inline constexpr int FutBase = SR_FUT_BASE, FutMul = SR_FUT_MUL, FutDepth = SR_FUT_DEPTH;
inline constexpr bool UseSeePrune = SR_SEEP;
inline constexpr int SeeQuietMul = SR_SEE_QUIET, SeeCapMul = SR_SEE_CAP;
inline constexpr bool UseHistPrune = SR_HISTP;
inline constexpr int HistPruneMul = SR_HISTP_MUL, HistPruneDepth = SR_HISTP_DEPTH;
inline constexpr bool UseRazor = SR_RAZOR;
inline constexpr int RazorMargin = SR_RAZOR_MARGIN;
inline constexpr bool UseCorr = SR_CORR;
inline constexpr int CorrWPawn = SR_CORR_WP, CorrWNonPawn = SR_CORR_WNP;
inline constexpr bool UseLmr2 = SR_LMR2;
inline constexpr bool UseSeNeg = SR_SE_NEG;
inline constexpr bool UseSeDouble = SR_SE_DOUBLE;
inline constexpr bool UseOrd2 = SR_ORD2;
inline constexpr bool Ord2Check = UseOrd2 && (SR_ORD2_PARTS & 1);
inline constexpr bool Ord2Threat = UseOrd2 && (SR_ORD2_PARTS & 2);
inline constexpr bool Ord2Cont4 = UseOrd2 && (SR_ORD2_PARTS & 4);
inline constexpr bool UseMdp = SR_MDP;
inline constexpr bool UseFhBlend = SR_FH_BLEND;
inline constexpr bool UseCheckExt = SR_CHECK_EXT;
inline constexpr bool UseMovePruning = UseFutPrune || UseSeePrune || UseHistPrune;
inline constexpr int CorrLimit = 1024;  // correction-history entries in [-CorrLimit, CorrLimit]

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
            root_depth_ = depth;
            if (thread_id_ > 0 && depth > 1 && (depth + thread_id_) % 2 == 0) continue;
            excluded_.clear();
            std::vector<Line> completed;

            // MultiPV: search the root once per line, excluding the best moves
            // already found at this depth.
            for (int k = 1; k <= lines; ++k) {
                seldepth_ = 0;
                Score score;
                if (UseAsp && k == 1 && lines == 1 && depth >= 4 && have_prev_ && !is_mate_score(prev_score_)) {
                    // Aspiration window around the last score, widened on a fail.
                    Score delta = AspDelta;
                    Score a = std::max(prev_score_ - delta, -Infinite), b = std::min(prev_score_ + delta, Infinite);
                    for (;;) {
                        score = pvs(depth, 0, a, b, false, false);
                        if (stopped_) break;
                        if (score <= a) { b = (a + b) / 2; a = std::max(score - delta, -Infinite); }
                        else if (score >= b) { b = std::min(score + delta, Infinite); }
                        else break;
                        delta += delta / 2;
                        if (delta > 600) { a = -Infinite; b = Infinite; }
                    }
                } else {
                    score = pvs(depth, 0, -Infinite, Infinite, false, false);
                }
                if (stopped_) break;  // partial result: discard
                if (k == 1) { prev_score_ = score; have_prev_ = true; }

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

    // Lazy SMP: helper threads (id >= 1) search the same position with a
    // shared TT; they skip alternate depths so the threads spread out, and
    // their results reach the main thread (id 0) only through the TT.
    void set_thread_id(int id) { thread_id_ = id; }

private:
    // Principal Variation Search.
    // First move: full window. Remaining moves: null window (alpha, alpha+1)
    // to prove they are no better; re-search with the full window if one is.
    // `null_ok`: false right after a null move (no two null moves in a row).
    // `cut_node`: a non-PV node expected to fail high (its parent expects a
    // refutation here); the alternative non-PV kind is an all node.
    Score pvs(int depth, int ply, Score alpha, Score beta, bool null_ok, bool cut_node) {
        pv_len_[ply] = ply;

        // Check extension: never stop searching while in check.
        const bool in_check = board_.inCheck();
        if (in_check && UseCheckExt) ++depth;

        if (depth <= 0) return qsearch(ply, alpha, beta);

        if (check_time()) return 0;
        seldepth_ = std::max(seldepth_, ply);

        if (ply > 0 && is_draw()) return 0;
        if (ply >= MaxPly - 1) return eval::evaluate(board_);

        // Mate distance pruning: no line from here can beat a shorter mate
        // already found (or avoid a quicker loss).
        if (UseMdp && ply > 0) {
            alpha = std::max(alpha, static_cast<Score>(-Mate + ply));
            beta = std::min(beta, static_cast<Score>(Mate - ply - 1));
            if (alpha >= beta) return alpha;
        }

        const bool pv_node = (beta - alpha > 1);
        const Score alpha_orig = alpha;
        const std::uint64_t key = board_.hash();

        // TT probe. Cut off only in non-PV nodes so the PV stays complete.
        chess::Move tt_move(chess::Move::NO_MOVE);
        // Singular-extension verification search: this node is being searched
        // without the move in se_excluded_[ply]; the TT result for the node
        // (which includes that move) must not cut it off or be overwritten.
        const chess::Move se_excluded = se_excluded_[static_cast<std::size_t>(ply)];
        const bool se_search = se_excluded != chess::Move::NO_MOVE;
        TTEntry entry;
        const bool tt_hit = tt_.probe(key, entry);
        if (tt_hit) {
            tt_move = chess::Move(entry.move);
            if (!pv_node && !se_search && entry.depth >= depth) {
                const Score s = score_from_tt(entry.score, ply);
                if (entry.bound == Bound::Exact ||
                    (entry.bound == Bound::Lower && s >= beta) ||
                    (entry.bound == Bound::Upper && s <= alpha))
                    return s;
            }
        }

        // Static evaluation, used by the pruning below. Not meaningful in check.
        // With SR_CORR it is corrected by what the search found in similar
        // positions (same pawns / same pieces).
        CorrKeys ck{};
        if (UseCorr) ck = corr_keys();
        const Score raw_eval = in_check ? -Infinite : eval::evaluate(board_);
        const Score static_eval = (UseCorr && !in_check) ? corrected(raw_eval, ck) : raw_eval;

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

            // Razoring: far below alpha at low depth: see if captures can save it.
            if (UseRazor && depth <= 3 && !is_mate_score(alpha) && static_eval + RazorMargin * depth < alpha) {
                const Score v = qsearch(ply, alpha, beta);
                if (stopped_) return 0;
                if (v <= alpha) return v;
            }

            // Null-move pruning: give the opponent a free move. If a reduced
            // search still fails high, a real move would too. Skipped without
            // pieces (pawn endgames), where zugzwang makes passing an
            // advantage and the assumption breaks. When improving, also tried
            // with the eval slightly below beta.
            const bool nmp_eval_ok =
                static_eval >= beta ||
                (UseImprovingNmp && improving && static_eval >= beta - NmpImprovingMargin);
            if (null_ok && !se_search && depth >= NmpMinDepth && nmp_eval_ok && !is_mate_score(beta) &&
                (!UseNmpCut || cut_node) && board_.hasNonPawnMaterial(board_.sideToMove())) {
                const int r = UseNmp2 ? 4 + depth / 3 + std::min((static_eval - beta) / NmpEvalDiv, 3)
                                      : NmpBase + depth / NmpDiv;
                moved_[static_cast<std::size_t>(ply)] = -1;
                board_.makeNullMove();
                const Score score = -pvs(depth - 1 - r, ply + 1, -beta, -beta + 1, false, false);
                board_.unmakeNullMove();
                if (stopped_) return 0;
                if (score >= beta) return is_mate_score(score) ? beta : score;  // don't trust null-move mates
            }

            // ProbCut: if a good capture beats a raised beta in a much
            // shallower search, it very likely beats beta at full depth.
            if (UseProbCut && !se_search && depth >= ProbCutMinDepth && !is_mate_score(beta)) {
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
                            score = -pvs(depth - 1 - ProbCutReduction, ply + 1, -pc_beta, -pc_beta + 1, true, !cut_node);
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

        // Internal iterative reduction: a PV / cut node without a TT move is
        // probably badly ordered; search it one ply shallower.
        if (UseIir && (pv_node || cut_node) && depth >= IirDepth && tt_move == chess::Move::NO_MOVE) --depth;

        OrderedMoves list;
        chess::movegen::legalmoves(list.moves, board_);

        if (list.moves.empty())
            return board_.inCheck() ? -Mate + ply : 0;  // checkmate : stalemate
        if (se_search && list.size() == 1) return alpha;  // only the excluded move: nothing to compare

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
            if (move == se_excluded) continue;
            const bool quiet = is_quiet(board_, move);

            // Extensions, limited so the search can't run away (ply < 2 x root depth).
            int extension = 0;
            if (ply > 0 && ply < 2 * root_depth_) {
                if (UseSingular && move == tt_move && !se_search && depth >= SingularMinDepth && tt_hit &&
                    entry.bound != Bound::Upper && entry.depth >= depth - 3 &&
                    !is_mate_score(score_from_tt(entry.score, ply))) {
                    const Score s_beta = score_from_tt(entry.score, ply) - SingularMargin * depth;
                    se_excluded_[static_cast<std::size_t>(ply)] = move;
                    const Score s = pvs((depth - 1) / 2, ply, s_beta - 1, s_beta, false, cut_node);
                    se_excluded_[static_cast<std::size_t>(ply)] = chess::Move(chess::Move::NO_MOVE);
                    if (stopped_) return 0;
                    if (s < s_beta) {                    // only the TT move is good: search it deeper
                        extension = 1;
                        if (UseSeDouble && !pv_node && s < s_beta - 20 && double_ext_[static_cast<std::size_t>(ply)] < 6) extension = 2;
                    }
                    else if (s_beta >= beta) return s_beta;  // several moves beat beta: multi-cut
                    else if (UseSeNeg && (score_from_tt(entry.score, ply) >= beta || cut_node)) extension = -1;
                }
                if (UsePawnExt && extension == 0 && board_.at(move.from()).type() == chess::PieceType::PAWN &&
                    move.typeOf() != chess::Move::PROMOTION) {
                    const int to_rank = move.to().index() / 8;
                    if ((board_.sideToMove() == chess::Color::WHITE ? to_rank : 7 - to_rank) == 6) extension = 1;
                }
            }

            // Late move pruning: at shallow depth, once enough moves have been
            // searched, skip the remaining quiet moves (ordered last, rarely
            // best). Fewer are searched when not improving.
            const bool lmp_skip = UseLmp && ply > 0 && !in_check && quiet && depth <= LmpMaxDepth &&
                                  best > -(Mate - MaxPly) && moves_searched >= lmp_limit(depth, improving);
            if (lmp_skip && !LmpKeepChecks) continue;

            const int move_hist = quiet ? quiet_history(move, ply) : 0;  // before the move is made

            // Move-loop pruning (after the first move): futility, history and
            // SEE pruning of quiets, SEE pruning of captures.
            if (UseMovePruning && ply > 0 && !in_check && best > -(Mate - MaxPly) && move != tt_move &&
                board_.hasNonPawnMaterial(board_.sideToMove())) {
                const bool gc = gives_check(board_, move);
                if (quiet && !gc) {
                    const int lmr_d = std::max(0, depth - 1 - lmr_reduction(depth, moves_searched + 1));
                    if (UseHistPrune && depth <= HistPruneDepth && move_hist < -HistPruneMul * depth) continue;
                    if (UseFutPrune && lmr_d <= FutDepth && static_eval + FutBase + FutMul * lmr_d <= alpha) continue;
                    if (UseSeePrune && !see_ge(board_, move, -SeeQuietMul * lmr_d * lmr_d)) continue;
                } else if (!quiet && UseSeePrune && depth <= 8 && !see_ge(board_, move, -SeeCapMul * depth)) {
                    continue;
                }
            }
            const bool bad_capture = !quiet && list.scores[static_cast<std::size_t>(i)] < 0;
            const bool refuter = quiet && (move == killers_[static_cast<std::size_t>(ply)][0] ||
                                           move == killers_[static_cast<std::size_t>(ply)][1]);
            make(move, ply);
            const bool gives_check = board_.inCheck();
            if (lmp_skip && !gives_check) {  // (LmpKeepChecks) checks are still searched
                board_.unmakeMove(move);
                continue;
            }
            const int new_depth = depth - 1 + extension;
            double_ext_[static_cast<std::size_t>(ply + 1)] =
                double_ext_[static_cast<std::size_t>(ply)] + (extension >= 2 ? 1 : 0);
            Score score;
            if (moves_searched == 0) {
                score = -pvs(new_depth, ply + 1, -beta, -alpha, true, pv_node ? false : !cut_node);
            } else {
                // Late move reductions: well-ordered moves late in the list
                // rarely matter, so search them shallower first and only
                // re-search at full depth if they beat alpha.
                int r = 0;
                if (depth >= LmrMinDepth && moves_searched >= LmrMinMoves && (quiet || (UseLmr2 && bad_capture)) &&
                    !in_check && !gives_check) {
                    r = lmr_reduction(depth, moves_searched + 1);
                    if (pv_node) --r;
                    if (UseImprovingLmr && !improving) ++r;
                    if (UseHistoryLmr && quiet) r -= move_hist / HistLmrDivisor;
                    if (UseLmrCut && cut_node) ++r;
                    if (UseLmr2 && refuter) --r;
                    r = std::clamp(r, 0, new_depth - 1);
                }

                // Reduced searches expect a refutation (cut node); an unreduced
                // null-window search flips the parent's expectation.
                score = -pvs(new_depth - r, ply + 1, -alpha - 1, -alpha, true, r > 0 ? true : !cut_node);
                if (score > alpha && r > 0) {  // reduced search surprised us
                    int nd = new_depth;
                    if (UseLmr2) nd += (score > best + 60 ? 1 : 0) - (score < best + 10 ? 1 : 0);
                    if (nd > new_depth - r) score = -pvs(nd, ply + 1, -alpha - 1, -alpha, true, !cut_node);
                }
                if (score > alpha && score < beta)  // PVS re-search, full window
                    score = -pvs(new_depth, ply + 1, -beta, -alpha, true, false);
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

        if (UseFhBlend && best >= beta && !is_mate_score(best) && !is_mate_score(beta))
            best = (best * depth + beta) / (depth + 1);

        // Correction history: the search result vs. the static eval, when the
        // bound says which way the eval was wrong (and the best move is quiet).
        if (UseCorr && !in_check && !se_search && !is_mate_score(best) &&
            (best_move == chess::Move::NO_MOVE || is_quiet(board_, best_move)) &&
            ((best_move != chess::Move::NO_MOVE) ? best > static_eval : best < static_eval)) {
            const int bonus = std::clamp((best - static_eval) * depth / 8, -CorrLimit / 4, CorrLimit / 4);
            update_corr(ck, bonus);
        }

        // Don't store the root while excluding MultiPV moves: that score is
        // for "best move except the ones already found", not the position.
        if (!(ply == 0 && !excluded_.empty()) && !se_search) {
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
            best = stand_pat = UseCorr ? corrected(eval::evaluate(board_), corr_keys()) : eval::evaluate(board_);
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
        const Threats threats = Ord2Threat ? compute_threats() : Threats{};
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
                if (UseOrd2) score += ord2_bonus(m, threats);
            }
            list.scores[static_cast<std::size_t>(i)] = score;
        }
    }

    // SR_ORD2: squares attacked by enemy pieces of lower value than each of
    // our piece types (a piece standing there can be won).
    struct Threats {
        std::uint64_t by_lesser[6]{};
    };
    Threats compute_threats() const {
        using chess::PieceType;
        Threats t;
        const chess::Color them = ~board_.sideToMove();
        const chess::Bitboard occ = board_.occ();
        std::uint64_t pawn = 0, minor = 0, rook = 0;
        auto each = [&](PieceType pt, auto&& f) {
            for (std::uint64_t b = board_.pieces(pt, them).getBits(); b; b &= b - 1)
                f(chess::Square(static_cast<int>(std::countr_zero(b))));
        };
        each(PieceType::PAWN, [&](chess::Square sq) { pawn |= chess::attacks::pawn(them, sq).getBits(); });
        each(PieceType::KNIGHT, [&](chess::Square sq) { minor |= chess::attacks::knight(sq).getBits(); });
        each(PieceType::BISHOP, [&](chess::Square sq) { minor |= chess::attacks::bishop(sq, occ).getBits(); });
        each(PieceType::ROOK, [&](chess::Square sq) { rook |= chess::attacks::rook(sq, occ).getBits(); });
        t.by_lesser[1] = t.by_lesser[2] = pawn;
        t.by_lesser[3] = pawn | minor;
        t.by_lesser[4] = pawn | minor | rook;
        return t;
    }
    int ord2_bonus(const chess::Move& m, const Threats& t) const {
        int b = 0;
        if (Ord2Check && gives_check(board_, m) && see_ge(board_, m, -75)) b += 8000;
        if (!Ord2Threat) return b;
        const auto pt = board_.at(m.from()).type();
        const int idx = static_cast<int>(pt.internal());
        if (idx >= 1 && idx <= 4) {
            const std::uint64_t lesser = t.by_lesser[static_cast<std::size_t>(idx)];
            const int from_t = static_cast<int>((lesser >> m.from().index()) & 1);
            const int to_t = static_cast<int>((lesser >> m.to().index()) & 1);
            b += 16 * see_value(pt) * (from_t - to_t);
        }
        return b;
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
            if (Ord2Cont4) h += ot_.cont_get(prev_move(ply, 4), pt) / 2;
        }
        return h;
    }

    void update_quiet(const chess::Move& m, int ply, int bonus) {
        ot_.main.update(board_.sideToMove(), m, bonus);
        if (UseContHistory) {
            const int pt = piece_to(board_, m);
            for (int back = 1; back <= (Ord2Cont4 ? 4 : 2); ++back) {
                if (back == 3) continue;
                const int prev = prev_move(ply, back);
                if (prev >= 0) update_stat(ot_.cont_at(prev, pt), bonus);
            }
        }
    }

    // Captures tracked by capture history (not promotions: those have their
    // own band and piece type changes).
    static bool is_capture_stat(const chess::Move& m) { return m.typeOf() != chess::Move::PROMOTION; }

    // Correction history keys for the current position (side to move included).
    struct CorrKeys {
        std::size_t pawn = 0, np_white = 0, np_black = 0;
    };
    CorrKeys corr_keys() const {
        using chess::PieceType;
        using chess::Color;
        const std::size_t stm = board_.sideToMove() == Color::WHITE ? 0 : 1;
        const std::size_t mask = OrderTables::CorrSize - 1;
        auto np = [&](Color c) {
            return mix_key(board_.pieces(PieceType::KNIGHT, c).getBits() | board_.pieces(PieceType::BISHOP, c).getBits(),
                           board_.pieces(PieceType::ROOK, c).getBits() | board_.pieces(PieceType::QUEEN, c).getBits(),
                           board_.pieces(PieceType::KING, c).getBits());
        };
        CorrKeys k;
        k.pawn = stm * OrderTables::CorrSize +
                 (mix_key(board_.pieces(PieceType::PAWN, Color::WHITE).getBits(), board_.pieces(PieceType::PAWN, Color::BLACK).getBits()) & mask);
        k.np_white = (stm * 2 + 0) * OrderTables::CorrSize + (np(Color::WHITE) & mask);
        k.np_black = (stm * 2 + 1) * OrderTables::CorrSize + (np(Color::BLACK) & mask);
        return k;
    }
    Score corrected(Score raw, const CorrKeys& k) const {
        const int c = (CorrWPawn * ot_.corr_pawn[k.pawn] + CorrWNonPawn * (ot_.corr_np[k.np_white] + ot_.corr_np[k.np_black])) / 1024;
        return std::clamp(raw + c, -(Mate - MaxPly) + 1, Mate - MaxPly - 1);
    }
    void update_corr(const CorrKeys& k, int bonus) {
        auto upd = [&](std::int16_t& e) {
            const int v = e;
            e = static_cast<std::int16_t>(std::clamp(v + bonus - v * std::abs(bonus) / CorrLimit, -CorrLimit, CorrLimit));
        };
        upd(ot_.corr_pawn[k.pawn]);
        upd(ot_.corr_np[k.np_white]);
        upd(ot_.corr_np[k.np_black]);
    }

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

    // Singular extensions: move excluded at each ply during a verification search.
    std::array<chess::Move, MaxPly + 1> se_excluded_{};
    std::array<int, MaxPly + 2> double_ext_{};  // double extensions on the path (SR_SE_DOUBLE)
    Score prev_score_ = 0;                     // last completed score (aspiration windows)
    bool have_prev_ = false;
    int root_depth_ = 1;
    int thread_id_ = 0;  // 0 = main thread

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
