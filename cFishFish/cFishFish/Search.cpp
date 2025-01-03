#include "Search.h"


int Search::PVS(int alpha, int beta, int depth, int ply_deep) {

    if (checkTimeout())
        return -312312;

    if (board->isRepetition())
        return 0;

    entry node = TT.transposition_search(board->zobrist());
    if (nodeTableBreak(node, alpha, beta, depth)) {
        TB_hits++;
        return node.score;
    }

    nodes++;

    int winLossDraw = mateScore(ply_deep);
    if (winLossDraw < 1)
        return winLossDraw;

    if (depth <= 3) {
        return negamax(alpha, beta, depth, ply_deep);
    }

    bool inCheck = board->inCheck();

    //NULL MOVE PRUNE
    //DO NOT PRUNE IF IN CHECK
    //ONLY PRUNE IN NULL WINDOWS
    if (depth > 4 && std::abs(alpha - beta) == 1 && !inCheck && !Eval::onlyPawns(board)) {
        int staticEval = Eval::evaluate(board);
        //DO NOT NULL PRUNE DRAWS, ONLY NULL PRUNE WHEN STATIC EVAL IS GREATER THAN OR EQUAL TO BETA
        if (staticEval != 0 && staticEval >= beta) {
            int R = depth > 6 ? 4 : 3;

            board->makeNullMove();
            int s = -PVS(-beta, -beta + 1, depth - R - 1, 0);
            board->unmakeNullMove();

            if (s >= beta)
                return s;
        }
    }

    int alpha_orig = alpha;

    Move table_move = 0;
    Move best_move = 0;
    if (node.best != 0) {
        table_move = node.best;
    }

    int i = 0;

    //Play the table move
    if (table_move != 0) {
        board->makeMove(table_move);
        int score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1);
        board->unmakeMove(table_move);

        if (score > alpha) {
            alpha = score;
            best_move = table_move;
        }

        i++;

    }

    //Play other moves
    if (alpha < beta) {
        std::vector<ScoredMove> ordered_moves = orderAll(table_move, depth);

        for (ScoredMove scoredMove : ordered_moves) {

            int score = 0;

            Move move = scoredMove.move;

            board->makeMove(move);

            //Late move reductions
            //Do not reduce when in check
            //do not reduce moves that are checks
            int LMR = 0;
            if (!inCheck && !board->inCheck()) {
                LMR = (int)(depth / 4);
            }

            if (i == 0) {
                score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1);
            }
            else {
                //Search with a null window until alpha improves
                score = -PVS(-alpha - 1, -alpha, depth - 1 - LMR, ply_deep + 1);

                //re-search required
                if (score > alpha && beta - alpha > 1) {
                    score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1);
                }
            }

            board->unmakeMove(move);


            if (score > alpha) {
                alpha = score;
                best_move = move;

                if (alpha >= beta) {

                    bool capture = !(board->at(move.to()) == Piece::NONE);
                    if (!capture) {
                        history_hueristic[move.from().index()][move.to().index()] += depth * depth;

                        if (killer_move[depth] == 0) {
                            killer_move[depth] = move;
                        }

                    }

                    break;

                }

            }

            i++;


        }

    }

    nodeType type = EXACT;

    //Fail-high (Cut-node)
    if (alpha >= beta) {
        type = CUT;
    }
    //Fail Low (All-node)
    else if (alpha == alpha_orig) {
        type = ALL;
    }

    entry insert_node = { depth, type, best_move, alpha };

    TT.transposition_entry(board->zobrist(), insert_node);

    return std::min(alpha, beta);

}

int Search::negamax(int alpha, int beta, int depth, int ply_deep) {

    if (checkTimeout())
        return -312312;

    if (board->isRepetition())
        return 0;

    entry node = TT.transposition_search(board->zobrist());
    if (nodeTableBreak(node, alpha, beta, depth)) {
        TB_hits++;
        return node.score;
    }

    nodes++;

    int winLossDraw = mateScore(ply_deep);
    if (winLossDraw < 1) {
        return winLossDraw;
    }

    if (depth <= 0) {
        return qSearch(alpha, beta, 7);
    }

    bool inCheck = board->inCheck();

    //Razoring
    //if our position is really poor, we do not need to investigate at low depth nodes
    //We do not razor if we are in check.
    if (!board->inCheck()) {
        if (depth == 1) {
            int staticEval = Eval::evaluate(board);

            int value = staticEval + (82 * 2);
            if (value < alpha) {
                return std::max(qSearch(alpha, beta, 5), value);
            }
        }
        if (depth == 2) {
            int staticEval = Eval::evaluate(board);

            int value = staticEval + (82 * 5);
            if (value < alpha) {
                return std::max(qSearch(alpha, beta, 5), value);
            }
        }
    }

    int alpha_orig = alpha;

    Move table_move = 0;
    Move best_move = 0;
    if (node.best != 0) {
        table_move = node.best;
    }

    //Play the table move
    if (table_move != 0) {
        board->makeMove(table_move);
        int score = -negamax(-beta, -alpha, depth - 1, ply_deep + 1);
        board->unmakeMove(table_move);

        if (score > alpha) {
            alpha = score;
            best_move = table_move;
        }

    }

    //Play other moves
    if (alpha < beta) {
        std::vector<ScoredMove> ordered_captures = orderCaptures(table_move);

        //Winning and Equal Captures
        for (ScoredMove scoredMove : ordered_captures) {

            if (scoredMove.score <= 0)
                continue;

            Move move = scoredMove.move;

            board->makeMove(move);

            int score = -negamax(-beta, -alpha, depth - 1, ply_deep + 1);

            board->unmakeMove(move);

            if (score > alpha) {
                alpha = score;
                best_move = move;

                //Fail-hard beta cut-off
                if (alpha >= beta) {
                    break;
                }

            }

        }

        //Quiet moves
        if (alpha < beta) {

            std::vector<ScoredMove> ordered_moves = orderQuiet(table_move, depth);

            for (ScoredMove scoredMove : ordered_moves) {

                Move move = scoredMove.move;

                board->makeMove(move);

                int score = -negamax(-beta, -alpha, depth - 1, ply_deep + 1);

                board->unmakeMove(move);

                if (score > alpha) {
                    alpha = score;
                    best_move = move;

                    //Fail-hard beta cut-off
                    if (alpha >= beta) {

                        if (killer_move[depth] == 0) {
                            killer_move[depth] = move;
                        }

                        break;
                    }

                }

            }

        }

        if (alpha < beta) {
            //Losing Captures
            for (ScoredMove scoredMove : ordered_captures) {

                if (scoredMove.score > 0)
                    continue;

                Move move = scoredMove.move;

                board->makeMove(move);

                int score = -negamax(-beta, -alpha, depth - 1, ply_deep + 1);

                board->unmakeMove(move);

                if (score > alpha) {
                    alpha = score;
                    best_move = move;

                    //Fail-hard beta cut-off
                    if (alpha >= beta) {
                        break;
                    }

                }

            }
        }


    }

    nodeType type = EXACT;

    //Fail-high (Cut-node)
    if (alpha >= beta) {
        type = CUT;
    }
    //Fail Low (All-node)
    else if (alpha == alpha_orig) {
        type = ALL;
    }

    entry insert_node = { depth, type, best_move, alpha };

    TT.transposition_entry(board->zobrist(), insert_node);

    return std::min(alpha, beta);

}

int Search::qSearch(int alpha, int beta, int depth) {
    if (checkTimeout())
        return -312312;


    nodes++;

    int static_eval = Eval::evaluate(board);

    if (depth <= 0) {
        return static_eval;
    }

    if (static_eval >= beta)
        return beta;

    if (alpha - 1125 > static_eval)
        return alpha;

    if (alpha < static_eval)
        alpha = static_eval;

    std::vector<ScoredMove> ordered_captures = orderCaptures(0);

    //Captures
    for (ScoredMove scoredMove : ordered_captures) {

        Move m = scoredMove.move;

        int to = order_score[board->at(m.to())];
        int from = order_score[board->at(m.from())];

        if (to + static_eval + 200 < alpha)
            continue;

        Color defender = ~board->sideToMove();

        //Hanging
        if (board->isAttacked(m.to().index(), defender) && to + 200 < from)
            continue;

        board->makeMove(m);
        int score = -qSearch(-beta, -alpha, depth - 1);
        board->unmakeMove(m);

        if (score > alpha) {
            alpha = score;

            if (alpha >= beta)
                return beta;

        }

    }

    std::vector<ScoredMove> ordered_checks = orderChecks();
    for (ScoredMove scoredMove : ordered_checks) {

        Move m = scoredMove.move;

        board->makeMove(m);
        int score = -qSearch(-beta, -alpha, depth - 1);
        board->unmakeMove(m);

        if (score > alpha) {
            alpha = score;

            if (alpha >= beta)
                return beta;

        }

    }


    return alpha;

}

int Search::mateScore(int plydeep) {
    if (!hasLegalMoves()) {

        if (board->inCheck()) {
            return -999999 + plydeep;
        }

        return 0;
    }

    return 2;
}

bool Search::nodeTableBreak(entry node, int alpha, int beta, int depth) {
    if (node.depth >= depth) {

        //this is the PV node.
        if (node.type == EXACT) {
            return true;
        }
        //Fail-high (Cut-node)
        if (node.type == CUT) {
            return node.score >= beta;
        }
        //Fail-low (All-Node)
        if (node.type == ALL) {
            return node.score <= alpha;
        }
    }

    return false;
}

bool Search::hasLegalMoves() {

    Movelist moves;
    movegen::legalmoves(moves, *board, PieceGenType::KING);

    if (moves.size() != 0)
        return true;

    movegen::legalmoves(moves, *board, PieceGenType::KNIGHT);

    if (moves.size() != 0)
        return true;

    movegen::legalmoves(moves, *board, PieceGenType::BISHOP);

    if (moves.size() != 0)
        return true;

    movegen::legalmoves(moves, *board, PieceGenType::ROOK);

    if (moves.size() != 0)
        return true;

    movegen::legalmoves(moves, *board, PieceGenType::QUEEN);

    if (moves.size() != 0)
        return true;

    movegen::legalmoves(moves, *board, PieceGenType::PAWN);

    if (moves.size() != 0)
        return true;

    return false;

}

std::vector<ScoredMove> Search::orderAll(Move table, int depth) {

    Movelist moves;
    movegen::legalmoves(moves, *board);

    std::vector<ScoredMove> orderedMoves;

    for (Move m : moves) {

        //Table PV move already tested
        if (m == table)
            continue;

        int score = Eval::PsqM(board, m);

        if (board->at(m.to()) != Piece::NONE) {
            int to = order_score[board->at(m.to())];
            int from = order_score[board->at(m.from())];

            Color defender = ~board->sideToMove();

            //Hanging
            if (!board->isAttacked(m.to(), defender)) {
                score += 20000 + to;
            }
            //Winning capture
            else if (to > from) {
                score += 20000 + (to - from);
            }
            else if (std::abs(to - from) < 30) {
                score += 5000 + to - from;
            }
            else
                score += -2000 + (to - from);
        }
        else {
            if (killer_move[depth] == m) {
                score += 3200;
            }
            else if (killer_move[depth + 1] == m) {
                score += 3100;
            }
            else if (depth > 1 && killer_move[depth - 1] == m) {
                score += 3000;
            }

            score += history_hueristic[m.from().index()][m.to().index()];
        }

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves.push_back(sm);

    }

    std::sort(orderedMoves.begin(), orderedMoves.end(), ScoredMove::comp);

    return orderedMoves;

}

std::vector<ScoredMove> Search::orderCaptures(Move table) {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, *board);

    std::vector<ScoredMove> orderedMoves;

    for (Move m : moves) {

        //Table PV move already tested
        if (m == table)
            continue;

        int score = Eval::PsqM(board, m);

        int to = order_score[board->at(m.to())];
        int from = order_score[board->at(m.from())];

        Color defender = ~board->sideToMove();

        //Hanging
        if (!board->isAttacked(m.to().index(), defender)) {
            score += 20000 + to;
        }
        //Winning capture
        else if (to > from) {
            score += 20000 + (to - from);
        }
        else if (std::abs(to - from) < 30) {
            score += 5000 + to - from;
        }
        else
            score += -2000 + (to - from);

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves.push_back(sm);

    }

    std::sort(orderedMoves.begin(), orderedMoves.end(), ScoredMove::comp);

    return orderedMoves;

}

std::vector<ScoredMove> Search::orderQuiet(Move table, int depth) {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::QUIET>(moves, *board);

    std::vector<ScoredMove> orderedMoves;

    for (Move m : moves) {

        //Table PV move already tested
        if (m == table)
            continue;

        int score = Eval::PsqM(board, m);

        if (killer_move[depth] == m) {
            score += 3200;
        }
        else if (killer_move[depth + 1] == m) {
            score += 3100;
        }
        else if (depth > 1 && killer_move[depth - 1] == m) {
            score += 3000;
        }

        score += history_hueristic[m.from().index()][m.to().index()];

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves.push_back(sm);

    }

    std::sort(orderedMoves.begin(), orderedMoves.end(), ScoredMove::comp);

    return orderedMoves;

}

std::vector<ScoredMove> Search::orderChecks() {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::QUIET>(moves, *board);

    std::vector<ScoredMove> orderedMoves;

    bool check = board->inCheck();

    for (Move m : moves) {

        int score = Eval::PsqM(board, m);

        board->makeMove(m);
        check = check || board->inCheck();
        board->unmakeMove(m);

        if (!check)
            continue;

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves.push_back(sm);

    }

    std::sort(orderedMoves.begin(), orderedMoves.end(), ScoredMove::comp);

    return orderedMoves;

}