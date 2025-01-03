#pragma once
#include "Chess.h"

using namespace chess;

class Eval {

public:

    //Evaluation function
    static float whitePhase(Board* board);
    static float blackPhase(Board* board);
    static int winnable(Board* board);
    static int isKingSquare(Board* board, int sq);
    static int pieceMobility(PieceType pt, Board* board, int sq);
    static bool attackedBy(Board* board, Square sq, PieceType pt, Color side);
    static int evaluate(Board* board);

    //Engine util functions
    static int PsqM(Board* board, Move m);
    static bool onlyPawns(Board* board);

};