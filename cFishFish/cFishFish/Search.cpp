#include "Search.h"

constexpr int order_score[13] = { 82, 337, 365, 477, 1025, 0, 82, 337, 365, 477, 1025, 0, 0 };

int Search::entryPoint(int alpha, int beta, int depth, int ply_deep, std::vector<Move> ignore, Move prev) {

    if (checkTimeout())
        return -312312;

    if (board->isRepetition())
        return 0;

    nodes++;

    int winLossDraw = mateScore(ply_deep);
    if (winLossDraw < 1)
        return winLossDraw;

    entry node = TT.transposition_search(board->zobrist());

    Move multi_move = multipv[ignore.size()];
    Move table_move = node.best;
    Move best_move = 0;

    if (contains(ignore, table_move))
        table_move = multi_move;

    Move counter_killer = TT.transposition_search_no_adjust((long)(prev.to().index() * 64 + prev.from().index())).best;

    //Order Moves
    std::vector<ScoredMove> ordered_moves = orderAll(table_move, depth, counter_killer);

    for (ScoredMove scoredMove : ordered_moves) {

        Move move = scoredMove.move;

        if (contains(ignore, move))
            continue;

        bool capture = board->at(move.to()) != Piece::NONE;

        board->makeMove(move);

        int score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { move, capture });

        board->unmakeMove(move);

        if (score > alpha) {
            alpha = score;
            best_move = move;
        }

    }

    entry insert_node = { depth, EXACT, best_move, alpha };

    if (ignore.size() == 0)
        TT.transposition_entry(board->zobrist(), insert_node);

    multipv[ignore.size()] = best_move;

    return std::min(alpha, beta);

}

int Search::PVS(int alpha, int beta, int depth, int ply_deep, MOVE prev) {

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

    if (depth <= 0) {
        return qSearch(alpha, beta, 20);
    }

    bool inCheck = board->inCheck();

    //Razoring
    //if our position is really poor, we do not need to investigate at low depth nodes
    //We do not razor if we are in check.
    if (!inCheck) {
        if (depth == 1) {
            int value = Eval::evaluate(board, this->search_time) + 200;
            if (value < alpha) {
                return std::max(qSearch(alpha, beta, 9), value);
            }
        }
        if (depth == 2) {
            int value = Eval::evaluate(board, this->search_time) + 500;
            if (value < alpha) {
                return std::max(qSearch(alpha, beta, 9), value);
            }
        }
        if (depth == 3) {
            int value = Eval::evaluate(board, this->search_time) + 900;
            if (value < alpha) {
                return std::max(qSearch(alpha, beta, 9), value);
            }
        }
    }

    Move threat_move = 0;

    //NULL MOVE PRUNE
    //DO NOT PRUNE IF IN CHECK
    //ONLY PRUNE IN NULL WINDOWS
    if (prev.m != 0 && depth > 3 && node.type == CUT && !inCheck && !Eval::onlyPawns(board)) {
        int static_eval = Eval::evaluate(board, this->search_time);

        //DO NOT NULL PRUNE DRAWS, ONLY NULL PRUNE WHEN STATIC EVAL IS GREATER THAN OR EQUAL TO BETA
        if (static_eval != 0 && static_eval >= beta) {
            int R = depth > 6 ? 3 : 2;

            board->makeNullMove();
            int s = -PVS(-beta, -beta + 1, depth - R - 1, 0, {0, true});

            threat_move = TT.transposition_search(board->zobrist()).best;

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

        bool capture = board->at(table_move.to()) != Piece::NONE;

        board->makeMove(table_move);
        int score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { table_move, capture });
        board->unmakeMove(table_move);

        if (score > alpha) {
            alpha = score;
            best_move = table_move;
        }

        i++;

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

            int score = 0;

            //Late move reductions
            //Do not reduce when in check
            //do not reduce moves that are checks
            int LMR = 0;
            if (depth >= 2 && i > 1 && !inCheck && !board->inCheck()) {
                //Equal value captures
                if (scoredMove.score < 10000) {
                    LMR = (int)(0.5 + std::log(depth) * std::log(i) / 2.9);
                }
                //Winning Captures
                else LMR = (int)(0.3 + std::log(depth) * std::log(i) / 3.35);
            }

            if (i == 0) {
                score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { move, true });
            }
            else {
                //Search with a null window until alpha improves
                score = -PVS(-alpha - 1, -alpha, depth - 1 - LMR, ply_deep + 1, { move, true });

                //LMR re-search
                if (score > alpha && LMR > 1) {
                    score = -PVS(-alpha - 1, -alpha, depth - 1, ply_deep + 1, { move, true });
                }
                //re-search required
                if (score > alpha && beta - alpha > 1) {
                    score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { move, true });
                }
            }

            board->unmakeMove(move);

            if (score > alpha) {
                alpha = score;
                best_move = move;

                //Fail-hard beta cut-off
                if (alpha >= beta) {
                    break;
                }

            }

            i++;

        }

        //Quiet moves
        if (alpha < beta) {
            Move threat_killer = TT.transposition_search_no_adjust((long)(threat_move.to().index() * 64 + threat_move.from().index())).best;
            Move counter_killer = TT.transposition_search_no_adjust((long)(prev.m.to().index() * 64 + prev.m.from().index())).best;

            std::vector<ScoredMove> ordered_moves = orderQuiet(table_move, depth, threat_killer, counter_killer);

            for (ScoredMove scoredMove : ordered_moves) {

                Move move = scoredMove.move;

                board->makeMove(move);

                int score = 0;
                //Late move reductions
                //Do not reduce when in check
                //do not reduce moves that are checks
                int LMR = 0;
                if (depth >= 2 && i > 1 && !inCheck && !board->inCheck()) {
                    LMR = (int)(0.7844 + std::log(depth) * std::log(i) / 2.4696);
                }

                Piece from = board->at(move.from());

                int ext = 0;

                //passed pawn push causes a search extension
                if (from == Piece::WHITEPAWN && Eval::isPassed(board, Color::WHITE, move.to().file())) {
                    ext = 1;
                    LMR = 0;
                }
                if (from == Piece::BLACKPAWN && Eval::isPassed(board, Color::BLACK, move.to().file())) {
                    ext = 1;
                    LMR = 0;
                }


                if (i == 0) {
                    score = -PVS(-beta, -alpha, depth - 1 + ext, ply_deep + 1, { move, false });
                }
                else {
                    //Search with a null window until alpha improves
                    score = -PVS(-alpha - 1, -alpha, depth - 1 - LMR + ext, ply_deep + 1, { move, false });
                    //LMR re-search
                    if (score > alpha && LMR > 1) {
                        score = -PVS(-alpha - 1, -alpha, depth - 1 + ext, ply_deep + 1, { move, false });
                    }
                    //re-search required
                    if (score > alpha && beta - alpha > 1) {
                        score = -PVS(-beta, -alpha, depth - 1 + ext, ply_deep + 1, { move, false });
                    }
                }

                board->unmakeMove(move);

                if (score > alpha) {
                    alpha = score;
                    best_move = move;

                    //Fail-hard beta cut-off
                    if (alpha >= beta) {

                        history_hueristic[move.from().index()][move.to().index()] += depth;

                        if (killer_move[depth] == 0) {
                            killer_move[depth] = move;
                        }

                        break;
                    }

                }

                i++;
            }

        }

        if (alpha < beta) {
            //Losing Captures
            for (ScoredMove scoredMove : ordered_captures) {

                if (scoredMove.score > 0)
                    continue;

                Move move = scoredMove.move;

                board->makeMove(move);

                int score = 0;
                //Late move reductions
                //Do not reduce when in check
                //do not reduce moves that are checks
                int LMR = 0;
                if (depth >= 2 && i > 1 && !inCheck && !board->inCheck()) {
                    LMR = (int)(0.7844 + std::log(depth) * std::log(i) / 2.4696);
                }

                if (i == 0) {
                    score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { move, true });
                }
                else {
                    //Search with a null window until alpha improves
                    score = -PVS(-alpha - 1, -alpha, depth - 1 - LMR, ply_deep + 1, { move, true });
                    //LMR re-search
                    if (score > alpha && LMR > 1) {
                        score = -PVS(-alpha - 1, -alpha, depth - 1, ply_deep + 1, { move, true });
                    }
                    //re-search required
                    if (score > alpha && beta - alpha > 1) {
                        score = -PVS(-beta, -alpha, depth - 1, ply_deep + 1, { move, true });
                    }
                }

                board->unmakeMove(move);

                if (score > alpha) {
                    alpha = score;
                    best_move = move;

                    //Fail-hard beta cut-off
                    if (alpha >= beta) {
                        break;
                    }

                }

                i++;
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
    if (threat_move != 0) {
        TT.transposition_entry((long)(threat_move.to().index() * 64 + threat_move.from().index()), insert_node);
    }

    if (prev.m != 0) {
        TT.transposition_entry((long)(prev.m.to().index() * 64 + prev.m.from().index()), insert_node);
    }

    return std::min(alpha, beta);

}

int Search::qSearch(int alpha, int beta, int depth) {
    if (checkTimeout())
        return -312312;


    nodes++;

    int static_eval = Eval::evaluate(board, this->search_time);

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
        int score = -qSearch(-beta, -alpha, std::min(7, depth - 1));
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

std::vector<ScoredMove> Search::orderAll(Move table, int depth, Move counter) {

    Movelist moves;
    movegen::legalmoves(moves, *board);

    ScoredMove orderedMoves[moves.size()];

    int i = 0;
    for (Move m : moves) {

        //Table PV move not already tested in root func call
        if (m == table) {

            ScoredMove sm;
            sm.score = 999999999;
            sm.move = m;

            orderedMoves[i++] = sm;

            continue;

        }

        //Internal iterative deepening
        if (depth > 3) {
            ScoredMove sm = {0 ,0};
            for (int d = 1; d < depth / 3; d++) {

                board->makeMove(m);

                int score = -entryPoint(-99999999, 99999999, d, 0, {}, m);

                board->unmakeMove(m);

                sm.score = score;
                sm.move = m;

            }

            if (sm.move != 0) {
                orderedMoves[i++] = sm;
                continue;
            }

        }

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
                score += 300;
            }
            else if (killer_move[depth + 1] == m) {
                score += 200;
            }
            else if (depth > 1 && killer_move[depth - 1] == m) {
                score += 100;
            }

            else if (m == counter) {
                score += 50;
            }

            score += history_hueristic[m.from().index()][m.to().index()];
        }

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves[i++] = sm;

    }

    std::sort(orderedMoves, orderedMoves + i, ScoredMove::comp);

    std::vector<ScoredMove> data(orderedMoves, orderedMoves + i);

    return data;

}

std::vector<ScoredMove> Search::orderCaptures(Move table) {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, *board);

    ScoredMove orderedMoves[moves.size()];

    int i = 0;
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

        orderedMoves[i++] = sm;

    }

    std::sort(orderedMoves, orderedMoves + i, ScoredMove::comp);

    std::vector<ScoredMove> data(orderedMoves, orderedMoves + i);

    return data;

}

std::vector<ScoredMove> Search::orderQuiet(Move table, int depth, Move threat, Move counter) {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::QUIET>(moves, *board);

    ScoredMove orderedMoves[moves.size()];

    int i = 0;
    for (Move m : moves) {

        //Table PV move already tested
        if (m == table)
            continue;

        int score = Eval::PsqM(board, m);

        if (killer_move[depth] == m) {
            score += 300;
        }
        else if (killer_move[depth + 1] == m) {
            score += 200;
        }
        else if (depth > 1 && killer_move[depth - 1] == m) {
            score += 100;
        }

        else if (m == threat) {
            score += 50;
        }

        else if (m == counter) {
            score += 50;
        }

        score += history_hueristic[m.from().index()][m.to().index()];

        ScoredMove sm;
        sm.score = score;
        sm.move = m;

        orderedMoves[i++] = sm;

    }

    std::sort(orderedMoves, orderedMoves + i, ScoredMove::comp);

    std::vector<ScoredMove> data(orderedMoves, orderedMoves + i);

    return data;

}

std::vector<ScoredMove> Search::orderChecks() {

    Movelist moves;
    movegen::legalmoves<movegen::MoveGenType::QUIET>(moves, *board);

    ScoredMove orderedMoves[moves.size()];

    bool check = board->inCheck();

    int i = 0;
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

        orderedMoves[i++] = sm;

    }

    std::sort(orderedMoves, orderedMoves + i, ScoredMove::comp);

    std::vector<ScoredMove> data(orderedMoves, orderedMoves + i);

    return data;

}