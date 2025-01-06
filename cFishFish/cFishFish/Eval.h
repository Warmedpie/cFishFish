#pragma once
#include "Chess.h"

using namespace chess;

class Eval {

public:

    //Evaluation function
    static float whitePhase(Board* board);
    static float blackPhase(Board* board);
    static int winnable(Board* board);
    static int pieceMobility(PieceType pt, Board* board, int sq);
    static int evaluate(Board* board);

    static Bitboard kingRing(int king_sq);

    //Engine util functions
    static int PsqM(Board* board, Move m);
    static bool onlyPawns(Board* board);

};