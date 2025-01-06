#include "Eval.h"

//Piece values by phase
static int mgPieceValues[5] = { 82, 337, 365, 477, 1025 };
static int egPieceValues[5] = { 101, 300, 319, 550, 1005 };

//Adjustment values by pawn count for knights and bishops
static int knightAdj[9] = { -20, -16, -12, -8, -4,  0,  4,  8, 12 };
static int rookAdj[9] = { 15,  12,   9,  6,  3,  0, -3, -6, -9 };

//Piece square tables
static int mgPawnTableBlack[64] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		98, 134, 61, 95, 68, 126, 34, -11,
		-6, 7, 26, 31, 65, 56, 25, -20,
		-14, 13, 6, 21, 23, 12, 17, -23,
		-27, -2, -5, 12, 17, 6, 10, -25,
		-26, -4, -4, -10, 3, 3, 33, -12,
		-35, -1, -20, -23, -15, 24, 38, -22,
		0, 0, 0, 0, 0, 0, 0, 0,
};

static int mgPawnTableWhite[64] = {
		 0, 0, 0, 0, 0, 0, 0, 0,
		-35, -1, -20, -23, -15, 24, 38, -22,
		-26, -4, -4, -10, 3, 3, 33, -12,
		-27, -2, -5, 12, 17, 6, 10, -25,
		-14, 13, 6, 21, 23, 12, 17, -23,
		-6, 7, 26, 31, 65, 56, 25, -20,
		98, 134, 61, 95, 68, 126, 34, -11,
		0, 0, 0, 0, 0, 0, 0, 0,
};

static int egPawnTableBlack[64] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		178, 173, 158, 134, 147, 132, 165, 187,
		94, 100, 85, 67, 56, 53, 82, 84,
		32, 24, 13, 5, -2, 4, 17, 17,
		13, 9, -3, -7, -7, -8, 3, -1,
		4, 7, -6, 1, 0, -5, -1, -8,
		13, 8, 8, 10, 13, 0, 2, -7,
		0, 0, 0, 0, 0, 0, 0, 0,
};

static int egPawnTableWhite[64] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		13, 8, 8, 10, 13, 0, 2, -7,
		4, 7, -6, 1, 0, -5, -1, -8,
		13, 9, -3, -7, -7, -8, 3, -1,
		32, 24, 13, 5, -2, 4, 17, 17,
		94, 100, 85, 67, 56, 53, 82, 84,
		178, 173, 158, 134, 147, 132, 165, 187,
		0, 0, 0, 0, 0, 0, 0, 0,
};

static int knightTable[64] = {
		 -50, -40, -30, -30, -30, -30, -40, -50,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-50, -40, -30, -30, -30, -30, -40, -50
};

static int bishopTableWhite[64] = {
		-33, -3, -14, -21, -13, -12, -39, -21,
		4, 15, 16, 0, 7, 21, 33, 1,
		0, 15, 15, 4, 3, 27, 18, 10,
		-6, 13, 13, 26, 34, 12, 10, 4,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-29, 4, -82, -37, -25, -42, 7, -8,
};

static int bishopTableBlack[64] = {
		-29, 4, -82, -37, -25, -42, 7, -8,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-6, 13, 13, 26, 34, 12, 10, 4,
		0, 15, 15, 15, 14, 27, 18, 10,
		4, 15, 16, 0, 7, 21, 33, 1,
		-33, -3, -14, -21, -13, -12, -39, -21,
};

static int rookTableWhite[64] = {
		-19, -13, 1, 17, 16, 7, -37, -26,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-5, 19, 26, 36, 17, 45, 61, 16,
		27, 32, 58, 62, 80, 67, 26, 44,
		32, 42, 32, 51, 63, 9, 31, 43,
};

static int rookTableBlack[64] = {
		32, 42, 32, 51, 63, 9, 31, 43,
		27, 32, 58, 62, 80, 67, 26, 44,
		-5, 19, 26, 36, 17, 45, 61, 16,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-19, -13, 1, 17, 16, 7, -37, -26,
};

static int queenTable[64] = {
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
};

static int mgKingTableWhite[64] = {
		-15, -4, -19, -54, -40, -28, 44, 14,
		1, 7, -8, -64, -43, -16, 9, 8,
		-14, -14, -22, -46, -44, -30, -15, -27,
		-49, -1, -27, -39, -46, -44, -33, -51,
		-17, -20, -12, -27, -30, -25, -14, -36,
		-9, 24, 2, -16, -20, 6, 22, -22,
		29, -1, -20, -7, -8, -4, -38, -29,
		-65, 23, 16, -15, -56, -34, 2, 13
};

static int mgKingTableBlack[64] = {
		-65, 23, 16, -15, -56, -34, 2, 13,
		29, -1, -20, -7, -8, -4, -38, -29,
		-9, 24, 2, -16, -20, 6, 22, -22,
		-17, -20, -12, -27, -30, -25, -14, -36,
		-49, -1, -27, -39, -46, -44, -33, -51,
		-14, -14, -22, -46, -44, -30, -15, -27,
		1, 7, -8, -64, -43, -16, 9, 8,
		-15, -4, -19, -54, -40, -28, 44, 14,
};

static int egKingTableWhite[64] = {
		-53, -34, -21, -11, -28, -14, -24, -43,
		-27, -11, 4, 13, 14, 4, -5, -17,
		-19, -3, 11, 21, 23, 16, 7, -9,
		-18, -4, 21, 24, 27, 23, 9, -11,
		-8, 22, 24, 27, 26, 33, 26, 3,
		10, 17, 23, 15, 20, 45, 44, 13,
		-12, 17, 14, 17, 17, 38, 23, 11,
		-74, -35, -18, -18, -11, 15, 4, -17
};

static int egKingTableBlack[64] = {
		-74, -35, -18, -18, -11, 15, 4, -17,
		-12, 17, 14, 17, 17, 38, 23, 11,
		10, 17, 23, 15, 20, 45, 44, 13,
		-8, 22, 24, 27, 26, 33, 26, 3,
		-18, -4, 21, 24, 27, 23, 9, -11,
		-19, -3, 11, 21, 23, 16, 7, -9,
		-27, -11, 4, 13, 14, 4, -5, -17,
		-53, -34, -21, -11, -28, -14, -24, -43
};

static int mateTableKing[64] = {
		-1000, -500, -500, -500, -500, -500, -500, -1000,
		-500, -500, -300, -300, -300, -300, -500, -500,
		-500, -300, -200, -200, -200, -200, -300, -500,
		-500, -300, -200, -100, -100, -200, -300, -500,
		-500, -300, -200, -100, -100, -200, -300, -500,
		-500, -300, -200, -200, -200, -200, -300, -500,
		-500, -500, -300, -300, -300, -300, -500, -500,
		-1000, -500, -500, -500, -500, -500, -500, -1000,
};

static int CENTER_MANHATTAN_DISTANCE[64] = {
		6, 5, 4, 3, 3, 4, 5, 6,
		5, 4, 3, 2, 2, 3, 4, 5,
		4, 3, 2, 1, 1, 2, 3, 4,
		3, 2, 1, 0, 0, 1, 2, 3,
		3, 2, 1, 0, 0, 1, 2, 3,
		4, 3, 2, 1, 1, 2, 3, 4,
		5, 4, 3, 2, 2, 3, 4, 5,
		6, 5, 4, 3, 3, 4, 5, 6
};


float Eval::whitePhase(Bitboard white_knight_BB, Bitboard white_bishop_BB, Bitboard white_rook_BB, Bitboard white_queen_BB) {
	int whiteMaterial =
		(white_knight_BB.count() * 3) +
		(white_bishop_BB.count() * 3) +
		(white_rook_BB.count() * 5) +
		(white_queen_BB.count() * 9);

	return std::max(0.0f, (((float)whiteMaterial - 10) / 21));
}

float Eval::blackPhase(Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB) {
	int blackMaterial =
		(black_knight_BB.count() * 3) +
		(black_bishop_BB.count() * 3) +
		(black_rook_BB.count() * 5) +
		(black_queen_BB.count() * 9);

	return std::max(0.0f, (((float)blackMaterial - 10) / 21));
}

int Eval::winnable(Bitboard white_pawn_BB, Bitboard white_knight_BB, Bitboard white_bishop_BB, Bitboard white_rook_BB, Bitboard white_queen_BB, Bitboard black_pawn_BB, Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB) {
	if (white_pawn_BB.count() != 0 || black_pawn_BB.count() != 0)
		return 1;

	if (white_queen_BB.count() != 0 || black_queen_BB.count() != 0)
		return 1;

	if (white_rook_BB.count() != 0 || black_rook_BB.count() != 0)
		return 1;


	if (white_bishop_BB.count() >= 2 || black_bishop_BB.count() >= 2)
		return 1;

	if ((white_bishop_BB != 0 && white_knight_BB != 0) || (black_bishop_BB != 0 && black_knight_BB != 0))
		return 1;

	if (white_knight_BB.count() >= 3 || black_knight_BB.count() >= 3)
		return 1;

	return 0;
}

int Eval::pieceMobility(PieceType pt, Board* b, int sq) {
	Bitboard occ_white = b->us(Color::WHITE);
	Bitboard occ_black = b->us(Color::BLACK);

	Bitboard occ_all = (occ_black | occ_white);

	switch (pt)
	{
	case 1:
		return (attacks::knight(Square(sq)) & ~occ_all).count();
	case 2:
		return (attacks::bishop(Square(sq), occ_all) & ~occ_all).count();
	case 3:
		return (attacks::rook(Square(sq), occ_all) & ~occ_all).count();
	case 4:
		return (attacks::queen(Square(sq), occ_all) & ~occ_all).count();
	default:
		break;
	}

	return 0;
}

int Eval::evaluate(Board* board) {

	//Pieces
	Bitboard white_pawn_BB = board->pieces(PieceType::PAWN, Color::WHITE);
	Bitboard black_pawn_BB = board->pieces(PieceType::PAWN, Color::BLACK);

	Bitboard white_knight_BB = board->pieces(PieceType::KNIGHT, Color::WHITE);
	Bitboard black_knight_BB = board->pieces(PieceType::KNIGHT, Color::BLACK);

	Bitboard white_bishop_BB = board->pieces(PieceType::BISHOP, Color::WHITE);
	Bitboard black_bishop_BB = board->pieces(PieceType::BISHOP, Color::BLACK);

	Bitboard white_rook_BB = board->pieces(PieceType::ROOK, Color::WHITE);
	Bitboard black_rook_BB = board->pieces(PieceType::ROOK, Color::BLACK);

	Bitboard white_queen_BB = board->pieces(PieceType::QUEEN, Color::WHITE);
	Bitboard black_queen_BB = board->pieces(PieceType::QUEEN, Color::BLACK);

	Bitboard white_king_BB = board->pieces(PieceType::KING, Color::WHITE);
	Bitboard black_king_BB = board->pieces(PieceType::KING, Color::BLACK);

	//Number of pawns each side has
	int white_pawns = white_pawn_BB.count();
	int black_pawns = black_pawn_BB.count();

	int whiteKing = white_king_BB.lsb();
	int blackKing = black_king_BB.lsb();

	//Game phase
	float white_mg = whitePhase(white_knight_BB,  white_bishop_BB,  white_rook_BB,  white_queen_BB);
	float black_mg = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);

	float phase = (white_mg + black_mg) / 2;

	//Returns draws by insufficient material
	if (phase == 0 && winnable(white_pawn_BB, white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB, black_pawn_BB, black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB) == 0)
		return 0;

	//score
	int score = 0;

	//Middle game and endgame score
	int mg_score_white = 0;
	int mg_score_black = 0;

	int eg_score_white = 0;
	int eg_score_black = 0;

	//Attackers
	int attack_white_count = 0;
	int attack_black_count = 0;

	int attack_white_weight = 0;
	int attack_black_weight = 0;

	int attack_squares_white = 0;
	int attack_squares_black = 0;

	int defender_white_knight = 0;
	int defender_black_knight = 0;

	int white_shield = 0;
	int black_shield = 0;

	Bitboard white_ring = kingRing(whiteKing) | white_king_BB;
	Bitboard black_ring = kingRing(blackKing) | black_king_BB;
	Bitboard white_bb = white_pawn_BB | white_knight_BB | white_bishop_BB | white_rook_BB | white_queen_BB;
	Bitboard black_bb = black_pawn_BB | black_knight_BB | black_bishop_BB | black_rook_BB | black_queen_BB;


	/* PIECE AND PIECE SQUARES */

	//Pawns
	while (white_pawn_BB.getBits()) {
		int sq = white_pawn_BB.pop();

		mg_score_white += mgPieceValues[0] + mgPawnTableWhite[sq];
		eg_score_white += egPieceValues[0] + egPawnTableWhite[sq];

		//Pawn shield
		if (white_ring.check(sq) && whiteKing < 8)
			white_shield++;

		if (white_ring.check(sq) && whiteKing >= 8)
			white_shield--;

		//Space
		if ((sq + 8 <= 63) && board->at(Square(sq + 8)) != Piece::WHITEPAWN) {
			score += 2;
			if ((sq + 16 <= 63) && board->at(Square(sq + 16)) != Piece::WHITEPAWN) {
				score += 2;
			}
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 1)
			mg_score_white += 4;

	}

	while (black_pawn_BB.getBits()) {
		int sq = black_pawn_BB.pop();

		mg_score_black += mgPieceValues[0] + mgPawnTableBlack[sq];
		eg_score_black += egPieceValues[0] + egPawnTableBlack[sq];

		//Pawn shield on first
		if (black_ring.check(sq) && blackKing > 55)
			black_shield++;

		if (black_ring.check(sq) && blackKing <= 55)
			black_shield--;

		//Space
		if ((sq - 8 > 0) && board->at(Square(sq - 8)) != Piece::BLACKPAWN) {
			score -= 2;
			if ((sq - 16 > 0) && board->at(Square(sq - 16)) != Piece::BLACKPAWN) {
				score -= 2;
			}
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 1)
			mg_score_black += 4;

	}

	//Re-init pawn bitboards for other pieces to use.
	white_pawn_BB = board->pieces(PieceType::PAWN, Color::WHITE);
	black_pawn_BB = board->pieces(PieceType::PAWN, Color::BLACK);

	//Knights
	while (white_knight_BB.getBits()) {
		int sq = white_knight_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[1] + knightTable[sq] + knightAdj[white_pawns];
		eg_score_white += egPieceValues[1] + knightTable[sq] + knightAdj[white_pawns];

		//First rank, encourage development
		if (sq >= 0 && sq <= 7)
			mg_score_white -= 25;

		//Mobility
		score += (3 * (pieceMobility(PieceType::KNIGHT, board, sq) - 4));

		int atkSq = (attacks::knight(thisSq) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 81;
			attack_squares_white += atkSq;
		}

		int defSq = (attacks::knight(thisSq) & white_ring).count();
		if (defSq) {
			defender_white_knight += defSq;
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 2)
			mg_score_white += 4;

	}

	while (black_knight_BB.getBits()) {
		int sq = black_knight_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[1] + knightTable[sq] + knightAdj[black_pawns];
		eg_score_black += egPieceValues[1] + knightTable[sq] + knightAdj[black_pawns];

		//First rank, encourage development
		if (sq >= 56 && sq <= 63)
			mg_score_black -= 25;

		//Mobility
		score -= (3 * (pieceMobility(PieceType::KNIGHT, board, sq) - 4));

		int atkSq = (attacks::knight(thisSq) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 81;
			attack_squares_black += atkSq;
		}

		int defSq = (attacks::knight(thisSq) & black_ring).count();
		if (defSq) {
			defender_black_knight += defSq;
		}


		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 2)
			mg_score_black += 4;

	}

	//bishops
	while (white_bishop_BB.getBits()) {
		int sq = white_bishop_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[2] + bishopTableWhite[sq];
		eg_score_white += egPieceValues[2] + bishopTableWhite[sq];

		//First rank, encourage development
		if (sq >= 0 && sq <= 7)
			mg_score_white -= 25;

		//Mobility
		score += (2 * (pieceMobility(PieceType::BISHOP, board, sq) - 7));

		int atkSq = (attacks::bishop(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 52;
			attack_squares_white += atkSq;
		}

	}

	while (black_bishop_BB.getBits()) {
		int sq = black_bishop_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[2] + bishopTableBlack[sq];
		eg_score_black += egPieceValues[2] + bishopTableBlack[sq];

		//First rank, encourage development
		if (sq >= 56 && sq <= 63)
			mg_score_black -= 25;

		//Mobility
		score -= (2 * (pieceMobility(PieceType::BISHOP, board, sq) - 7));

		int atkSq = (attacks::bishop(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 52;
			attack_squares_black += atkSq;
		}

	}

	//rooks
	while (white_rook_BB.getBits()) {
		int sq = white_rook_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[3] + rookTableWhite[sq] + rookAdj[white_pawns];
		eg_score_white += egPieceValues[3] + rookTableWhite[sq] + rookAdj[white_pawns];

		//Mobility
		score += (2 * (pieceMobility(PieceType::ROOK, board, sq) - 7));


		int atkSq = (attacks::rook(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 44;
			attack_squares_white += atkSq;
		}

	}

	while (black_rook_BB.getBits()) {
		int sq = black_rook_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[3] + rookTableBlack[sq] + rookAdj[black_pawns];
		eg_score_black += egPieceValues[3] + rookTableBlack[sq] + rookAdj[black_pawns];

		//Mobility
		score -= (2 * (pieceMobility(PieceType::ROOK, board, sq) - 7));

		int atkSq = (attacks::rook(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 44;
			attack_squares_black += atkSq;
		}

	}

	//queens
	while (white_queen_BB.getBits()) {
		int sq = white_queen_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[4] + queenTable[sq];
		eg_score_white += egPieceValues[4] + queenTable[sq];

		//Mobility
		score += (pieceMobility(PieceType::QUEEN, board, sq) - 14);

		int atkSq = (attacks::queen(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 30;
			attack_squares_white += atkSq;
		}

	}

	while (black_queen_BB.getBits()) {
		int sq = black_queen_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[4] + queenTable[sq];
		eg_score_black += egPieceValues[4] + queenTable[sq];

		//Mobility
		score -= (pieceMobility(PieceType::QUEEN, board, sq) - 14);

		int atkSq = (attacks::queen(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 30;
			attack_squares_black += atkSq;
		}

	}

	//Kings
	mg_score_white += mgKingTableWhite[whiteKing];
	eg_score_white += egKingTableWhite[whiteKing];

	if (whiteKing > 7)
		mg_score_white -= 27;

	mg_score_black += mgKingTableBlack[blackKing];
	eg_score_black += egKingTableBlack[blackKing];

	if (blackKing < 56)
		mg_score_black -= 27;

	
	/* Weighted phase scores */
	score -= (int)((white_mg * mg_score_black) + ((1 - white_mg) * eg_score_black));
	score += (int)((black_mg * mg_score_white) + ((1 - black_mg) * eg_score_white));

	/* Piece pair adjustments */
	if (white_bishop_BB.count() == 2)
		score += (int)(10 * phase + 17 * (1 - phase));

	if (black_bishop_BB.count() == 2)
		score -= (int)(10 * phase + 17 * (1 - phase));

	if (white_knight_BB.count() == 2)
		score -= (int)(10 * phase + 17 * (1 - phase));

	if (black_knight_BB.count() == 2)
		score += (int)(10 * phase + 17 * (1 - phase));

	if (white_rook_BB.count() == 2)
		score -= (int)(12 * phase);

	if (black_rook_BB.count() == 2)
		score += (int)(12 * phase);


	/* Attack and defense scores */
	int attack_score_white = attack_white_count > 1 ? (attack_white_count * attack_white_weight) +
		(69 * attack_squares_white) -
		(black_shield * 50) -
		(defender_black_knight * 45) : 0;


	int attack_score_black = attack_black_count > 1 ? (attack_black_count * attack_black_weight) +
		(69 * attack_squares_black) -
		(white_shield * 50) -
		(defender_white_knight * 45) : 0;

	if (attack_score_white > 0)
		score += attack_score_white * phase;

	if (attack_score_black > 0)
		score -= attack_score_black * phase;


	/* MATE SQUARE */
	if (white_mg == 0 && white_rook_BB.count() == 0 && white_queen_BB.count() == 0 && (black_rook_BB.count() > 0 || black_queen_BB.count() > 0)) {

		score += mateTableKing[whiteKing];

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score += (int)distance;
	}

	if (black_mg == 0 && black_rook_BB.count() == 0 && black_queen_BB == 0 && (white_rook_BB.count() > 0 || white_queen_BB.count() > 0)) {

		score -= mateTableKing[blackKing];

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score -= (int)distance;
	}

	/* 50 MOVE DRAW CONSIDERATIONS */
	if (white_mg < 0.10f || black_mg < 0.10f) {
		//50 move draw reduction
		int movesBeforeDraw = 100 - board->halfMoveClock();
		score = (movesBeforeDraw * score) / 100;
	}


	/* NEGA-MAX adjustment for black */
	if (board->sideToMove() == Color::BLACK)
		score *= -1;

	return score;

}

Bitboard Eval::kingRing(int king_sq) {
	return attacks::king(king_sq);
}

int Eval::PsqM(Board* board, Move m) {

	Piece p = board->at(m.from());

	if (p == Piece::WHITEPAWN) {

		Bitboard black_knight_BB = board->pieces(PieceType::KNIGHT, Color::BLACK);
		Bitboard black_bishop_BB = board->pieces(PieceType::BISHOP, Color::BLACK);
		Bitboard black_rook_BB = board->pieces(PieceType::ROOK, Color::BLACK);
		Bitboard black_queen_BB = board->pieces(PieceType::QUEEN, Color::BLACK);

		float black_mg = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);
		return (int)(black_mg * (mgPawnTableWhite[m.to().index()] - mgPawnTableWhite[m.from().index()]) - (black_mg - 1) * (egPawnTableWhite[m.to().index()] - egPawnTableWhite[m.from().index()]));
	}
	if (p == Piece::WHITEKNIGHT) {
		return knightTable[m.to().index()] - knightTable[m.from().index()];
	}
	if (p == Piece::WHITEBISHOP) {
		return bishopTableWhite[m.to().index()] - bishopTableWhite[m.from().index()];
	}
	if (p == Piece::WHITEROOK) {
		return rookTableWhite[m.to().index()] - rookTableWhite[m.from().index()];
	}
	if (p == Piece::WHITEQUEEN) {
		return queenTable[m.to().index()] - queenTable[m.from().index()];
	}
	if (p == Piece::WHITEKING) {
		Bitboard black_knight_BB = board->pieces(PieceType::KNIGHT, Color::BLACK);
		Bitboard black_bishop_BB = board->pieces(PieceType::BISHOP, Color::BLACK);
		Bitboard black_rook_BB = board->pieces(PieceType::ROOK, Color::BLACK);
		Bitboard black_queen_BB = board->pieces(PieceType::QUEEN, Color::BLACK);

		float black_mg = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);
		return (int)(black_mg * (mgKingTableWhite[m.to().index()] - mgPawnTableWhite[m.from().index()]) - (black_mg - 1) * (egKingTableWhite[m.to().index()] - egKingTableWhite[m.from().index()]));
	}

	if (p == Piece::BLACKPAWN) {

		Bitboard white_knight_BB = board->pieces(PieceType::KNIGHT, Color::WHITE);
		Bitboard white_bishop_BB = board->pieces(PieceType::BISHOP, Color::WHITE);
		Bitboard white_rook_BB = board->pieces(PieceType::ROOK, Color::WHITE);
		Bitboard white_queen_BB = board->pieces(PieceType::QUEEN, Color::WHITE);

		float white_mg = whitePhase(white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB);
		return (int)(white_mg * (mgPawnTableBlack[m.to().index()] - mgPawnTableBlack[m.from().index()]) - (white_mg - 1) * (egPawnTableBlack[m.to().index()] - egPawnTableBlack[m.from().index()]));
	}
	if (p == Piece::BLACKKNIGHT) {
		return knightTable[m.to().index()] - knightTable[m.from().index()];
	}
	if (p == Piece::BLACKBISHOP) {
		return bishopTableBlack[m.to().index()] - bishopTableBlack[m.from().index()];
	}
	if (p == Piece::BLACKROOK) {
		return rookTableBlack[m.to().index()] - rookTableBlack[m.from().index()];
	}
	if (p == Piece::BLACKQUEEN) {
		return queenTable[m.to().index()] - queenTable[m.from().index()];
	}
	if (p == Piece::BLACKKING) {

		Bitboard white_knight_BB = board->pieces(PieceType::KNIGHT, Color::WHITE);
		Bitboard white_bishop_BB = board->pieces(PieceType::BISHOP, Color::WHITE);
		Bitboard white_rook_BB = board->pieces(PieceType::ROOK, Color::WHITE);
		Bitboard white_queen_BB = board->pieces(PieceType::QUEEN, Color::WHITE);

		float white_mg = whitePhase(white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB);
		return (int)(white_mg * (mgKingTableBlack[m.to().index()] - mgKingTableBlack[m.from().index()]) - (white_mg - 1) * (egKingTableBlack[m.to().index()] - egKingTableBlack[m.from().index()]));
	}

	return 0;
}

bool Eval::onlyPawns(Board* board) {

	Bitboard occ_pawn_king = board->pieces(PieceType::PAWN, Color::WHITE) | board->pieces(PieceType::PAWN, Color::BLACK) | board->pieces(PieceType::KING, Color::WHITE) | board->pieces(PieceType::KING, Color::BLACK);
	Bitboard occ_all = board->us(Color::WHITE) | board->us(Color::BLACK);

	return !(occ_all ^ occ_pawn_king);

}