#include "Eval.h"

//Piece values by phase
static int mgPieceValues[5] = { 82, 337, 365, 477, 1025 };
static int egPieceValues[5] = { 101, 300, 319, 550, 1005 };

//Adjustment values by pawn count for knights and bishops
static const int knightAdj[9] = { -20, -16, -12, -8, -4,  0,  4,  8, 12 };
static const int rookAdj[9] = { 15,  12,   9,  6,  3,  0, -3, -6, -9 };

//COLOR PHASE PIECE TYPE SQUARE
static const int PSQT[2][2][6][64] = {

	//WHITE TABLES
	{
		//MG TABLES
		{
			//PAWN
			{
				 0, 0, 0, 0, 0, 0, 0, 0,
				-35, -1, -20, -23, -15, 24, 38, -22,
				-26, -4, -4, -10, 3, 3, 33, -12,
				-27, -2, -5, 12, 17, 6, 10, -25,
				-14, 13, 6, 21, 23, 12, 17, -23,
				-6, 7, 26, 31, 65, 56, 25, -20,
				98, 134, 61, 95, 68, 126, 34, -11,
				0, 0, 0, 0, 0, 0, 0, 0,
			},

			//KNIGHT
			{
				-50, -40, -30, -30, -30, -30, -40, -50,
				-40, -20, 0, 0, 0, 0, -20, -40,
				-30, 0, 10, 15, 15, 10, 0, -30,
				-30, 5, 15, 20, 20, 15, 5, -30,
				-30, 5, 15, 20, 20, 15, 5, -30,
				-30, 0, 10, 15, 15, 10, 0, -30,
				-40, -20, 0, 0, 0, 0, -20, -40,
				-50, -40, -30, -30, -30, -30, -40, -50
			},
	//BISHOP
	{
		-33, -3, -14, -21, -13, -12, -39, -21,
		4, 15, 16, 0, 7, 21, 33, 1,
		0, 15, 15, 4, 3, 27, 18, 10,
		-6, 13, 13, 26, 34, 12, 10, 4,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-29, 4, -82, -37, -25, -42, 7, -8,
	},
	//ROOK
	{
		-19, -13, 1, 17, 16, 7, -37, -26,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-5, 19, 26, 36, 17, 45, 61, 16,
		27, 32, 58, 62, 80, 67, 26, 44,
		32, 42, 32, 51, 63, 9, 31, 43,
	},
	//QUEEN
	{
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
	},
	//KING
	{
		-15, -4, -19, -54, -40, -28, 44, 14,
		1, 7, -8, -64, -43, -16, 9, 8,
		-14, -14, -22, -46, -44, -30, -15, -27,
		-49, -1, -27, -39, -46, -44, -33, -51,
		-17, -20, -12, -27, -30, -25, -14, -36,
		-9, 24, 2, -16, -20, 6, 22, -22,
		29, -1, -20, -7, -8, -4, -38, -29,
		-65, 23, 16, -15, -56, -34, 2, 13
	},
},
//EG TABLES
{
	//PAWN
	{
		0, 0, 0, 0, 0, 0, 0, 0,
		13, 8, 8, 10, 13, 0, 2, -7,
		4, 7, -6, 1, 0, -5, -1, -8,
		13, 9, -3, -7, -7, -8, 3, -1,
		32, 24, 13, 5, -2, 4, 17, 17,
		94, 100, 85, 67, 56, 53, 82, 84,
		178, 173, 158, 134, 147, 132, 165, 187,
		0, 0, 0, 0, 0, 0, 0, 0,
	},

	//KNIGHT
	{
		-50, -40, -30, -30, -30, -30, -40, -50,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-50, -40, -30, -30, -30, -30, -40, -50
	},
	//BISHOP
	{
		-33, -3, -14, -21, -13, -12, -39, -21,
		4, 15, 16, 0, 7, 21, 33, 1,
		0, 15, 15, 4, 3, 27, 18, 10,
		-6, 13, 13, 26, 34, 12, 10, 4,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-29, 4, -82, -37, -25, -42, 7, -8,
	},
	//ROOK
	{
		-19, -13, 1, 17, 16, 7, -37, -26,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-5, 19, 26, 36, 17, 45, 61, 16,
		27, 32, 58, 62, 80, 67, 26, 44,
		32, 42, 32, 51, 63, 9, 31, 43,
	},
	//QUEEN
	{
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
	},
	//KING
	{
		-53, -34, -21, -11, -28, -14, -24, -43,
		-27, -11, 4, 13, 14, 4, -5, -17,
		-19, -3, 11, 21, 23, 16, 7, -9,
		-18, -4, 21, 24, 27, 23, 9, -11,
		-8, 22, 24, 27, 26, 33, 26, 3,
		10, 17, 23, 15, 20, 45, 44, 13,
		-12, 17, 14, 17, 17, 38, 23, 11,
		-74, -35, -18, -18, -11, 15, 4, -17
	},
}
},
//BLACK TABLES
{
	//MG TABLES
	{
		//PAWN
		{
			0, 0, 0, 0, 0, 0, 0, 0,
			98, 134, 61, 95, 68, 126, 34, -11,
			-6, 7, 26, 31, 65, 56, 25, -20,
			-14, 13, 6, 21, 23, 12, 17, -23,
			-27, -2, -5, 12, 17, 6, 10, -25,
			-26, -4, -4, -10, 3, 3, 33, -12,
			-35, -1, -20, -23, -15, 24, 38, -22,
			0, 0, 0, 0, 0, 0, 0, 0,
		},

		//KNIGHT
		{
			-50, -40, -30, -30, -30, -30, -40, -50,
			-40, -20, 0, 0, 0, 0, -20, -40,
			-30, 0, 10, 15, 15, 10, 0, -30,
			-30, 5, 15, 20, 20, 15, 5, -30,
			-30, 5, 15, 20, 20, 15, 5, -30,
			-30, 0, 10, 15, 15, 10, 0, -30,
			-40, -20, 0, 0, 0, 0, -20, -40,
			-50, -40, -30, -30, -30, -30, -40, -50
		},
	//BISHOP
	{
		-29, 4, -82, -37, -25, -42, 7, -8,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-6, 13, 13, 26, 34, 12, 10, 4,
		0, 15, 15, 15, 14, 27, 18, 10,
		4, 15, 16, 0, 7, 21, 33, 1,
		-33, -3, -14, -21, -13, -12, -39, -21,
	},
	//ROOK
	{
		32, 42, 32, 51, 63, 9, 31, 43,
		27, 32, 58, 62, 80, 67, 26, 44,
		-5, 19, 26, 36, 17, 45, 61, 16,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-19, -13, 1, 17, 16, 7, -37, -26,
	},
	//QUEEN
	{
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
	},
	//KING
	{
		-65, 23, 16, -15, -56, -34, 2, 13,
		29, -1, -20, -7, -8, -4, -38, -29,
		-9, 24, 2, -16, -20, 6, 22, -22,
		-17, -20, -12, -27, -30, -25, -14, -36,
		-49, -1, -27, -39, -46, -44, -33, -51,
		-14, -14, -22, -46, -44, -30, -15, -27,
		1, 7, -8, -64, -43, -16, 9, 8,
		-15, -4, -19, -54, -40, -28, 44, 14,
	},
},
//EG TABLES
{
	//PAWN
	{
		0, 0, 0, 0, 0, 0, 0, 0,
		178, 173, 158, 134, 147, 132, 165, 187,
		94, 100, 85, 67, 56, 53, 82, 84,
		32, 24, 13, 5, -2, 4, 17, 17,
		13, 9, -3, -7, -7, -8, 3, -1,
		4, 7, -6, 1, 0, -5, -1, -8,
		13, 8, 8, 10, 13, 0, 2, -7,
		0, 0, 0, 0, 0, 0, 0, 0,
	},

	//KNIGHT
	{
		-50, -40, -30, -30, -30, -30, -40, -50,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 5, 15, 20, 20, 15, 5, -30,
		-30, 0, 10, 15, 15, 10, 0, -30,
		-40, -20, 0, 0, 0, 0, -20, -40,
		-50, -40, -30, -30, -30, -30, -40, -50
	},
	//BISHOP
	{
		-29, 4, -82, -37, -25, -42, 7, -8,
		-26, 16, -18, -13, 30, 59, 18, -47,
		-16, 37, 43, 40, 35, 50, 37, -2,
		-4, 5, 19, 50, 37, 37, 7, -2,
		-6, 13, 13, 26, 34, 12, 10, 4,
		0, 15, 15, 15, 14, 27, 18, 10,
		4, 15, 16, 0, 7, 21, 33, 1,
		-33, -3, -14, -21, -13, -12, -39, -21,
	},
	//ROOK
	{
		32, 42, 32, 51, 63, 9, 31, 43,
		27, 32, 58, 62, 80, 67, 26, 44,
		-5, 19, 26, 36, 17, 45, 61, 16,
		-24, -11, 7, 26, 24, 35, -8, -20,
		-36, -26, -12, -1, 9, -7, 6, -23,
		-45, -25, -16, -17, 3, 0, -5, -33,
		-44, -16, -20, -9, -1, 11, -6, -71,
		-19, -13, 1, 17, 16, 7, -37, -26,
	},
	//QUEEN
	{
		-20, -10, -10, -5, -5, -10, -10, -20,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 8, 8, 5, 0, -10,
		-10, 0, 5, 5, 5, 5, 0, -10,
		-10, 0, 0, 0, 0, 0, 0, -10,
		-20, -10, -10, -5, -5, -10, -10, -20,
	},
	//KING
	{
		-74, -35, -18, -18, -11, 15, 4, -17,
		-12, 17, 14, 17, 17, 38, 23, 11,
		10, 17, 23, 15, 20, 45, 44, 13,
		-8, 22, 24, 27, 26, 33, 26, 3,
		-18, -4, 21, 24, 27, 23, 9, -11,
		-19, -3, 11, 21, 23, 16, 7, -9,
		-27, -11, 4, 13, 14, 4, -5, -17,
		-53, -34, -21, -11, -28, -14, -24, -43
	},
}
}

};

static const int isolated_penality[8] = { -12, -16, -16, -18, -19, -15, -14, -17 };
static const int passed_bonus[8] = { 0, 10, 17, 15, 31, 84, 137 };

//Piece square tables
static const int mateTableKing[64] = {
		-1000, -500, -500, -500, -500, -500, -500, -1000,
		-500, -500, -300, -300, -300, -300, -500, -500,
		-500, -300, -200, -200, -200, -200, -300, -500,
		-500, -300, -200, -100, -100, -200, -300, -500,
		-500, -300, -200, -100, -100, -200, -300, -500,
		-500, -300, -200, -200, -200, -200, -300, -500,
		-500, -500, -300, -300, -300, -300, -500, -500,
		-1000, -500, -500, -500, -500, -500, -500, -1000,
};

static const int CENTER_MANHATTAN_DISTANCE[64] = {
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
	int white_material =
		(white_knight_BB.count() * mgPieceValues[1]) +
		(white_bishop_BB.count() * mgPieceValues[2]) +
		(white_rook_BB.count() * mgPieceValues[3]) +
		(white_queen_BB.count() * mgPieceValues[4]);

	return std::max(0.0f, (((float)white_material - 954) / 3454));
}

float Eval::blackPhase(Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB) {
	int black_material =
		(black_knight_BB.count() * mgPieceValues[1]) +
		(black_bishop_BB.count() * mgPieceValues[2]) +
		(black_rook_BB.count() * mgPieceValues[3]) +
		(black_queen_BB.count() * mgPieceValues[4]);

	return std::max(0.0f, (((float)black_material - 954) / 3454));
}

bool Eval::winnable(Bitboard white_pawn_BB, Bitboard white_knight_BB, Bitboard white_bishop_BB, Bitboard white_rook_BB, Bitboard white_queen_BB, Bitboard black_pawn_BB, Bitboard black_knight_BB, Bitboard black_bishop_BB, Bitboard black_rook_BB, Bitboard black_queen_BB, Color c) {
	if (c == Color::NONE) {
		if (white_pawn_BB.count() != 0 || black_pawn_BB.count() != 0)
			return true;

		if (white_queen_BB.count() != 0 || black_queen_BB.count() != 0)
			return true;

		if (white_rook_BB.count() != 0 || black_rook_BB.count() != 0)
			return true;

		if (white_bishop_BB.count() >= 2 || black_bishop_BB.count() >= 2)
			return true;

		if ((white_bishop_BB != 0 && white_knight_BB != 0) || (black_bishop_BB != 0 && black_knight_BB != 0))
			return true;

		if (white_knight_BB.count() >= 3 || black_knight_BB.count() >= 3)
			return true;

		return false;
	}
	if (c == Color::WHITE) {
		if (white_pawn_BB.count() != 0)
			return true;

		if (white_queen_BB.count() != 0)
			return true;

		if (white_rook_BB.count() != 0)
			return true;

		if (white_bishop_BB.count() >= 2)
			return true;

		if ((white_bishop_BB != 0 && white_knight_BB != 0))
			return true;

		if (white_knight_BB.count() >= 3)
			return true;

		return false;
	}

	if (c == Color::BLACK) {
		if (black_pawn_BB.count() != 0)
			return true;

		if (black_queen_BB.count() != 0)
			return true;

		if (black_rook_BB.count() != 0)
			return true;

		if (black_bishop_BB.count() >= 2)
			return true;

		if ((black_bishop_BB != 0 && black_knight_BB != 0))
			return true;

		if (black_knight_BB.count() >= 3)
			return true;

		return false;
	}

	return false;
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

	//Simple material values
	int white_material =
		(white_pawn_BB.count()) +
		(white_knight_BB.count() * 3) +
		(white_bishop_BB.count() * 3) +
		(white_rook_BB.count() * 5) +
		(white_queen_BB.count() * 9);

	int black_material =
		(black_pawn_BB.count()) +
		(black_knight_BB.count() * 3) +
		(black_bishop_BB.count() * 3) +
		(black_rook_BB.count() * 5) +
		(black_queen_BB.count() * 9);

	//Number of pawns each side has
	int white_pawns = white_pawn_BB.count();
	int black_pawns = black_pawn_BB.count();

	//number of queens
	int white_queens = white_queen_BB.count();
	int black_queens = black_queen_BB.count();

	//number of rooks
	int white_rooks = white_rook_BB.count();
	int black_rooks = black_rook_BB.count();

	//Number of knights
	int white_knights = white_knight_BB.count();
	int black_knights = black_knight_BB.count();

	//Number of Bishops
	int white_bishops = white_bishop_BB.count();
	int black_bishops = black_bishop_BB.count();

	int whiteKing = white_king_BB.lsb();
	int blackKing = black_king_BB.lsb();

	//Game phase
	float white_mg_phase = whitePhase(white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB);
	float black_mg_phase = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);

	bool white_can_win = winnable(white_pawn_BB, white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB, black_pawn_BB, black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB, Color::WHITE);
	bool black_can_win = winnable(white_pawn_BB, white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB, black_pawn_BB, black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB, Color::BLACK);
	//Returns draws by insufficient material
	if (!white_can_win && !black_can_win)
		return 0;

	//score
	int score = 0;

	//Middle game and endgame score
	int mg_score_white = 0;
	int mg_score_black = 0;

	int eg_score_white = 0;
	int eg_score_black = 0;

	//Mobility scores (we only really need these values for king safety in the middle game)
	int mg_mobility_white = 0;
	int mg_mobility_black = 0;

	int eg_mobility_white = 0;
	int eg_mobility_black = 0;

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

	int weak_white = 0;
	int weak_black = 0;

	int bonus_white_weak = 0;
	int bonus_black_weak = 0;

	int bonus_attacker_white = 0;
	int bonus_attacker_black = 0;

	Bitboard white_ring = kingRing(whiteKing);
	Bitboard black_ring = kingRing(blackKing);

	//Defenders
	Bitboard large_white_ring = kingRing(whiteKing + 8) | kingRing(whiteKing - 8) | white_ring;
	Bitboard large_black_ring = kingRing(blackKing + 8) | kingRing(blackKing - 8) | black_ring;
	
	int white_defenders = 0;
	int black_defenders = 0;

	Bitboard white_bb = white_pawn_BB | white_knight_BB | white_bishop_BB | white_rook_BB | white_queen_BB;
	Bitboard black_bb = black_pawn_BB | black_knight_BB | black_bishop_BB | black_rook_BB | black_queen_BB;

	Bitboard white_vison_pawn;
	Bitboard white_vison_knight;
	Bitboard white_vison_bishop;
	Bitboard white_vison_rook;
	Bitboard white_vison_queen;
	Bitboard black_vison_pawn;
	Bitboard black_vison_knight;
	Bitboard black_vison_bishop;
	Bitboard black_vison_rook;
	Bitboard black_vison_queen;

	/* PIECE AND PIECE SQUARES */

	//Knights
	while (white_knight_BB.getBits()) {
		int sq = white_knight_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[1] + PSQT[C_WHITE][MG][KNIGHT][sq] + knightAdj[white_pawns];
		eg_score_white += egPieceValues[1] + PSQT[C_WHITE][EG][KNIGHT][sq] + knightAdj[white_pawns];

		//First rank, encourage development
		if (sq >= 0 && sq <= 7)
			mg_score_white -= 25;

		//Mobility

		Bitboard attacks = attacks::knight(thisSq);
		int m = attacks.count();

		mg_mobility_white += 4 * (m - 4);
		eg_mobility_white += 4 * (m - 4);

		int atkSq = (attacks & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 81;
			attack_squares_white += atkSq;
		}

		int defSq = (attacks & white_ring).count();
		if (defSq) {
			defender_white_knight += defSq;
		}

		white_vison_knight |= attacks;

		if (large_white_ring.check(sq)) {
			white_defenders++;
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 2)
			mg_score_white += 4;

	}

	while (black_knight_BB.getBits()) {
		int sq = black_knight_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[1] + PSQT[C_BLACK][MG][KNIGHT][sq] + knightAdj[black_pawns];
		eg_score_black += egPieceValues[1] + PSQT[C_BLACK][EG][KNIGHT][sq] + knightAdj[black_pawns];

		//First rank, encourage development
		if (sq >= 56 && sq <= 63)
			mg_score_black -= 25;

		//Mobility
		Bitboard attacks = attacks::knight(thisSq);
		int m = attacks.count();

		mg_mobility_black += 4 * (m - 4);
		eg_mobility_black += 4 * (m - 4);

		int atkSq = (attacks & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 81;
			attack_squares_black += atkSq;
		}

		int defSq = (attacks & black_ring).count();
		if (defSq) {
			defender_black_knight += defSq;
		}

		black_vison_knight |= attacks;

		if (large_black_ring.check(sq)) {
			black_defenders++;
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 2)
			mg_score_black += 4;

	}

	//bishops
	while (white_bishop_BB.getBits()) {
		int sq = white_bishop_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[2] + PSQT[C_WHITE][MG][BISHOP][sq];
		eg_score_white += egPieceValues[2] + PSQT[C_WHITE][EG][BISHOP][sq];

		//First rank, encourage development
		if (sq >= 0 && sq <= 7)
			mg_score_white -= 25;

		//Mobility
		Bitboard attacks = attacks::bishop(thisSq, board->occ());
		int m = attacks.count();

		mg_mobility_white += 3 * (m - 7);
		eg_mobility_white += 3 * (m - 7);


		//Bishop counts for pawn shield too
		if (white_ring.check(sq) && whiteKing < 8)
			white_shield++;

		if (white_ring.check(sq) && whiteKing >= 8)
			white_shield--;

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::bishop(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 52;
			attack_squares_white += atkSq;
		}

		if (large_white_ring.check(sq)) {
			white_defenders+=2;
		}

		white_vison_bishop |= attacks;

	}

	while (black_bishop_BB.getBits()) {
		int sq = black_bishop_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[2] + PSQT[C_BLACK][MG][BISHOP][sq];
		eg_score_black += egPieceValues[2] + PSQT[C_BLACK][EG][BISHOP][sq];

		//First rank, encourage development
		if (sq >= 56 && sq <= 63)
			mg_score_black -= 25;

		//Mobility
		Bitboard attacks = attacks::bishop(thisSq, board->occ());
		int m = attacks.count();

		mg_mobility_black += 3 * (m - 7);
		eg_mobility_black += 3 * (m - 7);

		//Bishop counts for pawn shield too
		if (black_ring.check(sq) && blackKing > 55)
			black_shield++;

		if (black_ring.check(sq) && blackKing <= 55)
			black_shield--;

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::bishop(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 52;
			attack_squares_black += atkSq;
		}

		if (large_black_ring.check(sq)) {
			black_defenders+=2;
		}

		black_vison_bishop |= attacks;

	}

	//rooks
	while (white_rook_BB.getBits()) {
		int sq = white_rook_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[3] + PSQT[C_WHITE][MG][ROOK][sq] + rookAdj[white_pawns];
		eg_score_white += egPieceValues[3] + PSQT[C_WHITE][EG][ROOK][sq] + rookAdj[white_pawns];

		Bitboard attacks = attacks::rook(thisSq, board->occ());

		//Only give mobility points when rook isn't trapped
		if (std::abs(File(whiteKing) - File(sq)) < 4 && Rank(whiteKing) == Rank(sq) && sq <= 7) {
			if (CENTER_MANHATTAN_DISTANCE[whiteKing] > CENTER_MANHATTAN_DISTANCE[sq]) {
				//Mobility
				int m = attacks.count();
				mg_mobility_white += 2 * (m - 7);
				eg_mobility_white += 4 * (m - 7);
			}
		}

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::rook(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 44;
			attack_squares_white += atkSq;
		}

		if (atkSq > 0 && (black_pawn_BB & Bitboard(File(sq))).count() == 0) {
			bonus_white_weak += atkSq;
			bonus_attacker_white++;
		}

		if (large_white_ring.check(sq)) {
			white_defenders++;
		}

		white_vison_rook |= attacks;

	}

	while (black_rook_BB.getBits()) {
		int sq = black_rook_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[3] + PSQT[C_BLACK][MG][ROOK][sq] + rookAdj[black_pawns];
		eg_score_black += egPieceValues[3] + PSQT[C_BLACK][EG][ROOK][sq] + rookAdj[black_pawns];

		Bitboard attacks = attacks::rook(thisSq, board->occ());

		//Only give mobility points when rook isn't trapped
		if (std::abs(File(blackKing) - File(sq)) < 4 && Rank(blackKing) == Rank(sq) && sq <= 7) {
			if (CENTER_MANHATTAN_DISTANCE[blackKing] > CENTER_MANHATTAN_DISTANCE[sq]) {
				//Mobility
				int m = attacks.count();
				mg_mobility_black += 2 * (m - 7);
				eg_mobility_black += 4 * (m - 7);


			}
		}

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::rook(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 44;
			attack_squares_black += atkSq;
		}

		if ((white_pawn_BB & Bitboard(File(sq))).count() == 0) {
			bonus_black_weak += atkSq;
			bonus_attacker_black++;
		}

		if (large_black_ring.check(sq)) {
			black_defenders++;
		}

		black_vison_rook |= attacks;

	}

	//queens
	while (white_queen_BB.getBits()) {
		int sq = white_queen_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[4] + PSQT[C_WHITE][MG][QUEEN][sq];
		eg_score_white += egPieceValues[4] + PSQT[C_WHITE][EG][QUEEN][sq];

		//Mobility
		Bitboard attacks = attacks::queen(thisSq, board->occ());
		int m = attacks.count();

		mg_mobility_white += m - 14;
		eg_mobility_white += 2 * (m - 14);

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::queen(thisSq, (black_bb | white_pawn_BB)) & black_ring).count();
		if (atkSq) {
			attack_white_count++;
			attack_white_weight += 10;
			attack_squares_white += atkSq;
		}

		if (large_white_ring.check(sq)) {
			white_defenders+=3;
		}

		white_vison_queen |= attacks;

	}

	while (black_queen_BB.getBits()) {
		int sq = black_queen_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[4] + PSQT[C_BLACK][MG][QUEEN][sq];
		eg_score_black += egPieceValues[4] + PSQT[C_BLACK][EG][QUEEN][sq];

		//Mobility
		Bitboard attacks = attacks::queen(thisSq, board->occ());
		int m = attacks.count();

		mg_mobility_black += m - 14;
		eg_mobility_black += 2 * (m - 14);

		//X-ray our pieces for batter/ discovery
		int atkSq = (attacks::queen(thisSq, (white_bb | black_pawn_BB)) & white_ring).count();
		if (atkSq) {
			attack_black_count++;
			attack_black_weight += 10;
			attack_squares_black += atkSq;
		}

		if (large_black_ring.check(sq)) {
			black_defenders+=3;
		}

		black_vison_queen |= attacks;

	}

	//Kings
	mg_score_white += PSQT[C_WHITE][MG][KING][whiteKing];
	eg_score_white += PSQT[C_WHITE][EG][KING][whiteKing];

	if (whiteKing > 7)
		mg_score_white -= 27;

	mg_score_black += PSQT[C_BLACK][MG][KING][blackKing];
	eg_score_black += PSQT[C_BLACK][EG][KING][blackKing];

	if (blackKing < 56)
		mg_score_black -= 27;

	//Pawns
	while (white_pawn_BB.getBits()) {
		int sq = white_pawn_BB.pop();
		Square thisSq = Square(sq);

		mg_score_white += mgPieceValues[0] + PSQT[C_WHITE][MG][PAWN][sq];
		eg_score_white += egPieceValues[0] + PSQT[C_WHITE][EG][PAWN][sq];

		//Pawn shield
		if (white_ring.check(sq) && whiteKing < 8)
			white_shield++;

		if (white_ring.check(sq) && whiteKing >= 8)
			white_shield--;

		//Space
		if (File(sq) != File::FILE_A && File(sq) != File::FILE_B && File(sq) != File::FILE_G && File(sq) != File::FILE_H) {
			if ((sq + 8 <= 63) && board->at(Square(sq + 8)) != Piece::WHITEPAWN) {
				score += 2;
				if ((sq + 16 <= 63) && board->at(Square(sq + 16)) != Piece::WHITEPAWN) {
					score += 2;
				}
			}
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 1)
			mg_score_white += 4;

		white_vison_pawn |= attacks::pawn(Color::WHITE, thisSq);

	}

	while (black_pawn_BB.getBits()) {
		int sq = black_pawn_BB.pop();
		Square thisSq = Square(sq);

		mg_score_black += mgPieceValues[0] + PSQT[C_BLACK][MG][PAWN][sq];
		eg_score_black += egPieceValues[0] + PSQT[C_BLACK][EG][PAWN][sq];

		//Pawn shield on first
		if (black_ring.check(sq) && blackKing > 55)
			black_shield++;

		if (black_ring.check(sq) && blackKing <= 55)
			black_shield--;

		//Space
		if (File(sq) != File::FILE_A && File(sq) != File::FILE_B && File(sq) != File::FILE_G && File(sq) != File::FILE_H) {
			if ((sq - 8 > 0) && board->at(Square(sq - 8)) != Piece::BLACKPAWN) {
				score -= 2;
				if ((sq - 16 > 0) && board->at(Square(sq - 16)) != Piece::BLACKPAWN) {
					score -= 2;
				}
			}
		}

		//Center control
		if (CENTER_MANHATTAN_DISTANCE[sq] <= 1)
			mg_score_black += 4;

		black_vison_pawn |= attacks::pawn(Color::BLACK, thisSq);

	}

	white_pawn_BB = board->pieces(PieceType::PAWN, Color::WHITE);
	black_pawn_BB = board->pieces(PieceType::PAWN, Color::BLACK);

	//Pawn Structure
	for (int i = 0; i < 8; i++) {

		Bitboard file_BB = Bitboard(File(i));
		Bitboard adj_left_file_BB = 0;
		Bitboard adj_right_file_BB = 0;

		if (i > 0) adj_left_file_BB = Bitboard(File(i - 1));
		if (i < 7) adj_right_file_BB = Bitboard(File(i + 1));
		Bitboard file_white = white_pawn_BB & file_BB;
		Bitboard file_black = black_pawn_BB & file_BB;

		//Isolated/ Connected bonus
		Bitboard adjacent_white = (white_pawn_BB & adj_left_file_BB | (white_pawn_BB & adj_right_file_BB));
		Bitboard adjacent_black = (black_pawn_BB & adj_left_file_BB | (black_pawn_BB & adj_right_file_BB));

		int rank_white = Square(file_white.lsb()).rank();
		int rank_black = Square(file_black.lsb()).rank();

		if (file_white.count() >= 1 && adjacent_white.count() == 0) {
			mg_score_white += isolated_penality[i];
			eg_score_white += isolated_penality[i] - 5;
		}
		//connected
		else if (file_white.count() >= 1) {
			//suported
			int s = (white_vison_pawn & file_white).count();
			//Opposed
			int op = (file_black).count() >= 1 ? 1 : 0;
			//phalanx
			int ph = (white_pawn_BB & Bitboard(rank_white)).count();

			mg_score_white += (2 + ph - op) + (9 * s);
			eg_score_white += (2 + ph - op) + (12 * s);
		}

		if (file_black.count() >= 1 && adjacent_black.count() == 0) {
			mg_score_black += isolated_penality[i];
			eg_score_black += isolated_penality[i] - 5;
		}
		//connected
		else if (file_black.count() >= 1) {
			//suported
			int s = (black_vison_pawn & file_black).count();
			//Opposed
			int op = (file_white).count() >= 1 ? 1 : 0;
			//phalanx
			int ph = (black_pawn_BB & Bitboard(rank_black)).count();

			mg_score_black += (2 + ph - op) + (9 * s);
			eg_score_black += (2 + ph - op) + (12 * s);
		}

		//Only unsupported double pawns get a penalty
		if (file_white.count() >= 2 && (white_vison_pawn & file_white).count() == 0) {
			mg_score_white -= 20;
			eg_score_white -= 25;
		}
		if (file_black.count() >= 2 && (black_vison_pawn & file_black).count() == 0) {
			mg_score_black -= 20;
			eg_score_black -= 25;
		}

		//passed pawns
		if ((adjacent_black | file_black).count() == 0 && file_white.count() >= 1) {
			int s = (white_vison_pawn & file_white).count();

			mg_score_white += passed_bonus[rank_white] + (s * 20);
			eg_score_white += passed_bonus[rank_white] + (s * 35);

		}

		if ((adjacent_white | file_white.count()) == 0 && file_black.count() >= 1) {
			int s = (black_vison_pawn & file_black).count();

			mg_score_black += passed_bonus[7 - rank_black] + (s * 20);
			eg_score_black += passed_bonus[7 - rank_black] + (s * 35);

		}

	}


	/* Mobility credit */
	mg_score_white += mg_mobility_white;
	mg_score_black += mg_mobility_black;

	eg_score_white += eg_mobility_white;
	eg_score_black += eg_mobility_black;

	/* Piece pair adjustments */
	if (white_bishops == 2) {
		mg_score_white += 10;
		eg_score_white += 17;
	}

	if (black_bishops == 2) {
		mg_score_black += 10;
		eg_score_black += 17;
	}

	if (white_knights == 2) {
		mg_score_white -= 10;
		eg_score_white -= 17;
	}

	if (black_knights == 2) {
		mg_score_black -= 10;
		eg_score_black -= 17;
	}

	if (white_rooks == 2)
		mg_score_white -= 12;

	if (black_rooks == 2)
		mg_score_black -= 12;

	Bitboard white_vison = white_vison_pawn | white_vison_knight | white_vison_bishop | white_vison_rook | white_vison_queen;
	Bitboard black_vison = black_vison_pawn | black_vison_knight | black_vison_bishop | black_vison_rook | black_vison_queen;

	weak_white = ((black_vison & white_ring) & ~white_vison).count();
	weak_black = ((white_vison & black_ring) & ~black_vison).count();

	//White king threats
	Bitboard knight_attack_white = attacks::knight(blackKing);
	Bitboard bisop_attack_white = attacks::bishop(blackKing, board->occ());
	Bitboard rook_attack_white = attacks::rook(blackKing, board->occ());

	Bitboard knight_attack_black = attacks::knight(whiteKing);
	Bitboard bisop_attack_black = attacks::bishop(whiteKing, board->occ());
	Bitboard rook_attack_black = attacks::rook(whiteKing, board->occ());

	int safe_knight_checks_white = ((knight_attack_white & white_vison_knight) & ~(black_vison | black_ring)).count();
	int safe_bishop_checks_white = ((bisop_attack_white & white_vison_bishop) & ~(black_vison | black_ring)).count();
	int safe_rook_checks_white = ((rook_attack_white & white_vison_rook) & ~(black_vison | black_ring)).count();
	int safe_queen_checks_white = (((bisop_attack_white | rook_attack_white) & white_vison_queen) & ~(black_vison | black_ring)).count();

	int safe_knight_checks_black = ((knight_attack_black & black_vison_knight) & ~(white_vison | white_ring)).count();
	int safe_bishop_checks_black = ((bisop_attack_black & black_vison_bishop) & ~(white_vison | white_ring)).count();
	int safe_rook_checks_black = ((rook_attack_black & black_vison_rook) & ~(white_vison | white_ring)).count();
	int safe_queen_checks_black = (((bisop_attack_black | rook_attack_black) & black_vison_queen) & ~(white_vison | white_ring)).count();

	int check_score_white = (safe_knight_checks_white * 60) + (safe_bishop_checks_white * 30) + (safe_rook_checks_white * 45) + (safe_queen_checks_white * 45);
	int check_score_black = (safe_knight_checks_black * 60) + (safe_bishop_checks_black * 30) + (safe_rook_checks_black * 45) + (safe_queen_checks_black * 45);

	/* Attack and defense scores */
	int attack_score_white = bonus_attacker_white + attack_white_count > 1 ?
		//More attackers + bigger attackers
		(attack_white_count * attack_white_weight) +
		//Squares we are attacking near king
		(69 * attack_squares_white) +
		//Safe checks on the king
		check_score_white +
		//Squares with only a king defender in the king ring
		(90 * (bonus_white_weak + weak_black)) -
		//Score of pawns covering the king
		(black_shield * 50) -
		//Pieces close to black king
		(black_defenders * 50) -
		//Using a knight defending king
		(defender_black_knight * 45) : 0;

	//Mate is less likely without a queen
	if (white_queens == 0)
		attack_score_white /= 2;

	int attack_score_black = bonus_attacker_black + attack_black_count > 1 ?
		//More attackers + bigger attackers
		(attack_black_count * attack_black_weight) +
		//Squares we are attacking near king
		(69 * attack_squares_black) +
		//Safe checks on the king
		check_score_black +
		//Squares with only a king defender in the king ring
		(90 * (bonus_black_weak + weak_white)) -
		//Score of pawns covering the king
		(white_shield * 50) -
		//Pieces close to white king
		(white_defenders * 50) -
		//Using a knight defending king
		(defender_white_knight * 45) : 0;

	//Mate is less likely without a queen
	if (black_queens == 0)
		attack_score_black /= 2;

	if (attack_score_white > 0) {
		mg_score_white += attack_score_white;
	}
	if (attack_score_black > 0) {
		mg_score_black += attack_score_black;
	}

	/* Weighted phase scores */
	score += (((mg_score_white * white_mg_phase) + (eg_score_white * (1 - white_mg_phase))) - ((mg_score_black * black_mg_phase) + (eg_score_black * (1 - black_mg_phase))));


	/* MATE SQUARE */
	if (white_bishops + white_knights < 2 && white_rooks == 0 && white_queens == 0 && (black_rooks > 0 || black_queens > 0 || black_bishops >= 2 || black_knights >= 3 || (black_bishops >= 1 && black_knights >= 1))) {

		float scale = 1.0f;

		if (white_bishops >= 1)
			scale /= 0.5f;

		if (white_knights >= 1)
			scale /= 0.5f;

		score += scale * mateTableKing[whiteKing];

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score += scale * (int)distance;
	}

	if (black_bishops + black_knights < 2 && black_rooks == 0 && black_queens == 0 && (white_rooks > 0 || white_queens > 0 || white_bishops >= 2 || white_knights >= 3 || (white_bishops >= 1 && white_knights >= 1))) {

		float scale = 1.0f;

		if (black_bishops >= 1)
			scale /= 0.5f;

		if (black_knights >= 1)
			scale /= 0.5f;

		score -= mateTableKing[blackKing] * scale;

		Square whiteSq = Square(whiteKing);
		Square blackSq = Square(blackKing);

		double distance = std::pow((whiteSq.rank() - blackSq.rank()), 2) + std::pow((whiteSq.file() - blackSq.file()), 2);

		score -= (int)distance * scale;
	}

	/* 50 MOVE DRAW REDUCTION */
	int movesBeforeDraw = 100 - board->halfMoveClock();
	score = (movesBeforeDraw * score) / 100;

	/* Drawish-positions */
	if (!white_can_win)
		score = std::min(0, score);

	if (!black_can_win)
		score = std::max(0, score);

	//Other likely draws
	if (black_mg_phase <= 0.1f || white_mg_phase <= 0.1f) {
		if (white_pawns == black_pawns && white_pawns < 3 && black_pawns < 3) {
			//Minor vs rook + pawns or rook vs pawns
			if (score > 0 && white_material - black_material <= 2)
				score *= 0.15f;
			if (score < 0 && black_material - white_material <= 2)
				score *= 0.15f;

			//Two rooks vs rook + minor or two minors vs minor
			if (score > 0 && white_material - black_material == 3)
				score *= 0.45;
			if (score < 0 && black_material - white_material == 3)
				score *= 0.45;
		}

		else {
			if (score > 0 && white_material - black_material <= 2)
				score *= 0.55f;
			if (score < 0 && black_material - white_material <= 2)
				score *= 0.55f;

			//Two rooks vs rook + minor or two minors vs minor
			if (score > 0 && white_material - black_material == 3)
				score *= 0.85;
			if (score < 0 && black_material - white_material == 3)
				score *= 0.85;
		}

	}

	/* NEGA-MAX adjustment for black */
	if (board->sideToMove() == Color::BLACK)
		score *= -1;

	return score;

}

Bitboard Eval::kingRing(int king_sq) {

	return attacks::king(king_sq);
}

bool Eval::isAttacked(Board* b, Square square, Color color) {
	// cheap checks first
	if (attacks::pawn(~color, square) & b->pieces(PieceType::PAWN, color)) return true;
	if (attacks::knight(square) & b->pieces(PieceType::KNIGHT, color)) return true;

	if (attacks::bishop(square, b->occ()) & (b->pieces(PieceType::BISHOP, color) | b->pieces(PieceType::QUEEN, color)))
		return true;

	if (attacks::rook(square, b->occ()) & (b->pieces(PieceType::ROOK, color) | b->pieces(PieceType::QUEEN, color)))
		return true;

	return false;
}

int Eval::PsqM(Board* board, Move m) {

	Piece p = board->at(m.from());

	if (p == Piece::WHITEPAWN) {

		Bitboard white_knight_BB = board->pieces(PieceType::KNIGHT, Color::WHITE);
		Bitboard white_bishop_BB = board->pieces(PieceType::BISHOP, Color::WHITE);
		Bitboard white_rook_BB = board->pieces(PieceType::ROOK, Color::WHITE);
		Bitboard white_queen_BB = board->pieces(PieceType::QUEEN, Color::WHITE);

		float phase = whitePhase(white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB);

		if (phase == 0)
			return 32 * (int)((phase * (PSQT[C_WHITE][MG][PAWN][m.to().index()] - PSQT[C_WHITE][MG][PAWN][m.from().index()]) - (1 - phase) * (PSQT[C_WHITE][EG][PAWN][m.to().index()] - PSQT[C_WHITE][EG][PAWN][m.from().index()])));
		return (int)((phase * (PSQT[C_WHITE][MG][PAWN][m.to().index()] - PSQT[C_WHITE][MG][PAWN][m.from().index()]) - (1 - phase) * (PSQT[C_WHITE][EG][PAWN][m.to().index()] - PSQT[C_WHITE][EG][PAWN][m.from().index()])));
	}
	if (p == Piece::WHITEKNIGHT) {
		return PSQT[C_WHITE][MG][KNIGHT][m.to().index()] - PSQT[C_WHITE][MG][KNIGHT][m.from().index()];
	}
	if (p == Piece::WHITEBISHOP) {
		return PSQT[C_WHITE][MG][BISHOP][m.to().index()] - PSQT[C_WHITE][MG][BISHOP][m.from().index()];
	}
	if (p == Piece::WHITEROOK) {
		return PSQT[C_WHITE][MG][ROOK][m.to().index()] - PSQT[C_WHITE][MG][ROOK][m.from().index()];
	}
	if (p == Piece::WHITEQUEEN) {
		return PSQT[C_WHITE][MG][QUEEN][m.to().index()] - PSQT[C_WHITE][MG][QUEEN][m.from().index()];
	}
	if (p == Piece::WHITEKING) {

		Bitboard white_knight_BB = board->pieces(PieceType::KNIGHT, Color::WHITE);
		Bitboard white_bishop_BB = board->pieces(PieceType::BISHOP, Color::WHITE);
		Bitboard white_rook_BB = board->pieces(PieceType::ROOK, Color::WHITE);
		Bitboard white_queen_BB = board->pieces(PieceType::QUEEN, Color::WHITE);

		float phase = whitePhase(white_knight_BB, white_bishop_BB, white_rook_BB, white_queen_BB);

		return (int)((phase * (PSQT[C_WHITE][MG][KING][m.to().index()] - PSQT[C_WHITE][MG][KING][m.from().index()]) - (1 - phase) * (PSQT[C_WHITE][EG][KING][m.to().index()] - PSQT[C_WHITE][EG][KING][m.from().index()])));
	}

	if (p == Piece::BLACKPAWN) {

		Bitboard black_knight_BB = board->pieces(PieceType::KNIGHT, Color::BLACK);
		Bitboard black_bishop_BB = board->pieces(PieceType::BISHOP, Color::BLACK);
		Bitboard black_rook_BB = board->pieces(PieceType::ROOK, Color::BLACK);
		Bitboard black_queen_BB = board->pieces(PieceType::QUEEN, Color::BLACK);

		float phase = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);
		if (phase == 0)
			return 32 * (int)((phase * (PSQT[C_BLACK][MG][PAWN][m.to().index()] - PSQT[C_BLACK][MG][PAWN][m.from().index()]) - (1 - phase) * (PSQT[C_BLACK][EG][PAWN][m.to().index()] - PSQT[C_BLACK][EG][PAWN][m.from().index()])));

		return (int)((phase * (PSQT[C_BLACK][MG][PAWN][m.to().index()] - PSQT[C_BLACK][MG][PAWN][m.from().index()]) - (1 - phase) * (PSQT[C_BLACK][EG][PAWN][m.to().index()] - PSQT[C_BLACK][EG][PAWN][m.from().index()])));
	}
	if (p == Piece::BLACKKNIGHT) {
		return PSQT[C_BLACK][MG][KNIGHT][m.to().index()] - PSQT[C_BLACK][MG][KNIGHT][m.from().index()];
	}
	if (p == Piece::BLACKBISHOP) {
		return PSQT[C_BLACK][MG][BISHOP][m.to().index()] - PSQT[C_BLACK][MG][BISHOP][m.from().index()];
	}
	if (p == Piece::BLACKROOK) {
		return PSQT[C_BLACK][MG][ROOK][m.to().index()] - PSQT[C_BLACK][MG][ROOK][m.from().index()];
	}
	if (p == Piece::BLACKQUEEN) {
		return PSQT[C_BLACK][MG][QUEEN][m.to().index()] - PSQT[C_BLACK][MG][QUEEN][m.from().index()];
	}
	if (p == Piece::BLACKKING) {


		Bitboard black_knight_BB = board->pieces(PieceType::KNIGHT, Color::BLACK);
		Bitboard black_bishop_BB = board->pieces(PieceType::BISHOP, Color::BLACK);
		Bitboard black_rook_BB = board->pieces(PieceType::ROOK, Color::BLACK);
		Bitboard black_queen_BB = board->pieces(PieceType::QUEEN, Color::BLACK);

		float phase = blackPhase(black_knight_BB, black_bishop_BB, black_rook_BB, black_queen_BB);

		return (int)((phase * (PSQT[C_BLACK][MG][KING][m.to().index()] - PSQT[C_BLACK][MG][KING][m.from().index()]) - (1 - phase) * (PSQT[C_BLACK][EG][KING][m.to().index()] - PSQT[C_BLACK][EG][KING][m.from().index()])));
	}

	return 0;
}

bool Eval::onlyPawns(Board* board) {

	Bitboard occ_pawn_king = board->pieces(PieceType::PAWN, Color::WHITE) | board->pieces(PieceType::PAWN, Color::BLACK) | board->pieces(PieceType::KING, Color::WHITE) | board->pieces(PieceType::KING, Color::BLACK);
	Bitboard occ_all = board->us(Color::WHITE) | board->us(Color::BLACK);

	return !(occ_all ^ occ_pawn_king);

}