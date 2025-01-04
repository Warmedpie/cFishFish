#pragma once
#include "Chess.h"

using namespace chess;

class ScoredMove {

public:
    int score;
    Move move;

    static bool comp(ScoredMove a, ScoredMove b) {
        return a.score >= b.score;
    }
};