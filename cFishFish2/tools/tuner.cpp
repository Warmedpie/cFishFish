// tuner.cpp : basic Texel tuner for cFishFish's evaluation.
//
// Texel's method: find eval weights that minimise the mean squared error
// between each position's game result (1 / 0.5 / 0, White's view) and a
// sigmoid of the static eval:  predicted = 1 / (1 + exp(-K * eval)).
//
// How it works:
//   1. Each position is converted to a sparse linear feature vector:
//        eval = taper(sum w_mg * f, sum w_eg * f, phase) + tempo, * fifty scale
//      Features mirror Eval.h term by term (PSTs, mobility, passed pawns,
//      pawn structure, bishops, rooks, threats, king shield, space, tempo).
//      Material and the king-danger table are held fixed (the table isn't
//      linear); the danger penalty is included as a constant.
//   2. The rebuilt eval is checked against eval::evaluate() on every
//      position: a mismatch means the feature extraction is wrong.
//   3. K is fitted to the current weights; then Adam gradient descent on all
//      weights, with a 90/10 train / validation split.
//   4. Prints the tuned values as C++ ready to paste into Eval.h.
//
//   tuner <data.txt> [epochs=450] [lr=1.0] [min_count=3000] [l2=1e-7]
//
// Regularisation against fitting noise:
//  - a weight is frozen if its feature appears in fewer than min_count
//    training positions, counted by phase (a middlegame weight only counts
//    positions in proportion to how middlegame they are, and vice versa);
//  - an L2 penalty l2 * (w - w_start)^2 keeps weights near their start
//    values unless the data clearly says otherwise.
//
// Data lines: "<FEN> | <result>" (tools/datagen output).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "../cFishFish2/uci.h"

using namespace eval;
using bb::U64;

// ---------------------------------------------------------------------------
// Parameter slots. Each slot has an mg and an eg weight; some are frozen.
// ---------------------------------------------------------------------------
enum Slot : int {
    S_PST = 0,                      // 6 * 64, index type*64 + table row (rank 8 first)
    S_MOB = S_PST + 6 * 64,         // 4: N B R Q
    S_PASSED = S_MOB + 4,           // 8: by relative rank
    S_PASSED_PROT = S_PASSED + 8,
    S_PASSED_BLOCK,
    S_PASSED_FREE,                  // 8: eg only
    S_PASSED_KDIST = S_PASSED_FREE + 8,  // eg only
    S_ROOK_BEHIND,
    S_SHIELD_RANK3, S_SHIELD_MISSING, S_SHIELD_OPEN,  // mg only
    S_ISOLATED, S_DOUBLED, S_BACKWARD,
    S_BISHOP_PAIR, S_BAD_BISHOP,
    S_ROOK_OPEN, S_ROOK_SEMI, S_ROOK_7TH,
    S_THREAT_PAWN, S_THREAT_MINOR, S_THREAT_ROOK, S_HANGING,
    S_SPACE,                        // mg only
    S_TEMPO,                        // untapered (stored in mg)
    S_COUNT
};

struct Weights {
    double mg[S_COUNT]{}, eg[S_COUNT]{};
    bool freeze_mg[S_COUNT]{}, freeze_eg[S_COUNT]{};
};

Weights initial_weights() {
    Weights w;
    for (int t = 0; t < 6; ++t)
        for (int i = 0; i < 64; ++i) {
            // Table row i (rank 8 first) is what a white piece on square i^56 reads.
            const chess::Piece p(chess::PieceType(static_cast<chess::PieceType::underlying>(t)), chess::Color::WHITE);
            const chess::Square sq(i ^ 56);
            w.mg[S_PST + t * 64 + i] = pst_mg_of(p, sq);
            w.eg[S_PST + t * 64 + i] = pst_eg_of(p, sq);
        }
    const MobilityWeight mw[4] = {KnightMobility, BishopMobility, RookMobility, QueenMobility};
    for (int i = 0; i < 4; ++i) { w.mg[S_MOB + i] = mw[i].mg; w.eg[S_MOB + i] = mw[i].eg; }
    for (int r = 0; r < 8; ++r) {
        w.mg[S_PASSED + r] = PassedMG[r];
        w.eg[S_PASSED + r] = PassedEG[r];
        w.eg[S_PASSED_FREE + r] = PassedFreePathEG[r];
        w.freeze_mg[S_PASSED_FREE + r] = true;
    }
    auto set = [&](int s, MgEg v) { w.mg[s] = v.mg; w.eg[s] = v.eg; };
    set(S_PASSED_PROT, PassedProtected);
    set(S_PASSED_BLOCK, PassedBlocked);
    w.eg[S_PASSED_KDIST] = PassedKingDistEG; w.freeze_mg[S_PASSED_KDIST] = true;
    set(S_ROOK_BEHIND, RookBehindPasser);
    w.mg[S_SHIELD_RANK3] = ShieldRank3; w.mg[S_SHIELD_MISSING] = ShieldMissing; w.mg[S_SHIELD_OPEN] = ShieldOpenFile;
    for (int s : {S_SHIELD_RANK3, S_SHIELD_MISSING, S_SHIELD_OPEN}) w.freeze_eg[s] = true;
    set(S_ISOLATED, IsolatedPawn); set(S_DOUBLED, DoubledPawn); set(S_BACKWARD, BackwardPawn);
    set(S_BISHOP_PAIR, BishopPair); set(S_BAD_BISHOP, BadBishopPerPawn);
    set(S_ROOK_OPEN, RookOpenFile); set(S_ROOK_SEMI, RookSemiOpenFile); set(S_ROOK_7TH, RookOnSeventh);
    set(S_THREAT_PAWN, ThreatByPawn); set(S_THREAT_MINOR, ThreatByMinor);
    set(S_THREAT_ROOK, ThreatByRook); set(S_HANGING, HangingPiece);
    w.mg[S_SPACE] = SpacePerSquare; w.freeze_eg[S_SPACE] = true;
    w.mg[S_TEMPO] = Tempo; w.freeze_eg[S_TEMPO] = true;
    // Pawns never stand on ranks 1 / 8: freeze those PST entries.
    for (int i = 0; i < 8; ++i)
        for (int row : {0, 7}) {
            w.freeze_mg[S_PST + row * 8 + i] = w.freeze_eg[S_PST + row * 8 + i] = true;
        }
    return w;
}

// ---------------------------------------------------------------------------
// Feature extraction (mirrors Eval.h)
// ---------------------------------------------------------------------------
struct Entry {
    std::uint16_t slot;
    float coef;  // white-view coefficient
};
struct Position {
    std::uint32_t first, count;  // range in the entry pool
    float result;
    std::int16_t phase;
    float fifty;                 // (100 - halfmove) / 100
    float fixed_mg;              // material + king-danger table (white view), not tuned
    float stm;                   // +1 white to move, -1 black
};

struct Dataset {
    std::vector<Position> pos;
    std::vector<Entry> pool;
    std::vector<float> fixed_eg;
};

inline double linear_eval(const Dataset& d, std::size_t i, const Weights& w) {
    const Position& p = d.pos[i];
    double mg = p.fixed_mg, eg = d.fixed_eg[i], tempo = 0;
    for (std::uint32_t k = p.first; k < p.first + p.count; ++k) {
        const Entry& e = d.pool[k];
        if (e.slot == S_TEMPO) { tempo += w.mg[S_TEMPO] * e.coef; continue; }
        mg += w.mg[e.slot] * e.coef;
        eg += w.eg[e.slot] * e.coef;
    }
    return ((mg * p.phase + eg * (PhaseMax - p.phase)) / PhaseMax + tempo) * p.fifty;
}

struct Extractor {
    Dataset data;
    std::vector<float> acc = std::vector<float>(S_COUNT, 0.f);

    void add(int slot, float coef) { acc[static_cast<std::size_t>(slot)] += coef; }

    // Appends the position to `data`.
    void extract(const chess::Board& b, float result) {
        std::fill(acc.begin(), acc.end(), 0.f);
        const Terms terms = compute(b);
        const AttackInfo ai = gather_attacks(b);
        Position pos{};
        pos.result = result;
        pos.phase = static_cast<std::int16_t>(terms.phase());
        pos.fifty = static_cast<float>(100 - std::min<int>(static_cast<int>(b.halfMoveClock()), 100)) / 100.f;
        pos.stm = b.sideToMove() == chess::Color::WHITE ? 1.f : -1.f;
        // Material (fixed): white - black, mg only here; eg handled via
        // separate constant below (stored in fixed_mg / fixed_eg split).
        fixed_mg_ = static_cast<float>(terms.side[0].material_mg - terms.side[1].material_mg);
        fixed_eg_ = static_cast<float>(terms.side[0].material_eg - terms.side[1].material_eg);

        for (int c = 0; c < 2; ++c) side(b, ai, c, c == 0 ? 1.f : -1.f);

        pos.fixed_mg = fixed_mg_;
        pos.first = static_cast<std::uint32_t>(data.pool.size());
        for (int s = 0; s < S_COUNT; ++s)
            if (acc[static_cast<std::size_t>(s)] != 0.f)
                data.pool.push_back({static_cast<std::uint16_t>(s), acc[static_cast<std::size_t>(s)]});
        pos.count = static_cast<std::uint32_t>(data.pool.size()) - pos.first;
        data.pos.push_back(pos);
        data.fixed_eg.push_back(fixed_eg_);
    }

private:
    float fixed_mg_ = 0, fixed_eg_ = 0;

    void side(const chess::Board& b, const AttackInfo& ai, int c, float sign) {
        using namespace bb;
        const bool white = (c == 0);
        const int them = c ^ 1;
        const U64 ours_p = ai.pieces[c][0], theirs_p = ai.pieces[them][0];
        const U64 occ = ai.occ[0] | ai.occ[1];
        const int our_k = lsb(ai.pieces[c][5]), their_k = lsb(ai.pieces[them][5]);
        (void)b;

        // PSTs.
        for (int t = 0; t < 6; ++t) {
            U64 pcs = ai.pieces[c][t];
            while (pcs) {
                const int sq = lsb(pcs);
                pcs &= pcs - 1;
                add(S_PST + t * 64 + (white ? (sq ^ 56) : sq), sign);
            }
        }

        // Mobility: weight * (attacked squares in the mobility area - base).
        const U64 excluded = ours_p | pawn_attacks(theirs_p, !white);
        const int base[5] = {0, KnightMobility.base, BishopMobility.base, RookMobility.base, QueenMobility.base};
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
                add(S_MOB + t - 1, sign * static_cast<float>(popcount(a) - popcount(a & excluded) - base[t]));
            }
        }

        // Pawns.
        U64 pawns = ours_p;
        while (pawns) {
            const int sq = lsb(pawns);
            pawns &= pawns - 1;
            const int f = sq % 8, r = rel_rank(sq, white);
            const U64 bit = 1ULL << sq;
            const bool isolated = (adjacent_files(f) & ours_p) == 0;
            if (isolated) add(S_ISOLATED, sign);
            if (M.forward_file[c][sq] & ours_p) add(S_DOUBLED, sign);
            if (!isolated) {
                const U64 behind_or_level = ~ranks_ahead(sq / 8, white);
                const int stop = white ? sq + 8 : sq - 8;
                if ((adjacent_files(f) & behind_or_level & ours_p) == 0 && stop >= 0 && stop < 64 &&
                    (ai.by[them][0] & (1ULL << stop)))
                    add(S_BACKWARD, sign);
            }
            if ((M.passed[c][sq] & theirs_p) == 0 && (M.forward_file[c][sq] & ours_p) == 0) {
                add(S_PASSED + r, sign);
                if (ai.by[c][0] & bit) add(S_PASSED_PROT, sign);
                const int stop = white ? sq + 8 : sq - 8;
                if (stop >= 0 && stop < 64) {
                    if (occ & (1ULL << stop)) add(S_PASSED_BLOCK, sign);
                    if ((M.forward_file[c][sq] & occ) == 0) add(S_PASSED_FREE + r, sign);
                    if (r >= 3)
                        add(S_PASSED_KDIST, sign * static_cast<float>((r - 2) * (distance(their_k, stop) * 2 - distance(our_k, stop))));
                }
                const U64 behind = file_mask(f) & ~M.forward_file[c][sq] & ~bit;
                if (behind & ai.pieces[c][3]) add(S_ROOK_BEHIND, sign);
            }
        }

        // Bishops.
        const U64 bishops = ai.pieces[c][2];
        if ((bishops & DarkSquares) && (bishops & ~DarkSquares)) add(S_BISHOP_PAIR, sign);
        U64 pcs = bishops;
        while (pcs) {
            const int sq = lsb(pcs);
            pcs &= pcs - 1;
            const U64 colour = (DarkSquares >> sq) & 1 ? DarkSquares : ~DarkSquares;
            add(S_BAD_BISHOP, sign * static_cast<float>(popcount(ours_p & colour)));
        }

        // Rooks.
        pcs = ai.pieces[c][3];
        while (pcs) {
            const int sq = lsb(pcs);
            pcs &= pcs - 1;
            const U64 file = file_mask(sq % 8);
            if ((file & ours_p) == 0) add((file & theirs_p) ? S_ROOK_SEMI : S_ROOK_OPEN, sign);
            if (rel_rank(sq, white) == 6 && (rel_rank(their_k, white) == 7 || (theirs_p & rank_mask(white ? 6 : 1))))
                add(S_ROOK_7TH, sign);
        }

        // Threats.
        const U64 mm = ai.pieces[them][1] | ai.pieces[them][2] | ai.pieces[them][3] | ai.pieces[them][4];
        add(S_THREAT_PAWN, sign * static_cast<float>(popcount(ai.by[c][0] & mm)));
        add(S_THREAT_MINOR, sign * static_cast<float>(popcount((ai.by[c][1] | ai.by[c][2]) & (ai.pieces[them][3] | ai.pieces[them][4]))));
        add(S_THREAT_ROOK, sign * static_cast<float>(popcount(ai.by[c][3] & ai.pieces[them][4])));
        add(S_HANGING, sign * static_cast<float>(popcount(ai.all[c] & mm & ~ai.all[them])));

        // King safety: danger table (fixed constant) + shield (tuned).
        int units = ai.king_units[them];
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
            fixed_mg_ -= sign * static_cast<float>(SafetyTable[std::min(units, 63)]);
        if (rel_rank(our_k, white) <= 1) {
            const int kf = std::clamp(our_k % 8, 1, 6);
            for (int f = kf - 1; f <= kf + 1; ++f) {
                const U64 file = file_mask(f);
                const U64 own = file & ours_p;
                if (!own) add(S_SHIELD_MISSING, sign);
                else {
                    const int nearest = white ? lsb(own) : 63 - std::countl_zero(own);
                    if (rel_rank(nearest, white) >= 2) add(S_SHIELD_RANK3, sign);
                }
                if (!(file & (ours_p | theirs_p))) add(S_SHIELD_OPEN, sign);
            }
        }

        // Space.
        const U64 area = (file_mask(2) | file_mask(3) | file_mask(4) | file_mask(5)) &
                         (white ? (rank_mask(1) | rank_mask(2) | rank_mask(3)) : (rank_mask(6) | rank_mask(5) | rank_mask(4)));
        const U64 safe_sq = area & ~ours_p & ~ai.by[them][0];
        U64 behind = ours_p;
        for (int i = 0; i < 3; ++i) behind |= white ? (behind >> 8) : (behind << 8);
        add(S_SPACE, sign * static_cast<float>(popcount(safe_sq) + popcount(safe_sq & behind)));

        // Tempo (only once, for the side to move).
        if (c == 0) add(S_TEMPO, b.sideToMove() == chess::Color::WHITE ? 1.f : -1.f);
    }
};

// ---------------------------------------------------------------------------
// Linear eval and loss
// ---------------------------------------------------------------------------
inline double sigmoid(double k, double e) { return 1.0 / (1.0 + std::exp(-k * e)); }

double loss(const Dataset& d, const Weights& w, double k, std::size_t from, std::size_t to) {
    double sum = 0;
    for (std::size_t i = from; i < to; ++i) {
        const double diff = d.pos[i].result - sigmoid(k, linear_eval(d, i, w));
        sum += diff * diff;
    }
    return sum / static_cast<double>(to - from);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
void print_weights(const Weights& w) {
    auto r = [](double x) { return static_cast<int>(std::lround(x)); };
    const char* names[6] = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};
    std::printf("\n// ===== Tuned values (paste into Eval.h) =====\n");
    for (int ph = 0; ph < 2; ++ph)
        for (int t = 0; t < 6; ++t) {
            std::printf("inline constexpr Table %s%s = {\n", names[t], ph == 0 ? "MG" : "EG");
            for (int row = 0; row < 8; ++row) {
                std::printf("    ");
                for (int f = 0; f < 8; ++f) {
                    const int i = S_PST + t * 64 + row * 8 + f;
                    std::printf("%4d,", r(ph == 0 ? w.mg[i] : w.eg[i]));
                }
                std::printf("   // rank %d\n", 8 - row);
            }
            std::printf("};\n");
        }
    const char* mob[4] = {"Knight", "Bishop", "Rook", "Queen"};
    const int base[4] = {KnightMobility.base, BishopMobility.base, RookMobility.base, QueenMobility.base};
    for (int i = 0; i < 4; ++i)
        std::printf("inline constexpr MobilityWeight %sMobility = {%d, %d, %d};\n", mob[i], r(w.mg[S_MOB + i]), r(w.eg[S_MOB + i]), base[i]);
    std::printf("inline constexpr Score PassedMG[8] = {");
    for (int i = 0; i < 8; ++i) std::printf("%d%s", r(w.mg[S_PASSED + i]), i < 7 ? ", " : "};\n");
    std::printf("inline constexpr Score PassedEG[8] = {");
    for (int i = 0; i < 8; ++i) std::printf("%d%s", r(w.eg[S_PASSED + i]), i < 7 ? ", " : "};\n");
    std::printf("inline constexpr Score PassedFreePathEG[8] = {");
    for (int i = 0; i < 8; ++i) std::printf("%d%s", r(w.eg[S_PASSED_FREE + i]), i < 7 ? ", " : "};\n");
    auto pr = [&](const char* type, const char* name, int s) {
        std::printf("inline constexpr %s %s = {%d, %d};\n", type, name, r(w.mg[s]), r(w.eg[s]));
    };
    pr("MgEg", "PassedProtected", S_PASSED_PROT);
    pr("MgEg", "PassedBlocked", S_PASSED_BLOCK);
    std::printf("inline constexpr Score PassedKingDistEG = %d;\n", r(w.eg[S_PASSED_KDIST]));
    pr("MgEg", "RookBehindPasser", S_ROOK_BEHIND);
    std::printf("inline constexpr Score ShieldRank3 = %d, ShieldMissing = %d, ShieldOpenFile = %d;\n",
                r(w.mg[S_SHIELD_RANK3]), r(w.mg[S_SHIELD_MISSING]), r(w.mg[S_SHIELD_OPEN]));
    pr("MgEg", "IsolatedPawn", S_ISOLATED); pr("MgEg", "DoubledPawn", S_DOUBLED); pr("MgEg", "BackwardPawn", S_BACKWARD);
    pr("MgEg", "BishopPair", S_BISHOP_PAIR); pr("MgEg", "BadBishopPerPawn", S_BAD_BISHOP);
    pr("MgEg", "RookOpenFile", S_ROOK_OPEN); pr("MgEg", "RookSemiOpenFile", S_ROOK_SEMI); pr("MgEg", "RookOnSeventh", S_ROOK_7TH);
    pr("MgEg", "ThreatByPawn", S_THREAT_PAWN); pr("MgEg", "ThreatByMinor", S_THREAT_MINOR);
    pr("MgEg", "ThreatByRook", S_THREAT_ROOK); pr("MgEg", "HangingPiece", S_HANGING);
    std::printf("inline constexpr Score Tempo = %d;\n", r(w.mg[S_TEMPO]));
    std::printf("inline constexpr Score SpacePerSquare = %d;\n", r(w.mg[S_SPACE]));
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tuner <data.txt> [epochs=450] [lr=1.0] [min_count=3000] [l2=1e-7]\n");
        return 1;
    }
    const int epochs = argc > 2 ? std::atoi(argv[2]) : 450;
    const double lr = argc > 3 ? std::atof(argv[3]) : 1.0;
    const double min_count = argc > 4 ? std::atof(argv[4]) : 3000;
    const double l2 = argc > 5 ? std::atof(argv[5]) : 1e-7;

    // Load and extract.
    std::ifstream in(argv[1]);
    std::vector<std::pair<std::string, float>> rows;
    for (std::string line; std::getline(in, line);) {
        const auto bar = line.find('|');
        if (bar == std::string::npos) continue;
        rows.emplace_back(line.substr(0, bar), std::stof(line.substr(bar + 1)));
    }
    std::mt19937 rng(12345);
    std::shuffle(rows.begin(), rows.end(), rng);

    Extractor ex;
    Dataset& d = ex.data;
    Weights w = initial_weights();
    int mismatches = 0, max_diff = 0;
    for (const auto& [fen, res] : rows) {
        const chess::Board b(fen);
        ex.extract(b, res);
        // Check: the linear model with the current weights must reproduce the engine's eval.
        const int engine = evaluate(b) * (b.sideToMove() == chess::Color::WHITE ? 1 : -1);
        const int lin = static_cast<int>(std::lround(linear_eval(d, d.pos.size() - 1, w)));
        const int diff = std::abs(lin - engine);
        max_diff = std::max(max_diff, diff);
        if (diff > 3) ++mismatches;
    }
    rows.clear();
    rows.shrink_to_fit();
    std::fprintf(stderr, "positions %zu, features %zu (%.1f / position)\n", d.pos.size(), d.pool.size(),
                 static_cast<double>(d.pool.size()) / static_cast<double>(d.pos.size()));
    std::fprintf(stderr, "model check vs eval::evaluate: max |diff| %d cp, positions off by >3 cp: %d\n", max_diff, mismatches);
    if (mismatches > static_cast<int>(d.pos.size() / 1000)) {
        std::fprintf(stderr, "feature extraction doesn't match the engine eval; aborting\n");
        return 1;
    }

    const std::size_t n = d.pos.size(), split = n * 9 / 10;

    // Freeze rarely-seen features (phase-weighted counts).
    std::vector<double> seen_mg(S_COUNT, 0.0), seen_eg(S_COUNT, 0.0);
    for (std::size_t i = 0; i < split; ++i) {
        const double pm = static_cast<double>(d.pos[i].phase) / PhaseMax;
        for (std::uint32_t k = d.pos[i].first; k < d.pos[i].first + d.pos[i].count; ++k) {
            seen_mg[d.pool[k].slot] += pm;
            seen_eg[d.pool[k].slot] += 1.0 - pm;
        }
    }
    seen_mg[S_TEMPO] = static_cast<double>(split);  // untapered
    int frozen = 0;
    for (int s = 0; s < S_COUNT; ++s) {
        if (!w.freeze_mg[s] && seen_mg[static_cast<std::size_t>(s)] < min_count) { w.freeze_mg[s] = true; ++frozen; }
        if (!w.freeze_eg[s] && seen_eg[static_cast<std::size_t>(s)] < min_count) { w.freeze_eg[s] = true; ++frozen; }
    }
    std::fprintf(stderr, "frozen for too little data (< %.0f phase-weighted positions): %d weights\n", min_count, frozen);
    const Weights start = w;

    // Fit K (golden-section search) with the current weights.
    double lo = 0.0005, hi = 0.02;
    for (int it = 0; it < 60; ++it) {
        const double a = lo + (hi - lo) * 0.382, b2 = lo + (hi - lo) * 0.618;
        if (loss(d, w, a, 0, split) < loss(d, w, b2, 0, split)) hi = b2; else lo = a;
    }
    const double K = (lo + hi) / 2;
    std::fprintf(stderr, "K = %.6f  (per cp; %.2f in 400-cp units)\n", K, K * 400 / std::log(10.0));
    std::fprintf(stderr, "start: train loss %.6f, validation loss %.6f\n", loss(d, w, K, 0, split), loss(d, w, K, split, n));

    // Adam.
    std::vector<double> gm(S_COUNT), ge(S_COUNT), m1m(S_COUNT), m2m(S_COUNT), m1e(S_COUNT), m2e(S_COUNT);
    const double b1 = 0.9, b2 = 0.999, eps = 1e-8;
    for (int ep = 1; ep <= epochs; ++ep) {
        std::fill(gm.begin(), gm.end(), 0.0);
        std::fill(ge.begin(), ge.end(), 0.0);
        for (std::size_t i = 0; i < split; ++i) {
            const Position& p = d.pos[i];
            const double s = sigmoid(K, linear_eval(d, i, w));
            const double g = (s - p.result) * s * (1 - s) * K * p.fifty;  // d loss / d eval (x fifty)
            const double gmg = g * p.phase / PhaseMax, geg = g * (PhaseMax - p.phase) / PhaseMax;
            for (std::uint32_t k = p.first; k < p.first + p.count; ++k) {
                const Entry& e = d.pool[k];
                if (e.slot == S_TEMPO) { gm[S_TEMPO] += g * e.coef; continue; }
                gm[e.slot] += gmg * e.coef;
                ge[e.slot] += geg * e.coef;
            }
        }
        const double t1 = 1 - std::pow(b1, ep), t2 = 1 - std::pow(b2, ep);
        for (int s = 0; s < S_COUNT; ++s) {
            const double g1 = gm[static_cast<std::size_t>(s)] / static_cast<double>(split) + 2 * l2 * (w.mg[s] - start.mg[s]);
            const double g2 = ge[static_cast<std::size_t>(s)] / static_cast<double>(split) + 2 * l2 * (w.eg[s] - start.eg[s]);
            auto& a1 = m1m[static_cast<std::size_t>(s)]; auto& a2 = m2m[static_cast<std::size_t>(s)];
            auto& c1 = m1e[static_cast<std::size_t>(s)]; auto& c2 = m2e[static_cast<std::size_t>(s)];
            a1 = b1 * a1 + (1 - b1) * g1; a2 = b2 * a2 + (1 - b2) * g1 * g1;
            c1 = b1 * c1 + (1 - b1) * g2; c2 = b2 * c2 + (1 - b2) * g2 * g2;
            if (!w.freeze_mg[s]) w.mg[s] -= lr * (a1 / t1) / (std::sqrt(a2 / t2) + eps);
            if (!w.freeze_eg[s]) w.eg[s] -= lr * (c1 / t1) / (std::sqrt(c2 / t2) + eps);
        }
        if (ep % 50 == 0 || ep == epochs)
            std::fprintf(stderr, "epoch %4d: train loss %.6f, validation loss %.6f\n", ep, loss(d, w, K, 0, split),
                         loss(d, w, K, split, n));
    }
    print_weights(w);
}
