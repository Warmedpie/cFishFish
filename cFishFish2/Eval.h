// Eval.h : Position evaluation for cFishFish.
//
// Tapered evaluation:
//   eval = taper(mg, eg, phase), mg / eg = white's terms - black's terms,
// terms = material + piece-square tables (PST) + mobility, phase from the
// non-pawn material on the board. Then scaled down as the fifty-move rule
// approaches.
//
// Piece values (PeSTO) and mobility weights come from the author's earlier
// engine; A/B tested at fixed depth 4 (2000 games each): PeSTO values +26 Elo,
// mobility +76 Elo. The earlier engine's per-side phase tested -20 / -31 Elo
// against this global phase and was not kept.
//
// Incremental: EvalBoard overrides chess::Board's placePiece / removePiece
// hooks, which makeMove / unmakeMove call for every piece that appears or
// disappears; each call adds / subtracts table entries. Everything that
// depends only on (piece, square) lives in those tables, including all of
// knight mobility and the constant part of slider mobility. evaluate() only
// has to add slider mobility (depends on occupancy) and do the phase blend.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstdint>
#include <string_view>

#include "Chess.h"

namespace eval {

// Scores are in centipawns-like units (a middlegame pawn = 82), used as UCI
// "score cp".
using Score = int;

// ---------------------------------------------------------------------------
// Piece values (PeSTO), indexed by chess::PieceType (P, N, B, R, Q, K).
// ---------------------------------------------------------------------------
inline constexpr std::array<Score, 6> PieceValueMG = {82, 337, 365, 497, 1025, 0};
inline constexpr std::array<Score, 6> PieceValueEG = {94, 281, 297, 512, 936, 0};

// Named middlegame values (used by SEE, MVV-LVA and pruning margins).
inline constexpr Score PawnValue   = PieceValueMG[0];
inline constexpr Score KnightValue = PieceValueMG[1];
inline constexpr Score BishopValue = PieceValueMG[2];
inline constexpr Score RookValue   = PieceValueMG[3];
inline constexpr Score QueenValue  = PieceValueMG[4];

// Search bounds. Mate scores sit just below Infinite so the search can
// encode "mate in N" as Mate - ply and still stay inside the window.
inline constexpr Score Infinite = 32000;
inline constexpr Score Mate     = 31000;

// ---------------------------------------------------------------------------
// Game phase (Stockfish's scheme, scaled to our piece values).
//
// Non-pawn material (knights, bishops, rooks, queens at middlegame values,
// both sides) at the start is 2 * (2*337 + 2*365 + 2*497 + 1025) = 6846.
// Stockfish's limits sit at 91.9% and 23.6% of its starting value (15258 and
// 3915 of 16604), which scale to 6291 and 1614 here. Above MidgameLimit is
// full middlegame (phase 128), below EndgameLimit full endgame (0).
// ---------------------------------------------------------------------------
inline constexpr std::array<Score, 6> PhaseWeight = {
    0, PieceValueMG[1], PieceValueMG[2], PieceValueMG[3], PieceValueMG[4], 0,
};
inline constexpr Score MidgameLimit = 6291;
inline constexpr Score EndgameLimit = 1614;
inline constexpr int PhaseMax = 128;

inline constexpr int phase_from_npm(Score npm) {
    npm = std::clamp(npm, EndgameLimit, MidgameLimit);
    return (npm - EndgameLimit) * PhaseMax / (MidgameLimit - EndgameLimit);
}

inline constexpr Score taper(Score mg, Score eg, int phase) {
    return (mg * phase + eg * (PhaseMax - phase)) / PhaseMax;
}

// ---------------------------------------------------------------------------
// Mobility: bonus = weight * (attacked squares - base) per piece. With the
// mobility-area term on (default), squares holding our own pawns or attacked
// by enemy pawns don't count. Started from the old engine's weights; tuned.
// ---------------------------------------------------------------------------
struct MobilityWeight {
    Score mg, eg;
    int base;
};
inline constexpr MobilityWeight KnightMobility = {7, 2, 4};
inline constexpr MobilityWeight BishopMobility = {7, 3, 7};
inline constexpr MobilityWeight RookMobility = {7, 4, 7};
inline constexpr MobilityWeight QueenMobility = {3, 6, 14};

// ---------------------------------------------------------------------------
// Piece-square tables, from WHITE's point of view.
//
// Laid out the way the board looks from White's side: the first row is
// rank 8 (a8 ... h8), the last row is rank 1 (a1 ... h1). Black uses the
// same tables mirrored vertically, so each table is written once.
//
// Values are Texel-tuned (tools/tuner.cpp) in engine units, starting from
// Stockfish's classical PSQT scaled to our piece values. Squares too rare in
// the tuning data to tune (e.g. a knight on the 8th rank) kept their
// starting values, which is why those rows still look symmetric.
// ---------------------------------------------------------------------------
using Table = std::array<Score, 64>;

// PST scale. Before tuning, the tables held raw Stockfish values scaled by
// our starting non-pawn material / Stockfish's, per side & phase:
//   opening: 2*337 + 2*365 + 2*497 + 1025 = 3423  vs  2*781 + 2*825 + 2*1276 + 2538 = 8302
//   endgame: 2*281 + 2*297 + 2*512 +  936 = 3116  vs  2*854 + 2*915 + 2*1380 + 2682 = 8980
// Since Texel tuning the tables are in engine units, so the scale is 1:1.
inline constexpr Score PstScaleNumMG = 1;
inline constexpr Score PstScaleDenMG = 1;
inline constexpr Score PstScaleNumEG = 1;
inline constexpr Score PstScaleDenEG = 1;

// clang-format off
// ---- Opening / middlegame --------------------------------------------------
// Pawns, opening / middlegame
inline constexpr Table PawnMG = {
       0,   0,   0,   0,   0,   0,   0,   0,   // rank 8
      -3,   3,  -1,  -5,   2,  -7,   4,  -3,   // rank 7
     -27, -11,  14,  -1,  16,   8, -11,  21,   // rank 6
     -12,   6,  -3,   3,  -5,  10, -12,  -5,   // rank 5
      -9, -13,  -2,  -1,   3,  -2, -14, -14,   // rank 4
     -12,   5,   5, -10,   3,   6,  22,  -1,   // rank 3
     -11,  -2,   2, -11,  -7,  12,  13, -10,   // rank 2
       0,   0,   0,   0,   0,   0,   0,   0,   // rank 1
};

// Knights, opening / middlegame
inline constexpr Table KnightMG = {
     -83, -34, -23, -11, -11, -23, -34, -83,   // rank 8
     -28, -11, -11,  15,  15,   2, -11, -28,   // rank 7
      -4,   9,  32,  11,  37,  24,  29,  -4,   // rank 6
     -14, -12,  12,  26,  28,  39,  -1,   8,   // rank 5
      -3,  26,   7,  10,  30,  25,  20,  19,   // rank 4
     -22, -18, -14,  21,  10,   1,   1,  -9,   // rank 3
     -32, -17,   1,  -9,  -3,  -4, -17, -32,   // rank 2
     -72, -23, -31, -30,   3, -31, -19, -72,   // rank 1
};

// Bishops, opening / middlegame
inline constexpr Table BishopMG = {
     -20,   0,  -6,  -9,  -9,  -6,   0, -20,   // rank 8
      -7,  -6,   5,   0,   0,   2,  -6,  -7,   // rank 7
     -27,   2,  -5,  12,   5,   0,  -3, -16,   // rank 6
     -14,  -3,  16,  72,  30,  22,  18, -33,   // rank 5
      -2,  -3,  23,  -1,  31,  11,  -8,  -2,   // rank 4
       6,  31, -21,   9,   7,  25,   1,  -2,   // rank 3
      -6,  -8,  18,   3,   9,  18,  19,  -6,   // rank 2
     -22,  -2,   7, -43,  23,   1,  -2, -22,   // rank 1
};

// Rooks, opening / middlegame
inline constexpr Table RookMG = {
      -7,  -8,   0,   4,   4,   0,  -8,  -7,   // rank 8
      -1, -16,   6,   7,   7,   7,   5,  -8,   // rank 7
     -26,  -1,   2,   5,   5,   2,  -1,  -9,   // rank 6
     -14,   2,  -7,   3,   1,  -2, -32, -28,   // rank 5
     -22,   6, -25, -23,  10,   2, -14, -11,   // rank 4
     -36, -16, -20,  -4, -25, -20, -31, -33,   // rank 3
     -29, -11, -20, -12, -10,   0, -13, -25,   // rank 2
     -25, -31, -27,  -3,   4,  11, -35, -28,   // rank 1
};

// Queens, opening / middlegame
inline constexpr Table QueenMG = {
      -1,  -1,   0,  -1,  -1,   0,  -1,  -1,   // rank 8
     -26,  -7,   4,   3,   3,   4,   2,  -2,   // rank 7
       5,   4,  15,   3,   3,   2,  22,  21,   // rank 6
      -6, -18,  15, -16,  20,  15,  11,  19,   // rank 5
       3, -12, -11,  -7,   0,  16,   6, -37,   // rank 4
      -3,   1, -12, -22,   3,   6,  14,  -7,   // rank 3
      -9,  -4,  13,  12,  -8,  12,  31,  -1,   // rank 2
     -16, -34,  12,   4,   9, -25,  -2,   1,   // rank 1
};

// King, opening / middlegame
inline constexpr Table KingMG = {
      24,  37,  19,   0,   0,  19,  37,  24,   // rank 8
      36,  49,  27,  14,  14,  27,  49,  36,   // rank 7
      51,  60,  33,  13,  13,  33,  60,  51,   // rank 6
      63,  74,  43,  29,  29,  43,  74,  63,   // rank 5
      68,  78,  57,  40,  40,  57,  78,  68,   // rank 4
      80,  86,  29,  34,  29,  61,  83,  76,   // rank 3
     115, 106,  92,  85,  96, 105, 132, 128,   // rank 2
     112, 152, 149, 116, 141, 125, 142, 168,   // rank 1
};

// ---- Endgame ---------------------------------------------------------------
// Pawns, endgame
inline constexpr Table PawnEG = {
       0,   0,   0,   0,   0,   0,   0,   0,   // rank 8
     -25,   6,   3,  15, -20,  -6,   3, -13,   // rank 7
     -12,  13,   0,  22,   4,  29,  36,  17,   // rank 6
       2,  14,  -6,   5,   2,   5,   9,   3,   // rank 5
       0,   6,  -6,   5,  -3, -13,   5,  -2,   // rank 4
       3,  -7,  -1,   0,  -5,  -8,  -3,  -3,   // rank 3
       3,  -1,  -3,   4, -10,   3,  -2, -12,   // rank 2
       0,   0,   0,   0,   0,   0,   0,   0,   // rank 1
};

// Knights, endgame
inline constexpr Table KnightEG = {
     -35, -31, -19,  -6,  -6, -19, -31, -35,   // rank 8
     -24, -20, -33, -11,   7, -26, -17, -24,   // rank 7
     -18, -11, -17,  -3,   2,  -6,   1, -18,   // rank 6
      -1,  -8,  -7, -19,  22,  14,  16,  -7,   // rank 5
      -7, -14,  -3,  13,   2,  -4,   3,  -2,   // rank 4
     -81, -12,  -5, -21,   1, -16,  -3, -53,   // rank 3
     -23, -19, -13,   2, -10, -16, -19, -23,   // rank 2
     -33, -61, -22,  -6, -26, -22, -32, -33,   // rank 1
};

// Bishops, endgame
inline constexpr Table BishopEG = {
     -16, -15, -10, -11,  10, -14, -15, -16,   // rank 8
     -21,  16,  22,  25,  10,  -4,   6,   3,   // rank 7
      -9,   6,  13,  30,   9,  -1,   1,  -6,   // rank 6
      -6,   9, -15,  10,  20,  -1,   4,   3,   // rank 5
      35,   1,   3,   9,  10,  24,  -4, -19,   // rank 4
      12,  -2,   8,   8,   6,   3, -13,   3,   // rank 3
     -13,  14,  -2,  -2,  -4, -24, -14, -13,   // rank 2
       5, -18,  -4, -19,   6,  -6,  -8,  -8,   // rank 1
};

// Rooks, endgame
inline constexpr Table RookEG = {
      -1,   0,   4,  13,  16,  13,   2,  13,   // rank 8
     -23, -15,  -5,   0,   2,   0, -11, -16,   // rank 7
      -5,   3,   2,  15,  17,   6,   5,   5,   // rank 6
       3,   1,   6,   3,   7,   6, -11,  -5,   // rank 5
       3,  -2,  -6,   9,  -1,  -8,   2,   5,   // rank 4
     -26, -38, -15, -20,  -3, -31,  -6,  13,   // rank 3
     -10, -22, -20, -20, -25, -13, -10, -31,   // rank 2
     -33, -27, -16, -32, -26, -28, -20, -34,   // rank 1
};

// Queens, endgame
inline constexpr Table QueenEG = {
     -35, -14, -15, -12, -12, -15, -18, -33,   // rank 8
      -5, -15,   4,  12,  -3,  -8,  -9, -17,   // rank 7
     -13,  -3,  -3,  -1,  27,  38,   3, -13,   // rank 6
     -33,   0,  12,  20,  24,  17,  33,  39,   // rank 5
     -10,  -5,  11,   7,  16,  15,  19,  -1,   // rank 4
     -14,  10, -16,  21,   7,   1, -17, -14,   // rank 3
     -19, -16, -20, -62,  -9, -39, -24, -19,   // rank 2
     -24, -20, -22, -40, -36, -16, -20, -24,   // rank 1
};

// King, endgame
inline constexpr Table KingEG = {
       4,  48,  25,  27,  27,  25,  43,   4,   // rank 8
      38,  59,  53,  48,  63,  56,  61,  31,   // rank 7
      56,  64,  41,  32,  42,  64,  77,  37,   // rank 6
      47,  62,  46,  54,  54,  52,  56,  15,   // rank 5
      40,  41,  47,  57,  43,  45,  46,  36,   // rank 4
      11,  33,  43,  48,  46,  49,  45,  20,   // rank 3
      23,  45,  42,  39,  44,  46,  41,  27,   // rank 2
      27,  29,  10,  13,  17,  24,  40,  23,   // rank 1
};
// clang-format on

// Indexed by chess::PieceType.
inline constexpr std::array<const Table*, 6> PSTMG = {
    &PawnMG, &KnightMG, &BishopMG, &RookMG, &QueenMG, &KingMG,
};
inline constexpr std::array<const Table*, 6> PSTEG = {
    &PawnEG, &KnightEG, &BishopEG, &RookEG, &QueenEG, &KingEG,
};

// ---------------------------------------------------------------------------
// Lookup tables built at compile time. Index: [piece][square], piece =
// color * 6 + type (chess::Piece order), square = a1 = 0 ... h8 = 63.
// Values are from the owner's point of view (positive = good for the side
// the piece belongs to); they are summed per side.
// ---------------------------------------------------------------------------
namespace detail {

// Table row for chess square `sq` (a1 = 0) as seen by the piece's owner.
// Tables are written rank 8 first, so White flips the rank (sq ^ 56) and
// Black, whose rank 8 is White's rank 1, reads the square directly.
constexpr std::size_t table_index(int sq, bool white) {
    return static_cast<std::size_t>(white ? (sq ^ 56) : sq);
}

// v * num / den, rounded to nearest (half away from zero).
constexpr Score scale(Score v, Score num, Score den) {
    const long long x = static_cast<long long>(v) * num;
    return static_cast<Score>((x >= 0 ? x + den / 2 : x - den / 2) / den);
}

// Knight moves from a square on an empty board (knight attacks don't
// depend on occupancy, so knight mobility is a pure piece-square term).
// (Written with plain index loops and no lambdas so older MSVC versions can
// evaluate it at compile time.)
constexpr int knight_squares(int sq) {
    const int df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    const int dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        const int f = sq % 8 + df[i], r = sq / 8 + dr[i];
        if (f >= 0 && f < 8 && r >= 0 && r < 8) ++n;
    }
    return n;
}

// Mobility, incremental part for one piece on one square: all of it for
// knights, and the constant "- weight * base" for sliders (their square
// counts depend on occupancy and are added in evaluate()).
constexpr Score mobility_constant(int type, int sq, bool mg) {
    switch (type) {
        case 1: return (mg ? KnightMobility.mg : KnightMobility.eg) * (knight_squares(sq) - KnightMobility.base);
        case 2: return -(mg ? BishopMobility.mg : BishopMobility.eg) * BishopMobility.base;
        case 3: return -(mg ? RookMobility.mg : RookMobility.eg) * RookMobility.base;
        case 4: return -(mg ? QueenMobility.mg : QueenMobility.eg) * QueenMobility.base;
        default: return 0;
    }
}

using PieceSquare = std::array<std::array<Score, 64>, 12>;

enum class Kind { MaterialMG, MaterialEG, PstMG, PstEG, MobilityMG, MobilityEG, NonPawnMaterial };

constexpr PieceSquare build(Kind kind) {
    PieceSquare out{};
    for (int color = 0; color < 2; ++color) {
        const bool white = (color == 0);
        for (int type = 0; type < 6; ++type) {
            const auto t = static_cast<std::size_t>(type);
            for (int sq = 0; sq < 64; ++sq) {
                Score v = 0;
                switch (kind) {
                    case Kind::MaterialMG: v = PieceValueMG[t]; break;
                    case Kind::MaterialEG: v = PieceValueEG[t]; break;
                    case Kind::PstMG: v = scale((*PSTMG[t])[table_index(sq, white)], PstScaleNumMG, PstScaleDenMG); break;
                    case Kind::PstEG: v = scale((*PSTEG[t])[table_index(sq, white)], PstScaleNumEG, PstScaleDenEG); break;
                    case Kind::MobilityMG: v = mobility_constant(type, sq, true); break;
                    case Kind::MobilityEG: v = mobility_constant(type, sq, false); break;
                    case Kind::NonPawnMaterial: v = PhaseWeight[t]; break;
                }
                out[static_cast<std::size_t>(color * 6 + type)][static_cast<std::size_t>(sq)] = v;
            }
        }
    }
    return out;
}

inline constexpr PieceSquare MaterialMGTable = build(Kind::MaterialMG);
inline constexpr PieceSquare MaterialEGTable = build(Kind::MaterialEG);
inline constexpr PieceSquare PstMGTable      = build(Kind::PstMG);
inline constexpr PieceSquare PstEGTable      = build(Kind::PstEG);
inline constexpr PieceSquare MobilityMGTable = build(Kind::MobilityMG);
inline constexpr PieceSquare MobilityEGTable = build(Kind::MobilityEG);
inline constexpr PieceSquare NPMTable        = build(Kind::NonPawnMaterial);

constexpr std::size_t pi(chess::Piece p) { return static_cast<std::size_t>(p.internal()); }
constexpr std::size_t si(chess::Square sq) { return static_cast<std::size_t>(sq.index()); }

}  // namespace detail

// Owner's-view table lookups for one piece on one square.
inline Score pst_mg_of(chess::Piece p, chess::Square sq) { return detail::PstMGTable[detail::pi(p)][detail::si(sq)]; }
inline Score pst_eg_of(chess::Piece p, chess::Square sq) { return detail::PstEGTable[detail::pi(p)][detail::si(sq)]; }

// ---------------------------------------------------------------------------
// Incrementally maintained terms, one set per side, each from that side's
// own point of view (all positive = good for that side).
// ---------------------------------------------------------------------------
struct SideTerms {
    Score material_mg = 0, material_eg = 0;
    Score pst_mg = 0, pst_eg = 0;
    Score mobility_mg = 0, mobility_eg = 0;  // incremental part only
    Score npm = 0;                           // non-pawn material (PhaseWeight): drives the phase

    bool operator==(const SideTerms&) const = default;
};

struct Terms {
    std::array<SideTerms, 2> side;  // [0] = white, [1] = black

    int phase() const { return phase_from_npm(side[0].npm + side[1].npm); }

    void add(chess::Piece p, chess::Square sq) { apply(p, sq, +1); }
    void sub(chess::Piece p, chess::Square sq) { apply(p, sq, -1); }

    bool operator==(const Terms&) const = default;

private:
    void apply(chess::Piece p, chess::Square sq, int sign) {
        using namespace detail;
        SideTerms& s = side[static_cast<std::size_t>(p.color().internal())];
        const std::size_t i = pi(p), j = si(sq);
        s.material_mg += sign * MaterialMGTable[i][j];
        s.material_eg += sign * MaterialEGTable[i][j];
        s.pst_mg      += sign * PstMGTable[i][j];
        s.pst_eg      += sign * PstEGTable[i][j];
        s.mobility_mg += sign * MobilityMGTable[i][j];
        s.mobility_eg += sign * MobilityEGTable[i][j];
        s.npm         += sign * NPMTable[i][j];
    }
};

// From-scratch computation: walks each piece bitboard, popping one set bit
// per piece. Used to initialize EvalBoard, for the UCI "eval" command, and
// to cross-check the incremental terms.
inline Terms compute(const chess::Board& board) {
    Terms t;
    for (int color = 0; color < 2; ++color) {
        const chess::Color c = color == 0 ? chess::Color::WHITE : chess::Color::BLACK;
        for (int type = 0; type < 6; ++type) {
            const chess::PieceType pt(static_cast<chess::PieceType::underlying>(type));
            const chess::Piece piece(pt, c);
            chess::Bitboard bb = board.pieces(pt, c);
            while (bb) t.add(piece, chess::Square(bb.pop()));
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
// Slider mobility, occupancy-dependent part: weight * attacked squares,
// summed per piece type (the "- weight * base" part is incremental).
// ---------------------------------------------------------------------------
struct MgEg {
    Score mg = 0, eg = 0;
};

inline MgEg slider_mobility(const chess::Board& b, chess::Color c) {
    const chess::Bitboard occ = b.occ();
    int bishop = 0, rook = 0, queen = 0;

    chess::Bitboard bb = b.pieces(chess::PieceType::BISHOP, c);
    while (bb) bishop += chess::attacks::bishop(chess::Square(bb.pop()), occ).count();
    bb = b.pieces(chess::PieceType::ROOK, c);
    while (bb) rook += chess::attacks::rook(chess::Square(bb.pop()), occ).count();
    bb = b.pieces(chess::PieceType::QUEEN, c);
    while (bb) queen += chess::attacks::queen(chess::Square(bb.pop()), occ).count();

    return {BishopMobility.mg * bishop + RookMobility.mg * rook + QueenMobility.mg * queen,
            BishopMobility.eg * bishop + RookMobility.eg * rook + QueenMobility.eg * queen};
}

// ===========================================================================
// Positional terms (computed per node, not incrementally).
//
// Each term has an on/off switch and named weights (mg, eg), in the same
// units as the piece values. Weights are Texel-tuned (tools/tuner.cpp, 612k
// self-play positions; +42 Elo at 50 ms/move over the hand-set values),
// except the king-danger table and attack units, which the tuner holds fixed.
// The EV_* macros exist only so A/B test builds can flip terms from the
// compiler command line; normal builds use the defaults below.
//
// A/B results (fixed depth 4, each term added on top of the ones before it):
//   passed pawns      +68  (2000 games)      kept
//   king safety       +15  (2000)            kept
//   threats           +59  (2000)            kept
//   bishop pair / bad +32  (2000)            kept
//   rook terms        +11  (2000, LOS 94%)   kept
//   mobility area     +28  (2000)            kept
//   tempo             +23  (2000)            kept
//   pawn structure    +25  (3000; +4 on an earlier, weaker baseline)  kept
//   space             +16  (3000)            kept
//   outposts           +5  (3000 twice, LOS ~82%)  off: not proven, retune
//   endgame scaling    -2  (3000)            off
// ===========================================================================
#ifndef EV_PASSED
#define EV_PASSED 1
#endif
#ifndef EV_KING_SAFETY
#define EV_KING_SAFETY 1
#endif
#ifndef EV_PAWN_STRUCTURE
#define EV_PAWN_STRUCTURE 1
#endif
#ifndef EV_OUTPOSTS
#define EV_OUTPOSTS 0
#endif
#ifndef EV_BISHOPS
#define EV_BISHOPS 1
#endif
#ifndef EV_ROOKS
#define EV_ROOKS 1
#endif
#ifndef EV_THREATS
#define EV_THREATS 1
#endif
#ifndef EV_MOBILITY_AREA
#define EV_MOBILITY_AREA 1
#endif
#ifndef EV_TEMPO
#define EV_TEMPO 1
#endif
#ifndef EV_SPACE
#define EV_SPACE 1
#endif
#ifndef EV_SCALING
#define EV_SCALING 0
#endif

inline constexpr bool EvalUsePassed        = EV_PASSED;
inline constexpr bool EvalUseKingSafety    = EV_KING_SAFETY;
inline constexpr bool EvalUsePawnStructure = EV_PAWN_STRUCTURE;
inline constexpr bool EvalUseOutposts      = EV_OUTPOSTS;
inline constexpr bool EvalUseBishops       = EV_BISHOPS;
inline constexpr bool EvalUseRooks         = EV_ROOKS;
inline constexpr bool EvalUseThreats       = EV_THREATS;
inline constexpr bool EvalUseMobilityArea  = EV_MOBILITY_AREA;
inline constexpr bool EvalUseTempo         = EV_TEMPO;
inline constexpr bool EvalUseSpace         = EV_SPACE;
inline constexpr bool EvalUseScaling       = EV_SCALING;

// ---- Weights ---------------------------------------------------------------
// Passed pawns, by relative rank (rank 2 = index 1 ... rank 7 = index 6).
// Started from the author's earlier engine, then tuned. They overlap with the
// pawn PSTs (both reward advanced pawns), so only their sum is meaningful.
inline constexpr Score PassedMG[8] = {0, -7, 12, -6, -1, 64, 145, 0};
inline constexpr Score PassedEG[8] = {0, 16, 11, 24, 38, 64, 107, 0};
inline constexpr MgEg PassedProtected = {2, -2};   // defended by own pawn
inline constexpr MgEg PassedBlocked = {-16, -9}; // stop square occupied
inline constexpr Score PassedFreePathEG[8] = {0, 15, 3, 2, 10, 19, 41, 0};
inline constexpr Score PassedKingDistEG = 4;           // x (rank-2) x (2*their king dist - our king dist)
inline constexpr MgEg RookBehindPasser = {23, 4};

// King safety: attack units -> penalty (mg), only with 2+ attackers.
inline constexpr int KingAttackWeight[6] = {0, 2, 2, 3, 5, 0};  // per attacked zone square
inline constexpr int SafeCheckUnits[6]   = {0, 3, 2, 4, 6, 0};  // N, B, R, Q safe check available
inline constexpr Score ShieldRank3 = -5, ShieldMissing = -27, ShieldOpenFile = -22;  // mg, per file
// Classic attack-unit table (chessprogramming.org "King Safety").
inline constexpr Score SafetyTable[64] = {
      0,   0,   1,   2,   3,   5,   7,   9,  12,  15,  18,  22,  26,  30,  35,  39,
     44,  50,  56,  62,  68,  75,  82,  85,  89,  97, 105, 113, 122, 131, 140, 150,
    169, 180, 191, 202, 213, 225, 237, 248, 260, 272, 283, 295, 307, 319, 330, 342,
    354, 366, 377, 389, 401, 412, 424, 436, 448, 459, 471, 483, 494, 500, 500, 500,
};

// Pawn structure (per pawn).
inline constexpr MgEg IsolatedPawn = {-3, -14};
inline constexpr MgEg DoubledPawn = {-13, -6};
inline constexpr MgEg BackwardPawn = {-7, -7};

// Outposts (protected by own pawn, can never be attacked by an enemy pawn).
inline constexpr MgEg KnightOutpost = {25, 15};
inline constexpr MgEg BishopOutpost = {12, 6};

// Bishops.
inline constexpr MgEg BishopPair = {22, 68};
inline constexpr MgEg BadBishopPerPawn = {-2, -2};  // own pawns on the bishop's colour

// Rooks.
inline constexpr MgEg RookOpenFile = {38, -2};
inline constexpr MgEg RookSemiOpenFile = {18, 9};
inline constexpr MgEg RookOnSeventh = {-9, 28};

// Threats (bonus for the attacking side).
inline constexpr MgEg ThreatByPawn = {42, 27};  // pawn attacks a piece
inline constexpr MgEg ThreatByMinor = {45, 38};  // knight/bishop attacks rook/queen
inline constexpr MgEg ThreatByRook = {35, 20};  // rook attacks queen
inline constexpr MgEg HangingPiece = {13, 21};  // attacked and undefended

inline constexpr Score Tempo = 12;       // side to move
inline constexpr Score SpacePerSquare = 1;  // mg

// ---- Bitboard helpers (a1 = bit 0, h8 = bit 63) -----------------------------
namespace bb {
using U64 = std::uint64_t;
inline constexpr U64 FileA = 0x0101010101010101ULL;
inline constexpr U64 FileH = FileA << 7;
inline constexpr U64 Rank1 = 0xFFULL;
inline constexpr U64 DarkSquares = 0xAA55AA55AA55AA55ULL;  // a1 is dark

constexpr U64 file_mask(int f) { return FileA << f; }
constexpr U64 rank_mask(int r) { return Rank1 << (8 * r); }
constexpr U64 adjacent_files(int f) {
    return (f > 0 ? file_mask(f - 1) : 0) | (f < 7 ? file_mask(f + 1) : 0);
}
// Ranks strictly in front of rank r, from `white`'s point of view.
constexpr U64 ranks_ahead(int r, bool white) {
    U64 m = 0;
    for (int i = 0; i < 8; ++i)
        if (white ? i > r : i < r) m |= rank_mask(i);
    return m;
}
constexpr int rel_rank(int sq, bool white) { return white ? sq / 8 : 7 - sq / 8; }

struct Masks {
    U64 forward_file[2][64]{};  // same file, ahead
    U64 passed[2][64]{};        // same + adjacent files, ahead
    U64 attack_span[2][64]{};   // adjacent files, ahead (where enemy pawns could attack from)
};
constexpr Masks build_masks() {
    Masks m{};
    for (int c = 0; c < 2; ++c)
        for (int sq = 0; sq < 64; ++sq) {
            const U64 ahead = ranks_ahead(sq / 8, c == 0);
            m.forward_file[c][sq] = file_mask(sq % 8) & ahead;
            m.attack_span[c][sq] = adjacent_files(sq % 8) & ahead;
            m.passed[c][sq] = m.forward_file[c][sq] | m.attack_span[c][sq];
        }
    return m;
}
inline constexpr Masks M = build_masks();

inline U64 pawn_attacks(U64 pawns, bool white) {
    return white ? (((pawns << 7) & ~FileH) | ((pawns << 9) & ~FileA))
                 : (((pawns >> 9) & ~FileH) | ((pawns >> 7) & ~FileA));
}
inline int popcount(U64 x) { return std::popcount(x); }
inline int lsb(U64 x) { return std::countr_zero(x); }
inline int distance(int a, int b) {
    return std::max(std::abs(a % 8 - b % 8), std::abs(a / 8 - b / 8));
}
}  // namespace bb

// ---- Attack information, both sides ---------------------------------------
struct AttackInfo {
    bb::U64 by[2][6]{};  // [color][piece type] squares attacked
    bb::U64 all[2]{};
    bb::U64 pieces[2][6]{};
    bb::U64 occ[2]{};
    // King attack bookkeeping: attacker side's pieces hitting the enemy king zone.
    int king_attackers[2]{};  // indexed by attacking side
    int king_units[2]{};
    // Slider mobility (weight * attacked squares) and mobility-area correction.
    MgEg slider[2]{};
    MgEg mobility_adjust[2]{};
};

inline AttackInfo gather_attacks(const chess::Board& b) {
    using namespace bb;
    AttackInfo ai;
    const U64 occ = b.occ().getBits();
    for (int c = 0; c < 2; ++c) {
        const chess::Color col = c == 0 ? chess::Color::WHITE : chess::Color::BLACK;
        for (int t = 0; t < 6; ++t)
            ai.pieces[c][t] = b.pieces(chess::PieceType(static_cast<chess::PieceType::underlying>(t)), col).getBits();
        ai.occ[c] = b.us(col).getBits();
    }
    U64 zone[2];
    for (int c = 0; c < 2; ++c) {
        const int ksq = lsb(ai.pieces[c][5]);
        zone[c] = chess::attacks::king(chess::Square(ksq)).getBits() | (1ULL << ksq);
    }
    for (int c = 0; c < 2; ++c) {
        const bool white = (c == 0);
        const U64 excluded = ai.pieces[c][0] | pawn_attacks(ai.pieces[c ^ 1][0], !white);  // mobility area
        ai.by[c][0] = pawn_attacks(ai.pieces[c][0], white);
        ai.by[c][5] = chess::attacks::king(chess::Square(lsb(ai.pieces[c][5]))).getBits();
        const MobilityWeight* mw[5] = {nullptr, &KnightMobility, &BishopMobility, &RookMobility, &QueenMobility};
        for (int t = 1; t <= 4; ++t) {
            U64 pcs = ai.pieces[c][t];
            while (pcs) {
                const int sq = lsb(pcs);
                pcs &= pcs - 1;
                const chess::Square s(sq);
                U64 a = 0;
                switch (t) {
                    case 1: a = chess::attacks::knight(s).getBits(); break;
                    case 2: a = chess::attacks::bishop(s, chess::Bitboard(occ)).getBits(); break;
                    case 3: a = chess::attacks::rook(s, chess::Bitboard(occ)).getBits(); break;
                    case 4: a = chess::attacks::queen(s, chess::Bitboard(occ)).getBits(); break;
                }
                ai.by[c][t] |= a;
                if (t >= 2) {
                    ai.slider[c].mg += mw[t]->mg * popcount(a);
                    ai.slider[c].eg += mw[t]->eg * popcount(a);
                }
                if (const U64 hit = a & zone[c ^ 1]) {
                    ++ai.king_attackers[c];
                    ai.king_units[c] += KingAttackWeight[t] * popcount(hit);
                }
                if (EvalUseMobilityArea) {
                    const int n = popcount(a & excluded);
                    ai.mobility_adjust[c].mg -= mw[t]->mg * n;
                    ai.mobility_adjust[c].eg -= mw[t]->eg * n;
                }
            }
        }
        for (int t = 0; t < 6; ++t) ai.all[c] |= ai.by[c][t];
    }
    return ai;
}

// ---- Per-side positional score (that side's point of view) ----------------
enum Term {
    TermPassed, TermKingSafety, TermStructure, TermOutposts, TermBishops, TermRooks,
    TermThreats, TermMobilityArea, TermSpace, TermCount
};
inline constexpr const char* TermNames[TermCount] = {
    "passed", "king safety", "pawn struct", "outposts", "bishops", "rooks",
    "threats", "mob. area", "space",
};
struct TermScores {
    MgEg t[TermCount]{};
    MgEg total() const {
        MgEg s;
        for (const auto& x : t) { s.mg += x.mg; s.eg += x.eg; }
        return s;
    }
};

inline TermScores side_positional(const AttackInfo& ai, int c) {
    using namespace bb;
    TermScores ts;
    auto add = [&](Term term, const MgEg& v, int n = 1) {
        ts.t[term].mg += v.mg * n;
        ts.t[term].eg += v.eg * n;
    };
    const bool white = (c == 0);
    const int them = c ^ 1;
    const U64 ours_p = ai.pieces[c][0], theirs_p = ai.pieces[them][0];
    const U64 occ = ai.occ[0] | ai.occ[1];
    const int our_k = lsb(ai.pieces[c][5]), their_k = lsb(ai.pieces[them][5]);

    // Pawns: structure and passed pawns.
    U64 pawns = ours_p;
    while (pawns) {
        const int sq = lsb(pawns);
        pawns &= pawns - 1;
        const int f = sq % 8, r = rel_rank(sq, white);
        const U64 bit = 1ULL << sq;

        if (EvalUsePawnStructure) {
            const bool isolated = (adjacent_files(f) & ours_p) == 0;
            if (isolated) add(TermStructure, IsolatedPawn);
            if (M.forward_file[c][sq] & ours_p) add(TermStructure, DoubledPawn);  // another own pawn ahead
            if (!isolated) {
                // Backward: no own pawn on an adjacent file level or behind, and
                // the stop square is attacked by an enemy pawn.
                const U64 behind_or_level = ~ranks_ahead(sq / 8, white);
                const int stop = white ? sq + 8 : sq - 8;
                if ((adjacent_files(f) & behind_or_level & ours_p) == 0 && stop >= 0 && stop < 64 &&
                    (ai.by[them][0] & (1ULL << stop)))
                    add(TermStructure, BackwardPawn);
            }
        }

        if (EvalUsePassed && (M.passed[c][sq] & theirs_p) == 0 && (M.forward_file[c][sq] & ours_p) == 0) {
            ts.t[TermPassed].mg += PassedMG[r];
            ts.t[TermPassed].eg += PassedEG[r];
            if (ai.by[c][0] & bit) add(TermPassed, PassedProtected);
            const int stop = white ? sq + 8 : sq - 8;
            if (stop >= 0 && stop < 64) {
                if (occ & (1ULL << stop)) add(TermPassed, PassedBlocked);
                if ((M.forward_file[c][sq] & occ) == 0) ts.t[TermPassed].eg += PassedFreePathEG[r];
                if (r >= 3)
                    ts.t[TermPassed].eg += PassedKingDistEG * (r - 2) * (distance(their_k, stop) * 2 - distance(our_k, stop));
            }
            if (EvalUseRooks) {
                const U64 behind = file_mask(f) & ~M.forward_file[c][sq] & ~bit;
                if (behind & ai.pieces[c][3]) add(TermRooks, RookBehindPasser);
            }
        }
    }

    // Knights / bishops: outposts; bishop pair / bad bishop.
    if (EvalUseOutposts) {
        for (int t = 1; t <= 2; ++t) {
            U64 pcs = ai.pieces[c][t];
            while (pcs) {
                const int sq = lsb(pcs);
                pcs &= pcs - 1;
                const int r = rel_rank(sq, white);
                if (r >= 3 && r <= 5 && (ai.by[c][0] & (1ULL << sq)) && (M.attack_span[c][sq] & theirs_p) == 0)
                    add(TermOutposts, t == 1 ? KnightOutpost : BishopOutpost);
            }
        }
    }
    if (EvalUseBishops) {
        const U64 bishops = ai.pieces[c][2];
        if ((bishops & DarkSquares) && (bishops & ~DarkSquares)) add(TermBishops, BishopPair);
        U64 pcs = bishops;
        while (pcs) {
            const int sq = lsb(pcs);
            pcs &= pcs - 1;
            const U64 colour = (DarkSquares >> sq) & 1 ? DarkSquares : ~DarkSquares;
            add(TermBishops, BadBishopPerPawn, popcount(ours_p & colour));
        }
    }

    // Rooks: open files, 7th rank.
    if (EvalUseRooks) {
        U64 pcs = ai.pieces[c][3];
        while (pcs) {
            const int sq = lsb(pcs);
            pcs &= pcs - 1;
            const U64 file = file_mask(sq % 8);
            if ((file & ours_p) == 0) add(TermRooks, (file & theirs_p) ? RookSemiOpenFile : RookOpenFile);
            if (rel_rank(sq, white) == 6 &&
                (rel_rank(their_k, white) == 7 || (theirs_p & rank_mask(white ? 6 : 1))))
                add(TermRooks, RookOnSeventh);
        }
    }

    // Threats against the opponent's pieces.
    if (EvalUseThreats) {
        const U64 minors_majors = ai.pieces[them][1] | ai.pieces[them][2] | ai.pieces[them][3] | ai.pieces[them][4];
        add(TermThreats, ThreatByPawn, popcount(ai.by[c][0] & minors_majors));
        add(TermThreats, ThreatByMinor, popcount((ai.by[c][1] | ai.by[c][2]) & (ai.pieces[them][3] | ai.pieces[them][4])));
        add(TermThreats, ThreatByRook, popcount(ai.by[c][3] & ai.pieces[them][4]));
        add(TermThreats, HangingPiece, popcount(ai.all[c] & minors_majors & ~ai.all[them]));
    }

    // King safety of OUR king (penalty; mg only).
    if (EvalUseKingSafety) {
        int units = ai.king_units[them];
        // Safe checks the opponent could give.
        const chess::Square k(our_k);
        const U64 safe = ~ai.all[c] & ~ai.occ[them];
        const U64 nchk = chess::attacks::knight(k).getBits();
        const U64 bchk = chess::attacks::bishop(k, chess::Bitboard(occ)).getBits();
        const U64 rchk = chess::attacks::rook(k, chess::Bitboard(occ)).getBits();
        if (nchk & ai.by[them][1] & safe) units += SafeCheckUnits[1];
        if (bchk & ai.by[them][2] & safe) units += SafeCheckUnits[2];
        if (rchk & ai.by[them][3] & safe) units += SafeCheckUnits[3];
        if ((bchk | rchk) & ai.by[them][4] & safe) units += SafeCheckUnits[4];
        if (ai.king_attackers[them] >= 2 && ai.pieces[them][4])
            ts.t[TermKingSafety].mg -= SafetyTable[std::min(units, 63)];

        // Pawn shield, for a king on its first two ranks.
        if (rel_rank(our_k, white) <= 1) {
            const int kf = std::clamp(our_k % 8, 1, 6);
            for (int f = kf - 1; f <= kf + 1; ++f) {
                const U64 file = file_mask(f);
                const U64 own = file & ours_p;
                if (!own) ts.t[TermKingSafety].mg += ShieldMissing;
                else {
                    const int nearest = white ? lsb(own) : 63 - std::countl_zero(own);
                    if (rel_rank(nearest, white) >= 2) ts.t[TermKingSafety].mg += ShieldRank3;
                }
                if (!(file & (ours_p | theirs_p))) ts.t[TermKingSafety].mg += ShieldOpenFile;
            }
        }
    }

    if (EvalUseMobilityArea) {
        ts.t[TermMobilityArea].mg += ai.mobility_adjust[c].mg;
        ts.t[TermMobilityArea].eg += ai.mobility_adjust[c].eg;
    }

    // Space: safe central squares in our half (files c-f, ranks 2-4), squares
    // behind our own pawns counting double.
    if (EvalUseSpace) {
        const U64 area = (file_mask(2) | file_mask(3) | file_mask(4) | file_mask(5)) &
                         (white ? (rank_mask(1) | rank_mask(2) | rank_mask(3))
                                : (rank_mask(6) | rank_mask(5) | rank_mask(4)));
        const U64 safe = area & ~ours_p & ~ai.by[them][0];
        U64 behind = ours_p;
        for (int i = 0; i < 3; ++i) behind |= white ? (behind >> 8) : (behind << 8);
        ts.t[TermSpace].mg += SpacePerSquare * (popcount(safe) + popcount(safe & behind));
    }
    return ts;
}

// Endgame scale factor (64 = normal) applied to the endgame score of the
// side that is ahead: opposite-coloured bishops, and material that can't win.
inline int endgame_scale(const AttackInfo& ai, Score eg_white_view) {
    using namespace bb;
    if (!EvalUseScaling) return 64;
    const int strong = eg_white_view >= 0 ? 0 : 1, weak = strong ^ 1;
    auto npm = [&](int c) {
        return popcount(ai.pieces[c][1]) * PieceValueMG[1] + popcount(ai.pieces[c][2]) * PieceValueMG[2] +
               popcount(ai.pieces[c][3]) * PieceValueMG[3] + popcount(ai.pieces[c][4]) * PieceValueMG[4];
    };
    // Strong side has no pawns and at most a minor piece more: hard or impossible to win.
    if (ai.pieces[strong][0] == 0 && npm(strong) - npm(weak) <= PieceValueMG[2])
        return npm(strong) < PieceValueMG[3] ? 0 : 16;
    // Opposite-coloured bishops.
    const U64 wb = ai.pieces[0][2], bbish = ai.pieces[1][2];
    if (popcount(wb) == 1 && popcount(bbish) == 1 &&
        (((wb & DarkSquares) != 0) != ((bbish & DarkSquares) != 0))) {
        const bool only_bishops = !(ai.pieces[0][1] | ai.pieces[1][1] | ai.pieces[0][3] | ai.pieces[1][3] |
                                    ai.pieces[0][4] | ai.pieces[1][4]);
        return only_bishops ? 32 : 48;
    }
    return 64;
}

// All positional terms, white minus black, plus the endgame scale factor.
struct Positional {
    MgEg score;              // white minus black
    TermScores side[2];      // per side, own point of view (for display)
    AttackInfo ai;
};
inline Positional positional(const chess::Board& b) {
    Positional p;
    p.ai = gather_attacks(b);
    p.side[0] = side_positional(p.ai, 0);
    p.side[1] = side_positional(p.ai, 1);
    const MgEg w = p.side[0].total(), bl = p.side[1].total();
    p.score = {w.mg - bl.mg, w.eg - bl.eg};
    return p;
}

// One side's middlegame / endgame totals (its own point of view).
inline MgEg side_totals(const SideTerms& s, const MgEg& slider) {
    return {s.material_mg + s.pst_mg + s.mobility_mg + slider.mg,
            s.material_eg + s.pst_eg + s.mobility_eg + slider.eg};
}

// Fifty-move scaling: the score shrinks linearly towards 0 as the halfmove
// clock approaches 100 (a draw by rule), so the engine prefers progress
// (captures, pawn moves) over shuffling when it is better.
inline Score fifty_move_scale(Score s, int halfmove_clock) {
    return s * (100 - std::min(halfmove_clock, 100)) / 100;
}

// White's point of view.
inline Score white_view(const chess::Board& board, const Terms& t) {
    const Positional p = positional(board);
    const MgEg w = side_totals(t.side[0], p.ai.slider[0]);
    const MgEg b = side_totals(t.side[1], p.ai.slider[1]);
    const Score mg = w.mg - b.mg + p.score.mg;
    Score eg = w.eg - b.eg + p.score.eg;
    eg = eg * endgame_scale(p.ai, eg) / 64;
    Score s = taper(mg, eg, t.phase());
    if (EvalUseTempo) s += board.sideToMove() == chess::Color::WHITE ? Tempo : -Tempo;
    return fifty_move_scale(s, static_cast<int>(board.halfMoveClock()));
}

// ---------------------------------------------------------------------------
// Board with incrementally updated evaluation terms.
// ---------------------------------------------------------------------------
class EvalBoard : public chess::Board {
public:
    explicit EvalBoard(std::string_view fen = chess::constants::STARTPOS) : chess::Board(fen) {
        refresh();
    }
    explicit EvalBoard(const chess::Board& board) : chess::Board(board) { refresh(); }

    void setFen(std::string_view fen) override {
        chess::Board::setFen(fen);
        refresh();
    }

    // Current incremental terms. No computation: just a read.
    const Terms& terms() const { return terms_; }

    // Recompute from scratch (after setup; also handy for debugging).
    void refresh() { terms_ = compute(*this); }

protected:
    void placePiece(chess::Piece piece, chess::Square sq) override {
        chess::Board::placePiece(piece, sq);
        terms_.add(piece, sq);
    }

    void removePiece(chess::Piece piece, chess::Square sq) override {
        terms_.sub(piece, sq);
        chess::Board::removePiece(piece, sq);
    }

private:
    Terms terms_;
};

// Static evaluation for negamax / PVS: positive = good for the side to move.
inline Score evaluate(const EvalBoard& board) {
    const Score s = white_view(board, board.terms());
    return board.sideToMove() == chess::Color::WHITE ? s : -s;
}

// Same, for a plain chess::Board (terms computed from scratch).
inline Score evaluate(const chess::Board& board) {
    const Score s = white_view(board, compute(board));
    return board.sideToMove() == chess::Color::WHITE ? s : -s;
}

}  // namespace eval
