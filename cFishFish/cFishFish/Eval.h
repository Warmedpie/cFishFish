#pragma once
#include "Chess.h"

using namespace chess;

class Eval {

public:

    //Evaluation function
    static float whitePhase(Bitboard white_knight_BB, Bitboard white_bishop_BB, Bitboard white_rook_BB, Bitboard white_queen_BB);
    static float blackPhase(Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB);
    static bool winnable(Bitboard white_pawn_BB, Bitboard white_knight_BB, Bitboard white_bishop_BB, Bitboard white_rook_BB, Bitboard white_queen_BB, Bitboard black_pawn_BB, Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB, Color c = Color::NONE);
    static int pieceMobility(PieceType pt, Board* board, int sq);
    static int evaluate(Board* board);

    static Bitboard kingRing(int king_sq);

    //Engine util functions
    static int PsqM(Board* board, Move m);
    static bool onlyPawns(Board* board);

};