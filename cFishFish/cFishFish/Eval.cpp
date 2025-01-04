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

float Eval::whitePhase(Board* board) {
	int whiteMaterial =
		(board->pieces(PieceType::KNIGHT, Color::WHITE).count() * 3) +
		(board->pieces(PieceType::BISHOP, Color::WHITE).count() * 3) +
		(board->pieces(PieceType::ROOK, Color::WHITE).count() * 5) +
		(board->pieces(PieceType::QUEEN, Color::WHITE).count() * 9);

	return std::max(0.0f, (((float)whiteMaterial - 10) / 21));
}

float Eval::blackPhase(Board* board) {
	int blackMaterial =
		(board->pieces(PieceType::KNIGHT, Color::BLACK).count() * 3) +
		(board->pieces(PieceType::BISHOP, Color::BLACK).count() * 3) +
		(board->pieces(PieceType::ROOK, Color::BLACK).count() * 5) +
		(board->pieces(PieceType::QUEEN, Color::BLACK).count() * 9);

	return std::max(0.0f, (((float)blackMaterial - 10) / 21));
}

int Eval::winnable(Board* board) {
	if (board->pieces(PieceType::PAWN, Color::WHITE).getBits() != 0 || board->pieces(PieceType::PAWN, Color::BLACK).getBits() != 0)
		return 1;

	if (board->pieces(PieceType::QUEEN, Color::WHITE).getBits() != 0 || board->pieces(PieceType::QUEEN, Color::BLACK).getBits() != 0)
		return 1;

	if (board->pieces(PieceType::ROOK, Color::WHITE).getBits() != 0 || board->pieces(PieceType::ROOK, Color::BLACK).getBits() != 0)
		return 1;

	Bitboard bishopBBW = board->pieces(PieceType::BISHOP, Color::WHITE);
	Bitboard bishopBBB = board->pieces(PieceType::BISHOP, Color::BLACK);

	if (bishopBBW.count() >= 2 || bishopBBB.count() >= 2)
		return 1;

	Bitboard knightBBW = board->pieces(PieceType::KNIGHT, Color::WHITE);
	Bitboard knightBBB = board->pieces(PieceType::KNIGHT, Color::BLACK);

	if ((bishopBBW != 0 && knightBBW != 0) || (bishopBBB != 0 && knightBBB != 0))
		return 1;

	if (knightBBW.count() >= 3 || knightBBB.count() >= 3)
		return 1;

	return 0;
}

int Eval::isKingSquare(Board* board, int sq) {
	int distWhite = std::abs(board->kingSq(Color::WHITE).index() - sq);

	if (distWhite == 1 || distWhite == 7 || distWhite == 8 || distWhite == 9)
		return 1;

	int distBlack = std::abs(board->kingSq(Color::BLACK).index() - sq);

	if (distBlack == 1 || distBlack == 7 || distBlack == 8 || distBlack == 9)
		return -1;

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

bool Eval::attackedBy(Board* b, Square sq, PieceType pt, Color side) {
	Bitboard occ_other = b->us(~side);

	if (pt == PieceType::KNIGHT) {
		if (attacks::knight(sq) & b->pieces(PieceType::KNIGHT, side)) return true;

		return false;
	}

	if (pt == PieceType::BISHOP) {
		if (attacks::bishop(sq, occ_other) & (b->pieces(PieceType::BISHOP, side))) return true;

		return false;
	}

	if (pt == PieceType::ROOK) {
		if (attacks::rook(sq, occ_other) & (b->pieces(PieceType::ROOK, side))) return true;

		return false;
	}

	if (pt == PieceType::QUEEN) {
		if (attacks::queen(sq, occ_other) & (b->pieces(PieceType::QUEEN, side))) return true;

		return false;
	}

	return false;

}

int Eval::evaluate(Board* board) {

	//Game phase
	float white_mg = whitePhase(board);
	float black_mg = blackPhase(board);

	float phase = (white_mg + black_mg) / 2;

	//Returns draws by insufficient material
	if (phase == 0 && winnable(board) == 0)
		return 0;

	//Number of pawns each side has
	int white_pawns = board->pieces(PieceType::PAWN, Color::WHITE).count();
	int black_pawns = board->pieces(PieceType::PAWN, Color::BLACK).count();

	//imblance
	int score = 0;

	//Middle game and endgame score
	int mg_score_white = 0;
	int mg_score_black = 0;

	int eg_score_white = 0;
	int eg_score_black = 0;

	for (int sq = 0; sq < 64; sq++) {
		Piece p = board->at(Square(sq));

		int kingSquare = isKingSquare(board, sq);

		//Square touches white king
		if (kingSquare == 1) {

			//Knight attacks
			if (attackedBy(board, Square(sq), PieceType::KNIGHT, Color::BLACK))
				mg_score_black += 81;

			//Bishop attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::BISHOP, Color::BLACK))
				mg_score_black += 52;

			//Queen attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::QUEEN, Color::BLACK))
				mg_score_black += 30;

			//Rook attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::ROOK, Color::BLACK))
				mg_score_black += 44;

			//Knight Defender
			if (attackedBy(board, Square(sq), PieceType::KNIGHT, Color::WHITE))
				mg_score_white += 45;

		}

		//Square touches black king
		if (kingSquare == -1) {

			//Knight attacks
			if (attackedBy(board, Square(sq), PieceType::KNIGHT, Color::WHITE))
				mg_score_white += 81;

			//Bishop attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::BISHOP, Color::WHITE))
				mg_score_white += 52;

			//Queen attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::QUEEN, Color::WHITE))
				mg_score_white += 30;

			//Rook attacks (x-ray)
			if (attackedBy(board, Square(sq), PieceType::ROOK, Color::WHITE))
				mg_score_white += 44;

			//Knight Defender
			if (attackedBy(board, Square(sq), PieceType::KNIGHT, Color::BLACK))
				mg_score_black += 45;

		}

		if (p == Piece::NONE) {
			continue;
		}

		if (p == Piece::WHITEPAWN) {

			mg_score_white += mgPieceValues[0] + mgPawnTableWhite[sq];
			eg_score_white += egPieceValues[0] + egPawnTableWhite[sq];

			//Pawn shield
			if (kingSquare == 1)
				mg_score_white += 37;

			//Space
			if ((sq + 8 <= 63) && board->at(Square(sq + 8)) != Piece::WHITEPAWN) {
				score += 2;
				if ((sq + 16 <= 63) && board->at(Square(sq + 16)) != Piece::WHITEPAWN) {
					score += 2;
				}
			}

			continue;
		}

		if (p == Piece::WHITEKNIGHT) {

			mg_score_white += mgPieceValues[1] + knightTable[sq] + knightAdj[white_pawns];
			eg_score_white += egPieceValues[1] + knightTable[sq] + knightAdj[white_pawns];

			//First rank, encourage development
			if (sq >= 0 && sq <= 7)
				mg_score_white -= 25;

			//Mobility
			score += (3 * (pieceMobility(PieceType::KNIGHT, board, sq) - 4));

			continue;

		}

		if (p == Piece::WHITEBISHOP) {

			mg_score_white += mgPieceValues[2] + bishopTableWhite[sq];
			eg_score_white += egPieceValues[2] + bishopTableWhite[sq];

			//First rank, encourage development
			if (sq >= 0 && sq <= 7)
				mg_score_white -= 25;

			//Mobility
			score += (2 * (pieceMobility(PieceType::BISHOP, board, sq) - 7));

			continue;

		}

		if (p == Piece::WHITEROOK) {
			mg_score_white += mgPieceValues[3] + rookTableWhite[sq] + rookAdj[white_pawns];
			eg_score_white += egPieceValues[3] + rookTableWhite[sq] + rookAdj[white_pawns];

			//Mobility
			score += (2 * (pieceMobility(PieceType::ROOK, board, sq) - 7));

			continue;

		}

		if (p == Piece::WHITEQUEEN) {

			mg_score_white += mgPieceValues[4] + queenTable[sq];
			eg_score_white += egPieceValues[4] + queenTable[sq];

			//Mobility
			score += (pieceMobility(PieceType::QUEEN, board, sq) - 14);

			continue;

		}

		if (p == Piece::WHITEKING) {

			mg_score_white += mgKingTableWhite[sq];
			eg_score_white += egKingTableWhite[sq];

			if (sq > 7)
				mg_score_white -= 27;

			continue;

		}

		if (p == Piece::BLACKPAWN) {

			mg_score_black += mgPieceValues[0] + mgPawnTableBlack[sq];
			eg_score_black += egPieceValues[0] + egPawnTableBlack[sq];

			//Pawn shield
			if (kingSquare == -1)
				mg_score_black += 37;

			//Space
			if ((sq - 8 > 0) && board->at(Square(sq - 8)) != Piece::BLACKPAWN) {
				score -= 2;
				if ((sq - 16 > 0) && board->at(Square(sq - 16)) != Piece::BLACKPAWN) {
					score -= 2;
				}
			}

			continue;
		}

		if (p == Piece::BLACKKNIGHT) {

			mg_score_black += mgPieceValues[1] + knightTable[sq] + knightAdj[black_pawns];
			eg_score_black += egPieceValues[1] + knightTable[sq] + knightAdj[black_pawns];

			//First rank, encourage development
			if (sq >= 56 && sq <= 63)
				mg_score_black -= 25;

			//Mobility
			score -= (3 * (pieceMobility(PieceType::KNIGHT, board, sq) - 4));

			continue;

		}

		if (p == Piece::BLACKBISHOP) {

			mg_score_black += mgPieceValues[2] + bishopTableBlack[sq];
			eg_score_black += egPieceValues[2] + bishopTableBlack[sq];

			//First rank, encourage development
			if (sq >= 56 && sq <= 63)
				mg_score_black -= 25;

			//Mobility
			score -= (2 * (pieceMobility(PieceType::BISHOP, board, sq) - 7));

			continue;

		}

		if (p == Piece::BLACKROOK) {
			mg_score_black += mgPieceValues[3] + rookTableBlack[sq] + rookAdj[black_pawns];
			eg_score_black += egPieceValues[3] + rookTableBlack[sq] + rookAdj[black_pawns];

			//Mobility
			score -= (2 * (pieceMobility(PieceType::ROOK, board, sq) - 7));

			continue;

		}

		if (p == Piece::BLACKQUEEN) {

			mg_score_black += mgPieceValues[4] + queenTable[sq];
			eg_score_black += egPieceValues[4] + queenTable[sq];

			//Mobility
			score -= (pieceMobility(PieceType::QUEEN, board, sq) - 14);

			continue;

		}

		if (p == Piece::BLACKKING) {

			mg_score_black += mgKingTableBlack[sq];
			eg_score_black += egKingTableBlack[sq];

			if (sq < 56)
				mg_score_black -= 27;

			continue;

		}

	}

	//mg and eg eval weighted
	score -= (int)((white_mg * mg_score_black) + ((1 - white_mg) * eg_score_black));
	score += (int)((black_mg * mg_score_white) + ((1 - black_mg) * eg_score_white));

	//Bishop pairs
	if (board->pieces(PieceType::BISHOP, Color::WHITE).count() == 2)
		score += (int)(10 * phase + 17 * (1 - phase));

	if (board->pieces(PieceType::BISHOP, Color::BLACK).count() == 2)
		score -= (int)(10 * phase + 17 * (1 - phase));

	//Knight pair
	if (board->pieces(PieceType::KNIGHT, Color::WHITE).count() == 2)
		score -= (int)(10 * phase + 17 * (1 - phase));

	if (board->pieces(PieceType::KNIGHT, Color::BLACK).count() == 2)
		score += (int)(10 * phase + 17 * (1 - phase));

	//rook pair
	if (board->pieces(PieceType::ROOK, Color::WHITE).count() == 2)
		score -= (int)(12 * phase);

	if (board->pieces(PieceType::ROOK, Color::BLACK).count() == 2)
		score += (int)(12 * phase);


	if (white_mg == 0 && board->pieces(PieceType::ROOK, Color::WHITE).count() == 0 && board->pieces(PieceType::QUEEN, Color::WHITE).count() == 0 && (board->pieces(PieceType::ROOK, Color::BLACK).count() > 0 || board->pieces(PieceType::QUEEN, Color::BLACK).count() > 0)) {
		int whiteKing = board->kingSq(Color::WHITE).index();
		int blackKing = board->kingSq(Color::BLACK).index();

		score += mateTableKing[whiteKing];

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score += (int)distance;
	}

	if (black_mg == 0 && board->pieces(PieceType::ROOK, Color::BLACK).count() == 0 && board->pieces(PieceType::QUEEN, Color::BLACK).count() == 0 && (board->pieces(PieceType::ROOK, Color::WHITE).count() > 0 || board->pieces(PieceType::QUEEN, Color::WHITE).count() > 0)) {
		int whiteKing = board->kingSq(Color::WHITE).index();
		int blackKing = board->kingSq(Color::BLACK).index();

		score -= mateTableKing[blackKing];

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score -= (int)distance;
	}

	if (white_mg < 0.10f || black_mg < 0.10f) {
		//50 move draw reduction
		int movesBeforeDraw = 100 - board->halfMoveClock();
		score = (movesBeforeDraw * score) / 100;
	}

	if (board->sideToMove() == Color::BLACK)
		score *= -1;

	return score;

}

int Eval::PsqM(Board* board, Move m) {

	Piece p = board->at(m.from());

	if (p == Piece::WHITEPAWN) {
		float black_mg = blackPhase(board);
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
		float black_mg = blackPhase(board);
		return (int)(black_mg * (mgKingTableWhite[m.to().index()] - mgPawnTableWhite[m.from().index()]) - (black_mg - 1) * (egKingTableWhite[m.to().index()] - egKingTableWhite[m.from().index()]));
	}

	if (p == Piece::BLACKPAWN) {
		float white_mg = whitePhase(board);
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
		float white_mg = whitePhase(board);
		return (int)(white_mg * (mgKingTableBlack[m.to().index()] - mgKingTableBlack[m.from().index()]) - (white_mg - 1) * (egKingTableBlack[m.to().index()] - egKingTableBlack[m.from().index()]));
	}

	return 0;
}

bool Eval::onlyPawns(Board* board) {
	return
		(board->pieces(PieceType::KNIGHT, Color::WHITE).count()) +
		(board->pieces(PieceType::BISHOP, Color::WHITE).count()) +
		(board->pieces(PieceType::ROOK, Color::WHITE).count()) +
		(board->pieces(PieceType::QUEEN, Color::WHITE).count()) +
		(board->pieces(PieceType::KNIGHT, Color::BLACK).count()) +
		(board->pieces(PieceType::BISHOP, Color::BLACK).count()) +
		(board->pieces(PieceType::ROOK, Color::BLACK).count()) +
		(board->pieces(PieceType::QUEEN, Color::BLACK).count())
		== 0;
}