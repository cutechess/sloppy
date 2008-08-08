/* Sloppy - eval.c
   Functions for the static evaluation of the board.
   At the moment most of the evaluation features are copied from
   Fruit 2.1 by Fabien Letouzey. The code had to be re-written though.

   Copyright (C) 2007 Ilari Pihlajisto (ilari.pihlajisto@mbnet.fi)

   Sloppy is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Sloppy is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


   TODO:
   -Consider only complete pins in eval:
	If a piece is pinned but can capture the pinner, it's not necessarily
	in a bad position.
   -Take pinned pieces in SEE into account. Moving the king can pin/unpin
    pieces, so this may be expensive.  */

#include <stdio.h>
#include <stdlib.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "magicmoves.h"
#include "movegen.h"
#include "eval.h"


/* Evaluation weights.  */
#define BACKWARD_PAWN_OP -8
#define BACKWARD_PAWN_EG -10
#define BACKWARD_OPEN_PAWN_OP -16
#define BACKWARD_OPEN_PAWN_EG -10
#define DOUBLED_PAWN_OP -10
#define DOUBLED_PAWN_EG -20
#define ISOLATED_PAWN_OP -10
#define ISOLATED_PAWN_EG -20
#define ISOLATED_OPEN_PAWN_OP -20
#define ISOLATED_OPEN_PAWN_EG -20
#define ROOK_CLOSED_OP -10
#define ROOK_CLOSED_EG -10
#define ROOK_SEMIOPEN_ADJACENT_OP 10
#define ROOK_SEMIOPEN_ADJACENT_EG 0
#define ROOK_SEMIOPEN_SAME_OP 20
#define ROOK_SEMIOPEN_SAME_EG 0
#define ROOK_OPEN_OP 10
#define ROOK_OPEN_EG 10
#define ROOK_OPEN_ADJACENT_OP 20
#define ROOK_OPEN_ADJACENT_EG 10
#define ROOK_OPEN_SAME_OP 30
#define ROOK_OPEN_SAME_EG 10
#define ROOK_ON_7TH_OP 20
#define ROOK_ON_7TH_EG 40
#define QUEEN_ON_7TH_OP 10
#define QUEEN_ON_7TH_EG 20
#define DOUBLE_BISHOPS_OP 50
#define DOUBLE_BISHOPS_EG 50
#define TRAPPED_BISHOP -50
#define BLOCKED_BISHOP -50
#define BLOCKED_ROOK -50

#define VAL_PAWN_EG 90

/* If this is defined, Sloppy will print a lot of evaluation details.  */
//#define DEBUG_EVAL

#define OPENING 0
#define ENDGAME 1

#define WHITE_SQUARES 0xaa55aa55aa55aa55
#define BLACK_SQUARES 0x55aa55aa55aa55aa

typedef struct _EvalData
{
	int op;
	int eg;
} EvalData;

/* If we're not using GNU C, elide __attribute__  */
#ifndef __GNUC__
#define  __attribute__(x)
//#   pragma pack(1)
#endif /* not __GNUC__ */
#define PHASH_SIZE 32768
typedef struct _PawnHash
{
	U64 passers;
	U64 key;
	int op;
	int eg;
} __attribute__ ((__packed__)) PawnHash;

static PawnHash *pawn_hash = NULL;

const int pc_val[] = { 0, VAL_PAWN, VAL_KNIGHT, VAL_BISHOP,
                       VAL_ROOK, VAL_QUEEN, VAL_KING, 0 };
const int phase_val[] = { 0, 0, 1, 1, 2, 4, 0 };

const int max_phase = 1 * 4 + 1 * 4 + 2 * 4 + 4 * 2;

/* Evaluation bitmasks.  */
static const U64 file_mask[8] = {
	0x0101010101010101, 0x0202020202020202,
	0x0404040404040404, 0x0808080808080808,
	0x1010101010101010, 0x2020202020202020,
	0x4040404040404040, 0x8080808080808080
};
static const U64 isolated_pawn[8] = {
	0x0202020202020202, 0x0505050505050505,
	0x0a0a0a0a0a0a0a0a, 0x1414141414141414,
	0x2828282828282828, 0x5050505050505050,
	0xa0a0a0a0a0a0a0a0, 0x4040404040404040
};

static const U64 eighth_rank[2] =
	{ 0x00000000000000FF, 0xFF00000000000000 };

static U64 fwd_mask[2][64];
static U64 pawn_shelter[2][64];
static U64 passer[2][64];
static U64 backw_pawn[2][64];

static U64 ka_mask[64];		/* king attack mask */

/* Flips a "white" square into a "black" square (eg. A1 becomes A8), or if
   color == WHITE, does nothing.  */
#define FLIP(sq, color) ((color) == WHITE ? (sq) : (flip[(sq)]))
static const int flip[64] =
{
	56,  57,  58,  59,  60,  61,  62,  63,
	48,  49,  50,  51,  52,  53,  54,  55,
	40,  41,  42,  43,  44,  45,  46,  47,
	32,  33,  34,  35,  36,  37,  38,  39,
	24,  25,  26,  27,  28,  29,  30,  31,
	16,  17,  18,  19,  20,  21,  22,  23,
	 8,   9,  10,  11,  12,  13,  14,  15,
	 0,   1,   2,   3,   4,   5,   6,   7
};


/* Piece/square tables.  */
static const int pcsq_pawn_op[64] =
{
	  0,   0,   0,   0,   0,   0,   0,   0,
	-15,  -5,   0,   5,   5,   0,  -5, -15,
	-15,  -5,   0,   5,   5,   0,  -5, -15,
	-15,  -5,   0,  15,  15,   0,  -5, -15,
	-15,  -5,   0,  25,  25,   0,  -5, -15,
	-15,  -5,   0,  15,  15,   0,  -5, -15,
	-15,  -5,   0,   5,   5,   0,  -5, -15,
	  0,   0,   0,   0,   0,   0,   0,   0
};
static const int pcsq_knight_op[64] =
{
	-135,  -25,  -15,  -10,  -10,  -15,  -25, -135,
	 -20,  -10,    0,    5,    5,    0,  -10,  -20,
	  -5,    5,   15,   20,   20,   15,    5,   -5,
	  -5,    5,   15,   20,   20,   15,    5,   -5,
	 -10,    0,   10,   15,   15,   10,    0,  -10,
	 -20,  -10,    0,    5,    5,    0,  -10,  -20,
	 -35,  -25,  -15,  -10,  -10,  -15,  -25,  -35,
	 -50,  -40,  -30,  -25,  -25,  -30,  -40,  -50
};
static const int pcsq_knight_eg[64] =
{
	-40, -30, -20, -15, -15, -20, -30, -40,
	-30, -20, -10,  -5,  -5, -10, -20, -30,
	-20, -10,   0,   5,   5,   0, -10, -20,
	-15,  -5,   5,  10,  10,   5,  -5, -15,
	-15,  -5,   5,  10,  10,   5,  -5, -15,
	-20, -10,   0,   5,   5,   0, -10, -20,
	-30, -20, -10,  -5,  -5, -10, -20, -30,
	-40, -30, -20, -15, -15, -20, -30, -40
};
static const int pcsq_bishop_op[64] =
{
	 -8,  -8,  -6,  -4,  -4,  -6,  -8,  -8,
	 -8,   0,  -2,   0,   0,  -2,   0,  -8,
	 -6,  -2,   4,   2,   2,   4,  -2,  -6,
	 -4,   0,   2,   8,   8,   2,   0,  -4,
	 -4,   0,   2,   8,   8,   2,   0,  -4,
	 -6,  -2,   4,   2,   2,   4,  -2,  -6,
	 -8,   0,  -2,   0,   0,  -2,   0,  -8,
	-18, -18, -16, -14, -14, -16, -18, -18
};
static const int pcsq_bishop_eg[64] =
{
	-18, -12,  -9,  -6,  -6,  -9, -12, -18,
	-12,  -6,  -3,   0,   0,  -3,  -6, -12,
	 -9,  -3,   0,   3,   3,   0,  -3,  -9,
	 -6,   0,   3,   6,   6,   3,   0,  -6,
	 -6,   0,   3,   6,   6,   3,   0,  -6,
	 -9,  -3,   0,   3,   3,   0,  -3,  -9,
	-12,  -6,  -3,   0,   0,  -3,  -6, -12,
	-18, -12,  -9,  -6,  -6,  -9, -12, -18
};
static const int pcsq_rook_op[64] =
{
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6,
	 -6,  -3,   0,   3,   3,   0,  -3,  -6
};
static const int pcsq_queen_op[64] =
{
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	 -5,  -5,  -5,  -5,  -5,  -5,  -5,  -5
};
static const int pcsq_queen_eg[64] =
{
	-24, -16, -12,  -8,  -8, -12, -16, -24,
	-16,  -8,  -4,   0,   0,  -4,  -8, -16,
	-12,  -4,   0,   4,   4,   0,  -4, -12,
	 -8,   0,   4,   8,   8,   4,   0,  -8,
	 -8,   0,   4,   8,   8,   4,   0,  -8,
	-12,  -4,   0,   4,   4,   0,  -4, -12,
	-16,  -8,  -4,   0,   0,  -4,  -8, -16,
	-24, -16, -12,  -8,  -8, -12, -16, -24
};
static const int pcsq_king_op[64] =
{
	-40, -30, -50, -70, -70, -50, -30, -40,
	-30, -20, -40, -60, -60, -40, -20, -30,
	-20, -10, -30, -50, -50, -30, -10, -20,
	-10,   0, -20, -40, -40, -20,   0, -10,
	  0,  10, -10, -30, -30, -10,  10,   0,
	 10,  20,   0, -20, -20,   0,  20,  10,
	 30,  40,  20,   0,   0,  20,  40,  30,
	 40,  50,  30,  10,  10,  30,  50,  40
};
static const int pcsq_king_eg[64] =
{
	-72, -48, -36, -24, -24, -36, -48, -72,
	-48, -24, -12,   0,   0, -12, -24, -48,
	-36, -12,   0,  12,  12,   0, -12, -36,
	-24,   0,  12,  24,  24,  12,   0, -24,
	-24,   0,  12,  24,  24,  12,   0, -24,
	-36, -12,   0,  12,  12,   0, -12, -36,
	-48, -24, -12,   0,   0, -12, -24, -48,
	-72, -48, -36, -24, -24, -36, -48, -72
};


static void
init_fwd_masks(void)
{
	int i;
	U64 white_mask;
	U64 black_mask;

	for (i = 0; i < 64; i++) {
		int j;
		white_mask = 0;
		black_mask = 0;
		for (j = SQ_FILE(i); j < 64; j += 8) {
			if (j < i)
				white_mask |= bit64[j];
			else if (j > i)
				black_mask |= bit64[j];
		}
		fwd_mask[WHITE][i] = white_mask;
		fwd_mask[BLACK][i] = black_mask;
	}
}

static void
init_pawn_shelter_masks(void)
{
	int color;

	for (color = WHITE; color <= BLACK; color++) {
		int i;
		int sign = SIGN(color);
		for (i = 0; i < 64; i++) {
			int j;
			U64 mask = 0;
			
			for (j = i; is_on_board(j); j -= sign*8) {
				int left;
				int right;
				
				mask |= bit64[j];
				left = j - 1;
				if (SQ_FILE(left) < 7)
					mask |= bit64[left];
				right = j + 1;
				if (SQ_FILE(right) > 0)
					mask |= bit64[right];
			}
			pawn_shelter[color][i] = mask;
		}
	}
}

static void
init_passer_masks(void)
{
	int color;

	for (color = WHITE; color <= BLACK; color++) {
		int i;
		int sign = SIGN(color);
		for (i = 0; i < 64; i++) {
			int j;
			U64 mask = 0;
			
			for (j = i - sign*8; is_on_board(j); j -= sign*8) {
				int left;
				int right;
				
				mask |= bit64[j];
				left = j - 1;
				if (SQ_FILE(left) < 7)
					mask |= bit64[left];
				right = j + 1;
				if (SQ_FILE(right) > 0)
					mask |= bit64[right];
			}
			passer[color][i] = mask;
		}
	}
}

static void
init_backward_pawn_masks(void)
{
	int color;

	for (color = WHITE; color <= BLACK; color++) {
		int i;
		int sign = SIGN(color);
		for (i = 0; i < 64; i++) {
			int j;
			U64 mask = 0;
			
			for (j = i; is_on_board(j); j += sign*8) {
				int left;
				int right;
				
				//if (j != i)
				//	mask |= bit64[j];
				left = j - 1;
				if (SQ_FILE(left) < 7)
					mask |= bit64[left];
				right = j + 1;
				if (SQ_FILE(right) > 0)
					mask |= bit64[right];
			}
			backw_pawn[color][i] = mask;
		}
	}
}

static void
init_ka_masks(void)
{
	int sq;
	const int sq_table[25] = {
		  0,   0, -16,   0,   0,
		  0,  -9,  -8,  -7,   0,
		 -2,  -1,   0,   1,   2,
		  0,   7,   8,   9,   0,
		  0,   0,  16,   0,   0
	};

	for (sq = 0; sq < 64; sq++) {
		int i;
		U64 mask = 0;

		for (i = 0; i < 25; i++) {
			int sq2;
			int d_file;
			sq2 = sq + sq_table[i];
			if (sq2 < 0 || sq2 > 63)
				continue;
			d_file = SQ_FILE(sq) - SQ_FILE(sq2);
			if (d_file < 0)
				d_file = -d_file;
			if (d_file <= 2)
				mask |= bit64[sq2];
		}
		ka_mask[sq] = mask;
	}
}

static void
init_pawn_hash(void)
{
	int i;
	PawnHash *hash;
	
	pawn_hash = calloc(PHASH_SIZE, sizeof(PawnHash));
	
	for (i = 0; i < PHASH_SIZE; i++) {
		hash = &pawn_hash[i];
		hash->passers = 0;
		hash->key = 1;
		hash->op = 0;
		hash->eg = 0;
	}
}

void
destroy_pawn_hash(void)
{
	if (pawn_hash != NULL) {
		free(pawn_hash);
		pawn_hash = NULL;
	}
}

void
init_eval(void)
{
	init_fwd_masks();
	init_pawn_shelter_masks();
	init_passer_masks();
	init_backward_pawn_masks();
	init_ka_masks();
	init_pawn_hash();
}


/* Functions and structures for displaying a detailed evaluation report.  */
#ifdef DEBUG_EVAL

typedef enum _LogCode
{
	LOG_MATERIAL, LOG_POSITION, LOG_PIECE, LOG_TEMPO, LOG_MOBILITY,
	LOG_KING_ATTACK, LOG_PAWN_STORM, LOG_PAWN_SHELTER, LOG_KING_SAFETY,
	LOG_PAWN, LOG_PASSER, LOG_DOUBLED, LOG_ISOLATED, LOG_BACKWARD,
	LOG_BACKWARD_OPEN, LOG_CANDIDATE, LOG_PATTERN, LOG_COUNT, LOG_ERROR
} LogCode;

static int eval_log[2][LOG_COUNT][2];

static void
init_eval_log(void)
{
	int color;
	LogCode code;

	for (color = WHITE; color <= BLACK; color++) {
		for (code = LOG_MATERIAL; code < LOG_COUNT; code++) {
			eval_log[color][code][OPENING] = 0;
			eval_log[color][code][ENDGAME] = 0;
		}
	}
}

static int
get_score(Board *board, int op, int eg)
{
	int phase;
	
	ASSERT(2, board != NULL);

	phase = board->phase;
	if (phase < 0)
		phase = 0;
	return ((op * (max_phase - phase)) + (eg * phase)) / max_phase;
}

static void
print_val(Board *board, LogCode code)
{
	int white[2];
	int black[2];
	int white_tot;
	int black_tot;
	int op;
	int eg;
	int score;

	ASSERT(2, board != NULL);

	white[OPENING] = eval_log[WHITE][code][OPENING];
	white[ENDGAME] = eval_log[WHITE][code][ENDGAME];
	black[OPENING] = eval_log[BLACK][code][OPENING];
	black[ENDGAME] = eval_log[BLACK][code][ENDGAME];
	white_tot = get_score(board, white[OPENING], white[ENDGAME]);
	black_tot = get_score(board, black[OPENING], black[ENDGAME]);
	op = white[OPENING] - black[OPENING];
	eg = white[ENDGAME] - black[ENDGAME];
	score = get_score(board, op, eg);
	printf("%5d\t%5d\t%5d\n", score, op, eg);
}

static void
finish_log(void)
{
	int color;
	for (color = WHITE; color <= BLACK; color++) {
		int phase;
		for (phase = OPENING; phase <= ENDGAME; phase++) {
			eval_log[color][LOG_PIECE][phase] +=
				eval_log[color][LOG_MOBILITY][phase];
			eval_log[color][LOG_PAWN][phase] +=
				eval_log[color][LOG_DOUBLED][phase] +
				eval_log[color][LOG_ISOLATED][phase] +
				eval_log[color][LOG_BACKWARD][phase] +
				eval_log[color][LOG_BACKWARD_OPEN][phase] +
				eval_log[color][LOG_CANDIDATE][phase];
			eval_log[color][LOG_KING_SAFETY][phase] +=
				eval_log[color][LOG_KING_ATTACK][phase] +
				eval_log[color][LOG_PAWN_STORM][phase] +
				eval_log[color][LOG_PAWN_SHELTER][phase];
		}
	}
}

static void
print_eval_log(Board *board)
{
	int phase;
	
	ASSERT(2, board != NULL);

	phase = board->phase;
	if (phase < 0)
		phase = 0;
	finish_log();
	printf("Term\tScore\tOpening\tEndgame\n\n");
	printf("Material:\t");
	print_val(board, LOG_MATERIAL);
	printf("Position table:\t");
	print_val(board, LOG_POSITION);
	printf("Pawns:\t\t");
	print_val(board, LOG_PAWN);
	printf("Tempo:\t\t");
	print_val(board, LOG_TEMPO);
	printf("Pattern:\t");
	print_val(board, LOG_PATTERN);
	printf("Piece:\t\t");
	print_val(board, LOG_PIECE);
	printf("Mobility:\t");
	print_val(board, LOG_MOBILITY);
	printf("King:\t\t");
	print_val(board, LOG_KING_SAFETY);
	printf("King attack:\t");
	print_val(board, LOG_KING_ATTACK);
	printf("Pawn storm:\t");
	print_val(board, LOG_PAWN_STORM);
	printf("Pawn shelter:\t");
	print_val(board, LOG_PAWN_SHELTER);
	printf("Passed Pawns:\t");
	print_val(board, LOG_PASSER);
	printf("Phase: %d%%\n", (phase * 100) / max_phase);
}

#define LOG_OP(color, code, val) \
  eval_log[(color)][(code)][OPENING] += (val);
#define LOG_EG(color, code, val) \
  eval_log[(color)][(code)][ENDGAME] += (val);

#else /* not DEBUG_EVAL */

#define LOG_OP(color, code, val)
#define LOG_EG(color, code, val)
#define init_eval_log()
#define print_eval_log(board)

#endif /* not DEBUG_EVAL */


/* The Static Exchange Evaluator.
   It returns the likely outcome (or evaluation) of a piece exchange
   (or a series of them) cause by <move> (not necessarily a capture).  */
#define MAX_CAPTURES 32
int
see(Board *board, U32 move, int color)
{
	int from;
	int to;
	int pc;
	int capt;
	int prom;
	int ep_sq;
	int val;		/* value of captured piece */
	int nc;			/* number of captures */
	int capt_list[MAX_CAPTURES]; /* values of captured pieces */
	U64 attacks;
	U64 mask;		/* all pieces from both sides */
	U64 bq;			/* bishops and queens from both sides */
	U64 rq;			/* rooks and queens from both sides */
	U64 *whites;		/* pointer to the mask of all white pieces */
	U64 *blacks;		/* pointer to the mask of all black pieces */
	U64 from_mask = 0;	/* mask of the attacker's <from> square */
	
	ASSERT(2, board != NULL);
	ASSERT(2, move != NULLMOVE);

	from = GET_FROM(move);
	to = GET_TO(move);
	pc = GET_PC(move);
	capt = GET_CAPT(move);
	prom = GET_PROM(move);
	ep_sq = GET_EPSQ(move);
	val = 0;
	nc = 1;
	mask = board->all_pcs;
	if (ep_sq)
		mask ^= bit64[ep_sq];
	whites = &board->pcs[WHITE][ALL];
	blacks = &board->pcs[BLACK][ALL];
	bq = whites[BQ] | blacks[BQ];
	rq = whites[RQ] | blacks[RQ];


	/* Determine which squares attack <to> for each side. Initialize by
	   placing the piece on <to> first in the list as it is being captured
	   to start things off.  */
	attacks =
		(move_masks.pawn_capt[WHITE][to] & blacks[PAWN]) |
		(move_masks.pawn_capt[BLACK][to] & whites[PAWN]) |
		(move_masks.knight[to] & (whites[KNIGHT] | blacks[KNIGHT])) |
		(B_MAGIC(to, mask) & bq) |
		(R_MAGIC(to, mask) & rq) |
		(move_masks.king[to] & (whites[KING] | blacks[KING]));

	if (capt)
		val += pc_val[capt];
	if (prom)
		val += pc_val[prom] - pc_val[PAWN];
	capt_list[0] = val;

	/* The first piece to capture on <to> is the piece in <from>.  */
	color = !color;
	if (prom)
		capt = pc_val[prom];
	else
		capt = pc_val[pc];
	attacks ^= bit64[from];
	mask ^= bit64[from];

	/* If the moving piece is a slider, the move may have given way for
	   other sliders to attack <to>. So we update <attacks>.  */
	attacks |= (B_MAGIC(to, mask) & mask) & bq;
	attacks |= (R_MAGIC(to, mask) & mask) & rq;

	while (attacks) {
		/* No captures available, end of exchange.  */
		if (!(board->pcs[color][ALL] & attacks))
			break;
		/* Find the least valuable attacker of the side that is on
		   the move.  */
		for (pc = PAWN; pc <= KING; pc++) {
			from_mask = board->pcs[color][pc] & attacks;
			if (from_mask != 0) {
				from_mask &= -from_mask;
				break;
			}
		}

		ASSERT(2, nc < MAX_CAPTURES);
		val = 0;
		/* Handle promotion (if any).  */
		if (pc == PAWN && (from_mask & seventh_rank[color])) {
			val = pc_val[QUEEN] - pc_val[PAWN];
			pc = QUEEN;
		}
		
		val += capt;
		/* Add the value of the captured piece to the list.  */
		capt_list[nc] = -capt_list[nc - 1] + val;
		nc++;
		/* A king capture ends it all.  */
		if (capt == pc_val[KING])
			break;
		/* The current attacker will be the next capture victim,
		   so assign it to <capt>.  */
		capt = pc_val[pc];

		attacks ^= from_mask;
		mask ^= from_mask;

		/* Update <attacks> in case there are new pieces capable of
		   attacking <to>.  */
		attacks |= (B_MAGIC(to, mask) & mask) & bq;
		attacks |= (R_MAGIC(to, mask) & mask) & rq;

		color = !color;
	}
	/* Starting at the end of the sequence of values, use a "minimax" like
	   procedure to decide where the captures will stop.  */
	while (--nc) {
		if (capt_list[nc] > -capt_list[nc - 1])
			capt_list[nc - 1] = -capt_list[nc];
	}

	return capt_list[0];
}

/* Returns the distance between two squares.  */
static int
get_distance(int sq1, int sq2)
{
	int delta_rank = SQ_RANK(sq1) - SQ_RANK(sq2);
	int delta_file = SQ_FILE(sq1) - SQ_FILE(sq2);

	if (delta_rank < 0)
		delta_rank = -delta_rank;
	if (delta_file < 0)
		delta_file = -delta_file;
	if (delta_rank >= delta_file)
		return delta_rank;
	return delta_file;
}

/* If true, the moving piece is a passed pawn.  */
bool
is_passer_move(Board *board, U32 move)
{
	int color;

	ASSERT(2, board != NULL);
	ASSERT(2, move != NULLMOVE);
	
	color = board->color;
	if (GET_PC(move) != PAWN
	|| board->pcs[!color][PAWN] & passer[color][GET_TO(move)])
		return false;
	return true;
}

/* From Fruit 2.1.  */
static int
quad(int y_min, int y_max, int x)
{

	int y;
	static const int passer_bonus[8] = { 0, 0, 0, 26, 77, 154, 256, 0 };

	y = y_min + ((y_max - y_min) * passer_bonus[x] + 128) / 256;

	return y;
}

static bool
unstoppable_passer(Board *board, int color, int sq)
{

	int prom_sq;
	int dist;

	ASSERT(2, board != NULL);

	/* Clear promotion path?  */
	if (board->all_pcs & fwd_mask[color][sq])
		return false;

	if (bit64[sq] & seventh_rank[!color])
		sq -= SIGN(color)*8;

	prom_sq = FLIP(SQ_FILE(sq), color);

	dist = get_distance(sq, prom_sq);
	if (board->color == !color)
		dist++;

	if (get_distance(board->king_sq[!color], prom_sq) > dist)
		return true;

	return false;
}

/* Return true if the opposing king can't catch the pawn.  */
static bool
king_passer(Board *board, int color, int pawn_sq)
{

	int king_sq;
	int prom_sq;
	int file;

	ASSERT(2, board != NULL);

	king_sq = board->king_sq[color];
	file = SQ_FILE(pawn_sq);
	prom_sq = FLIP(file, color);

	if ((move_masks.king[king_sq] & bit64[prom_sq])
	&&  (move_masks.king[king_sq] & bit64[pawn_sq])
	&&  (SQ_FILE(king_sq) != file || (file != 0 && file != 7)))
		return true;

	return false;
}

/* Returns true if the pawn can advance safely.  */
static bool
free_passer(Board *board, int color, int from)
{

	int to;
	int prom;
	U32 move;

	ASSERT(2, board != NULL);

	to = from - SIGN(color)*8;
	ASSERT(2, is_on_board(to));

	if (board->mailbox[to] != 0)
		return false;

	if (bit64[from] & seventh_rank[color])
		prom = QUEEN;
	else
		prom = 0;
	move = simple_move(PAWN, from, to, prom);
	if (see(board, move, color) < 0)
		return false;

	return true;
}

/* Evaluate a passed pawn.  */
static void
passer_eval(Board *board, int color, int sq, EvalData *ed)
{
	int delta;
	int rank = SQ_RANK(sq);
	int sign = SIGN(color);
	int *op;
	int *eg;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	op = &ed->op;
	eg = &ed->eg;

	if (color == WHITE)
		rank = 7 - rank;

	/* Ppening scoring.  */
	*op += quad(10, 70, rank);
	LOG_OP(color, LOG_PASSER, quad(10, 70, rank));

	/* Endgame scoring init.  */
	delta = 140 - 20;

	/* "Dangerous" bonus.  */
	if (board->material[!color] == 0
	&& (unstoppable_passer(board, color, sq) ||
	    king_passer(board, color, sq)))
		delta += 800;
	else if (free_passer(board, color, sq))
		delta += 60;

	/* King-distance bonus.  */
	delta -= get_distance(sq - sign*8, board->king_sq[color]) * 5;
	delta += get_distance(sq - sign*8, board->king_sq[!color]) * 20;

	/* Endgame scoring.  */
	*eg += 20;
	LOG_EG(color, LOG_PASSER, 20);
	if (delta > 0) {
		*eg += quad(0, delta, rank);
		LOG_EG(color, LOG_PASSER, quad(0, delta, rank));
	}
}

static bool
is_backward(Board *board, int color, int sq)
{
	U64 my_pawns;
	U64 op_pawns;
	U64 pawns;
	U64 *capts;
	int plus1;
	int plus2;
	int sign = SIGN(color);

	ASSERT(2, board != NULL);

	my_pawns = board->pcs[color][PAWN];
	op_pawns = board->pcs[!color][PAWN];
	pawns = my_pawns | op_pawns;
	capts = &move_masks.pawn_capt[color][0];
	plus1 = sq - sign*8;
	plus2 = sq - sign*16;

	if (my_pawns & backw_pawn[color][sq])
		return false;
	if ((my_pawns & capts[sq])
	&&  !(pawns & bit64[plus1])
	&&  !(op_pawns & (capts[sq] | capts[plus1])))
		return false;
	if ((bit64[sq] & seventh_rank[!color])
	&&  (my_pawns & capts[plus1])
	&&  !(pawns & (bit64[plus1] | bit64[plus2]))
	&&  !(op_pawns & (capts[sq] | capts[plus1] | capts[plus2])))
		return false;
	
	return true;
}

static bool
is_candidate(Board *board, int color, int sq)
{
	U64 my_pawns;
	U64 op_pawns;
	
	ASSERT(2, board != NULL);

	my_pawns = board->pcs[color][PAWN];
	op_pawns = board->pcs[!color][PAWN];
	
	if (popcount(passer[color][sq] & op_pawns) >
	    popcount(backw_pawn[color][sq] & my_pawns))
		return false;
	if (popcount(move_masks.pawn_capt[color][sq] & op_pawns) >
	    popcount(move_masks.pawn_capt[!color][sq] & my_pawns))
		return false;

	return true;
}

static void
pawn_eval(Board *board, int color, int sq, EvalData *ed, int *hash_op, int *hash_eg, U64 *passers)
{
	bool open = false;
	U64 *my_pcs;
	U64 *oppn_pcs;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	my_pcs = &board->pcs[color][ALL];
	oppn_pcs = &board->pcs[!color][ALL];
		
	*hash_op += VAL_PAWN;
	LOG_OP(color, LOG_MATERIAL, VAL_PAWN);
	*hash_eg += VAL_PAWN_EG;
	LOG_EG(color, LOG_MATERIAL, VAL_PAWN_EG);
	*hash_op += pcsq_pawn_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_pawn_op[FLIP(sq, color)]);
	if ((fwd_mask[color][sq] & (my_pcs[PAWN] | oppn_pcs[PAWN])) == 0)
		open = true;
	
	if (open) {
		if (!(oppn_pcs[PAWN] & passer[color][sq])) {
			passer_eval(board, color, sq, ed);
			*passers |= bit64[sq];
		}
		else if (is_candidate(board, color, sq)) {
			int rank = SQ_RANK(sq);
			if (color == WHITE)
				rank = 7 - rank;
			*hash_op += quad(5, 55, rank);
			LOG_OP(color, LOG_CANDIDATE, quad(5, 55, rank));
			*hash_eg += quad(10, 110, rank);
			LOG_EG(color, LOG_CANDIDATE, quad(10, 110, rank));
		}
	} else if (my_pcs[PAWN] & fwd_mask[color][sq]) {
		*hash_op += DOUBLED_PAWN_OP;
		LOG_OP(color, LOG_DOUBLED, DOUBLED_PAWN_OP);
		*hash_eg += DOUBLED_PAWN_EG;
		LOG_EG(color, LOG_DOUBLED, DOUBLED_PAWN_EG);
	}
	if (!(my_pcs[PAWN] & isolated_pawn[SQ_FILE(sq)])) {
		if (!open) {
			*hash_op += ISOLATED_PAWN_OP;
			LOG_OP(color, LOG_ISOLATED, ISOLATED_PAWN_OP);
			*hash_eg += ISOLATED_PAWN_EG;
			LOG_EG(color, LOG_ISOLATED, ISOLATED_PAWN_EG);
		} else {
			*hash_op += ISOLATED_OPEN_PAWN_OP;
			LOG_OP(color, LOG_ISOLATED, ISOLATED_OPEN_PAWN_OP);
			*hash_eg += ISOLATED_OPEN_PAWN_EG;
			LOG_EG(color, LOG_ISOLATED, ISOLATED_OPEN_PAWN_EG);
		}
	} else if (is_backward(board, color, sq)) {
		if (!open) {
			*hash_op += BACKWARD_PAWN_OP;
			LOG_OP(color, LOG_BACKWARD, BACKWARD_PAWN_OP);
			*hash_eg += BACKWARD_PAWN_EG;
			LOG_EG(color, LOG_BACKWARD, BACKWARD_PAWN_EG);
		} else {
			*hash_op += BACKWARD_OPEN_PAWN_OP;
			LOG_OP(color, LOG_BACKWARD_OPEN, BACKWARD_OPEN_PAWN_OP);
			*hash_eg += BACKWARD_OPEN_PAWN_EG;
			LOG_EG(color, LOG_BACKWARD_OPEN, BACKWARD_OPEN_PAWN_EG);
		}
	}
}

static void
knight_eval(Board *board, int color, int sq, EvalData *ed)
{
	int mob;
	int *op;
	int *eg;
	static const int knight_outpost[64] = {
		 0,  0,  0,  0,  0,  0,  0,  0,
		 0,  0,  0,  0,  0,  0,  0,  0,
		 0,  0,  4,  5,  5,  4,  0,  0,
		 0,  2,  5, 10, 10,  5,  2,  0,
		 0,  2,  5, 10, 10,  5,  2,  0,
		 0,  0,  0,  0,  0,  0,  0,  0,
		 0,  0,  0,  0,  0,  0,  0,  0,
		 0,  0,  0,  0,  0,  0,  0,  0
	};

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);
	
	op = &ed->op;
	eg = &ed->eg;

	/* Piece/square.  */
	*op += pcsq_knight_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_knight_op[FLIP(sq, color)]);
	*eg += pcsq_knight_eg[sq];
	LOG_EG(color, LOG_POSITION, pcsq_knight_eg[sq]);

	/* Mobility.  */
	mob = popcount(move_masks.knight[sq] & ~board->pcs[color][ALL]);
	*op += (mob - 4) * 4;
	LOG_OP(color, LOG_MOBILITY, (mob - 4) * 4);
	*eg += (mob - 4) * 4;
	LOG_EG(color, LOG_MOBILITY, (mob - 4) * 4);
	
	/* Knight outpost.  */
	if (knight_outpost[FLIP(sq, color)] > 0) {
		U64 pawns = board->pcs[color][PAWN];
		pawns &= move_masks.pawn_capt[!color][sq];
		if (pawns) {
			int bonus = knight_outpost[FLIP(sq, color)];
			pawns ^= pawns & -pawns;
			if (pawns)
				bonus *= 2;
			*op += bonus;
			LOG_OP(color, LOG_PIECE, bonus);
			*eg += bonus;
			LOG_EG(color, LOG_PIECE, bonus);
		}
	}
}

static int
trapped_bishop(Board *board, int color, int sq)
{
	const U64 btrap_mask = 0x7E7E7E7E7E7E7E7E;
	U64 op_pawns;
	
	ASSERT(2, board != NULL);
	
	op_pawns = board->pcs[!color][PAWN] & btrap_mask;
	
	switch (FLIP(sq, color)) {
	case A7: case B8: case H7: case G8:
		if (move_masks.pawn_capt[!color][sq] & op_pawns)
			return 2;
		break;
	case A6: case H6:
		if (move_masks.pawn_capt[!color][sq] & op_pawns)
			return 1;
		break;
	}
	
	return 0;
}

static bool
blocked_bishop(Board *board, int color, int sq)
{
	ASSERT(2, board != NULL);

	switch (FLIP(sq, color)) {
	case C1:
		if ((bit64[FLIP(D2, color)] & board->pcs[color][PAWN])
		&&  board->mailbox[FLIP(D3, color)] != 0)
			return true;
		break;
	case F1:
		if ((bit64[FLIP(E2, color)] & board->pcs[color][PAWN])
		&&  board->mailbox[FLIP(E3, color)] != 0)
			return true;
		break;
	}
	
	return false;
}

static void
bishop_eval(Board *board, int color, int sq, EvalData *ed)
{
	int mob;
	int tb_score;
	int *op;
	int *eg;
	U64 mask;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	op = &ed->op;
	eg = &ed->eg;

	/* Piece/square.  */
	*op += pcsq_bishop_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_bishop_op[FLIP(sq, color)]);
	*eg += pcsq_bishop_eg[sq];
	LOG_EG(color, LOG_POSITION, pcsq_bishop_eg[sq]);
	
	/* Mobility.  */
	mask = B_MAGIC(sq, board->all_pcs);
	mob = popcount(mask & ~board->pcs[color][ALL]);
	*op += (mob - 6) * 5;
	LOG_OP(color, LOG_MOBILITY, (mob - 6) * 5);
	*eg += (mob - 6) * 5;
	LOG_EG(color, LOG_MOBILITY, (mob - 6) * 5);
	
	/* Trapped/blocked bishop penalty.  */
	tb_score = trapped_bishop(board, color, sq) * TRAPPED_BISHOP;
	if (tb_score) {
		*op += tb_score;
		LOG_OP(color, LOG_PATTERN, tb_score);
		*eg += tb_score;
		LOG_EG(color, LOG_PATTERN, tb_score);
	} else if (blocked_bishop(board, color, sq)) {
		*op += BLOCKED_BISHOP;
		LOG_OP(color, LOG_PATTERN, BLOCKED_BISHOP);
	}
}

static void
rook_file_bonus(Board *board, int color, int sq, EvalData *ed)
{
	int file;
	int king_file;
	int *op;
	int *eg;
	
	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);
	
	op = &ed->op;
	eg = &ed->eg;
	
	file = SQ_FILE(sq);
	if (file_mask[file] & board->pcs[color][PAWN]) {
		*op += ROOK_CLOSED_OP;
		LOG_OP(color, LOG_PIECE, ROOK_CLOSED_OP);
		*eg += ROOK_CLOSED_EG;
		LOG_EG(color, LOG_PIECE, ROOK_CLOSED_EG);
		return;
	}
	
	king_file = SQ_FILE(board->king_sq[!color]);
	if (file_mask[file] & board->pcs[!color][PAWN]) {
		if (file == king_file) {
			*op += ROOK_SEMIOPEN_SAME_OP;
			LOG_OP(color, LOG_PIECE, ROOK_SEMIOPEN_SAME_OP);
			*eg += ROOK_SEMIOPEN_SAME_EG;
			LOG_EG(color, LOG_PIECE, ROOK_SEMIOPEN_SAME_EG);
		} else if (file == king_file - 1 || file == king_file + 1) {
			*op += ROOK_SEMIOPEN_ADJACENT_OP;
			LOG_OP(color, LOG_PIECE, ROOK_SEMIOPEN_ADJACENT_OP);
			*eg += ROOK_SEMIOPEN_ADJACENT_EG;
			LOG_EG(color, LOG_PIECE, ROOK_SEMIOPEN_ADJACENT_EG);
		}
	} else {
		if (file == king_file) {
			*op += ROOK_OPEN_SAME_OP;
			LOG_OP(color, LOG_PIECE, ROOK_OPEN_SAME_OP);
			*eg += ROOK_OPEN_SAME_EG;
			LOG_EG(color, LOG_PIECE, ROOK_OPEN_SAME_EG);
		} else if (file == king_file - 1 || file == king_file + 1) {
			*op += ROOK_OPEN_ADJACENT_OP;
			LOG_OP(color, LOG_PIECE, ROOK_OPEN_ADJACENT_OP);
			*eg += ROOK_OPEN_ADJACENT_EG;
			LOG_EG(color, LOG_PIECE, ROOK_OPEN_ADJACENT_EG);
		} else {
			*op += ROOK_OPEN_OP;
			LOG_OP(color, LOG_PIECE, ROOK_OPEN_OP);
			*eg += ROOK_OPEN_EG;
			LOG_EG(color, LOG_PIECE, ROOK_OPEN_EG);
		}
	}
}

static bool
blocked_rook(Board *board, int color, int sq)
{
	int king_sq;
	
	ASSERT(2, board != NULL);
	
	king_sq = FLIP(board->king_sq[color], color);
	switch (FLIP(sq, color)) {
	case A1: case A2: case B1:
		if (king_sq == B1 || king_sq == C1)
			return true;
		break;
	case H1: case H2: case G1:
		if (king_sq == F1 || king_sq == G1)
			return true;
		break;
	}

	return false;
}

static void
rook_eval(Board *board, int color, int sq, EvalData *ed)
{
	int mob;
	int *op;
	int *eg;
	U64 mask;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	op = &ed->op;
	eg = &ed->eg;

	/* Piece/square.  */
	*op += pcsq_rook_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_rook_op[FLIP(sq, color)]);

	/* Semi-open file, open file, etc.  */
	rook_file_bonus(board, color, sq, ed);

	/* 7th rank bonus.  */
	if ((bit64[sq] & seventh_rank[color])
	&& ((board->pcs[!color][PAWN] & seventh_rank[color]) ||
	    (board->pcs[!color][KING] & eighth_rank[color]))) {
		*op += ROOK_ON_7TH_OP;
		LOG_OP(color, LOG_PIECE, ROOK_ON_7TH_OP);
		*eg += ROOK_ON_7TH_EG;
		LOG_EG(color, LOG_PIECE, ROOK_ON_7TH_EG);
	}
	
	/* Mobility.  */
	mask = R_MAGIC(sq, board->all_pcs);
	mob = popcount(mask & ~board->pcs[color][ALL]);
	*op += (mob - 7) * 2;
	LOG_OP(color, LOG_MOBILITY, (mob - 7) * 2);
	*eg += (mob - 7) * 4;
	LOG_EG(color, LOG_MOBILITY, (mob - 7) * 4);
	
	if (blocked_rook(board, color, sq)) {
		*op += BLOCKED_ROOK;
		LOG_OP(color, LOG_PATTERN, BLOCKED_ROOK);
	}
}

static void
queen_eval(Board *board, int color, int sq, EvalData *ed)
{
	int dist_file;
	int dist_rank;
	int *op;
	int *eg;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);
	
	op = &ed->op;
	eg = &ed->eg;

	/* Piece/square.  */
	*op += pcsq_queen_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_queen_op[FLIP(sq, color)]);
	*eg += pcsq_queen_eg[sq];
	LOG_EG(color, LOG_POSITION, pcsq_queen_eg[sq]);

	/* 7th rank bonus.  */
	if ((bit64[sq] & seventh_rank[color])
	&& ((board->pcs[!color][PAWN] & seventh_rank[color]) ||
	    (board->pcs[!color][KING] & eighth_rank[color]))) {
		*op += QUEEN_ON_7TH_OP;
		LOG_OP(color, LOG_PIECE, QUEEN_ON_7TH_OP);
		*eg += QUEEN_ON_7TH_EG;
		LOG_EG(color, LOG_PIECE, QUEEN_ON_7TH_EG);
	}
	
	/* King-distance bonus.  */
	dist_file = SQ_FILE(board->king_sq[!color]) - SQ_FILE(sq);
	if (dist_file < 0)
		dist_file = -dist_file;
	dist_rank = SQ_RANK(board->king_sq[!color]) - SQ_RANK(sq);
	if (dist_rank < 0)
		dist_rank = -dist_rank;
	*op += 10 - dist_file - dist_rank;
	LOG_OP(color, LOG_PIECE, 10 - dist_file - dist_rank);
	*eg += 10 - dist_file - dist_rank;
	LOG_EG(color, LOG_PIECE, 10 - dist_file - dist_rank);
}

static void
pawn_shelter_eval(Board *board, int color, EvalData *ed)
{
	U64 mask;
	U64 my_pawns;
	int score = 0;
	int npawns;
	int k_file;

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	k_file = SQ_FILE(board->king_sq[color]);
	if (k_file == 0 || k_file == 7)
		npawns = 2;
	else
		npawns = 3;
	my_pawns = board->pcs[color][PAWN];
	mask = pawn_shelter[color][board->king_sq[color]] & my_pawns;
	my_pawns = mask;
	while (mask) {
		int sq;
		int dist;
		int penalty;

		sq = pop_lsb(&mask);
		if (fwd_mask[!color][sq] & my_pawns)
			continue;
		npawns--;
		if (color == WHITE)
			dist = SQ_RANK(sq);
		else
			dist = 7 - SQ_RANK(sq);
		penalty = 36 - (dist * dist);
		if (SQ_FILE(sq) == k_file)
			penalty *= 2;
		score -= penalty;
	}
	ASSERT(2, npawns >= 0);
	score -= npawns * 36;
	if (!(fwd_mask[color][board->king_sq[color]] & my_pawns))
		score -= 36;
	if (score == 0)
		score = -11;
	ed->op += score;
	LOG_OP(color, LOG_PAWN_SHELTER, score);
}

static void
pawn_storm_eval(Board *board, int color, EvalData *ed)
{
	U64 mask;
	static const int pawn_storm[] = { 0, 0, 0, -10, -30, -60, 0, 0 };

	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	mask = passer[color][board->king_sq[color]] & board->pcs[!color][PAWN];
	while (mask) {
		int rank;

		rank = SQ_RANK(pop_lsb(&mask));
		if (color == BLACK)
			rank = 7 - rank;
		ed->op += pawn_storm[rank];
		LOG_OP(color, LOG_PAWN_STORM, pawn_storm[rank]);
	}
}

static void
king_eval(Board *board, int color, int sq, EvalData *ed)
{
	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);

	/* Piece/square.  */
	ed->op += pcsq_king_op[FLIP(sq, color)];
	LOG_OP(color, LOG_POSITION, pcsq_king_op[FLIP(sq, color)]);
	ed->eg += pcsq_king_eg[sq];
	LOG_EG(color, LOG_POSITION, pcsq_king_eg[sq]);
}

/* Get a mask of all attacks (no castling, pawn pushes, or king moves)
   by <color>, and assign the combined value of king attacks to <sum>.  */
#define FWD_LEFT(mask, color) ((color) == WHITE ? (mask) >> 9 : (mask) << 7)
#define FWD_RIGHT(mask, color) ((color) == WHITE ? (mask) >> 7 : (mask) << 9)
static U64
get_attack_mask(Board *board, int color, int *sum)
{
	int sq;		/* attacked square */
	U64 mask;	/* attacking pieces of one piece type */
	U64 attacks;	/* all attacked squares */
	U64 tmp;	/* squares attacked by one piece */
	U64 ka;		/* king-attack mask */
	U64 *my_pcs;
	
	ASSERT(2, board != NULL);
	ASSERT(2, sum != NULL);

	*sum = 0;
	ka = ka_mask[board->king_sq[!color]];
	my_pcs = &board->pcs[color][ALL];

	/* Pawn attacks.  */
	attacks = (FWD_LEFT(board->pcs[color][PAWN], color) & FILE_A_G) |
	          (FWD_RIGHT(board->pcs[color][PAWN], color) & FILE_B_H);


	/* Knight attacks.  */
	mask = my_pcs[KNIGHT];
	while (mask) {
		sq = pop_lsb(&mask);
		tmp = move_masks.knight[sq];
		if (tmp & move_masks.king[board->king_sq[!color]])
			*sum += 3;
		attacks |= tmp;
	}

	/* Bishop attacks.  */
	mask = my_pcs[BISHOP];
	while (mask) {
		sq = pop_lsb(&mask);
		tmp = B_MAGIC(sq, board->all_pcs);
		if (tmp & ka)
			*sum += 3;
		attacks |= tmp;
	}

	/* Rook attacks.  */
	mask = my_pcs[ROOK];
	while (mask) {
		sq = pop_lsb(&mask);
		tmp = R_MAGIC(sq, board->all_pcs);
		if (tmp & ka)
			*sum += 6;
		attacks |= tmp;
	}
	
	/* Queen attacks */
	mask = my_pcs[QUEEN];
	while (mask) {
		sq = pop_lsb(&mask);
		tmp = Q_MAGIC(sq, board->all_pcs);
		if (tmp & ka)
			*sum += 12;
		attacks |= tmp;
	}
	
	return attacks;
}

/* Evaluate attacks and threats against the kings.  */
static void
king_attack_eval(Board *board, EvalData *ed)
{
	int color;
	int sum[2];
	bool do_ka[2] = { false, false };
	U64 attacks[2];
	
	ASSERT(1, board != NULL);

	if (board->material[WHITE] > VAL_QUEEN && board->pcs[WHITE][QUEEN])
		do_ka[WHITE] = true;
	if (board->material[BLACK] > VAL_QUEEN && board->pcs[BLACK][QUEEN])
		do_ka[BLACK] = true;
	if (!do_ka[WHITE] && !do_ka[BLACK])
		return;
	
	attacks[WHITE] = get_attack_mask(board, WHITE, &sum[WHITE]);
	attacks[BLACK] = get_attack_mask(board, BLACK, &sum[BLACK]);
	
	for (color = WHITE; color <= BLACK; color++) {
		int king_sq = board->king_sq[color];
		int op_king_sq = board->king_sq[!color];
		int counter = 0;
		int score;
		U64 mask;
		U64 atk;

		if (!do_ka[color])
			continue;
		/* The attacked squares. The king can join in an attack,
		   but it can't be a defending piece.  */
		atk = attacks[color] | move_masks.king[king_sq];
		
		/* Squares attacked by <color>.  */
		mask = ka_mask[op_king_sq] & atk;
		counter += popcount(mask);
		/* Attacked squares undefended by <!color>.  */
		mask &= ~attacks[!color];
		counter += popcount(mask);
		
		score = sum[color] + (sum[color] * counter) / 12;
		score = (score * score) / 11;
		ed->op += SIGN(color) * score;
		LOG_OP(color, LOG_KING_ATTACK, score);
	}
}

static bool
probe_pawn_hash(U64 key, U64 *passers, EvalData *ed)
{
	PawnHash *hash;
	
	ASSERT(2, passers != NULL);
	ASSERT(2, ed != NULL);

	if (key == 1)
		return false;
	hash = &pawn_hash[key % PHASH_SIZE];
	if (hash->key == key) {
		*passers = hash->passers;
		ed->op += hash->op;
		ed->eg += hash->eg;
		return true;
	}

	return false;
}

static void
store_pawn_hash(U64 key, U64 passers, int op, int eg)
{
	PawnHash *hash;
	
	hash = &pawn_hash[key % PHASH_SIZE];
	if (hash->key != key) {
		hash->key = key;
		hash->passers = passers;
		hash->op = op;
		hash->eg = eg;
	}
}

static void
eval_pawns(Board *board, EvalData *ed)
{
	int color;
	int hash_op = 0;
	int hash_eg = 0;
	int *op;
	int *eg;
	U64 mask;
	U64 pp; /* mask of passed pawns */
	
	op = &ed->op;
	eg = &ed->eg;
	
	/* If we get a hit from pawn hash, we still have to evaluate
	   passed pawns separately.  */
	if (probe_pawn_hash(board->posp->pawn_key, &mask, ed)) {
		if (!mask)
			return;

		for (color = WHITE; color <= BLACK; color++) {
			pp = mask & board->pcs[color][PAWN];
			while (pp)
				passer_eval(board, color, pop_lsb(&pp), ed);
			*op *= -1;
			*eg *= -1;
		}
		return;
	}

	pp = 0;
	for (color = WHITE; color <= BLACK; color++) {
		mask = board->pcs[color][PAWN];
		while (mask)
			pawn_eval(board, color, pop_lsb(&mask), ed, &hash_op, &hash_eg, &pp);
		*op *= -1;
		*eg *= -1;
		hash_op *= -1;
		hash_eg *= -1;
	}
	store_pawn_hash(board->posp->pawn_key, pp, hash_op, hash_eg);
	
	*op += hash_op;
	*eg += hash_eg;
}

/* Evaluate all pieces for one side.  */
static void
eval_pieces(Board *board, int color, EvalData *ed)
{
	int *op;
	int *eg;
	U64 mask;
	
	ASSERT(2, board != NULL);
	ASSERT(2, ed != NULL);
	
	op = &ed->op;
	eg = &ed->eg;
	
	mask = board->pcs[color][KNIGHT];
	while (mask)
		knight_eval(board, color, pop_lsb(&mask), ed);
	mask = board->pcs[color][BISHOP];
	while (mask)
		bishop_eval(board, color, pop_lsb(&mask), ed);
	mask = board->pcs[color][ROOK];
	while (mask)
		rook_eval(board, color, pop_lsb(&mask), ed);
	mask = board->pcs[color][QUEEN];
	while (mask)
		queen_eval(board, color, pop_lsb(&mask), ed);

	king_eval(board, color, board->king_sq[color], ed);
}

static void
init_ed(EvalData *ed)
{
	ed->op = 0;
	ed->eg = 0;
}

/* The main static evaluation function.  */
int
eval(Board *board)
{
	int phase;
	int score;
	int color;
	EvalData ed;

	ASSERT(2, board != NULL);
	ASSERT(2, !board_is_check(board));
	init_ed(&ed);
	init_eval_log();

	for (color = WHITE; color <= BLACK; color++) {
		/* Material.  */
		ed.op += board->material[color];
		LOG_OP(color, LOG_MATERIAL, board->material[color]);
		ed.eg += board->material[color];
		LOG_EG(color, LOG_MATERIAL, board->material[color]);
		
		/* Some of the king safety.  */
		if (board->material[!color] > VAL_QUEEN
		&& board->pcs[!color][QUEEN]) {
			pawn_shelter_eval(board, color, &ed);
			pawn_storm_eval(board, color, &ed);
		}

		eval_pieces(board, color, &ed);

		/* Double bishop eval.  */
		if ((board->pcs[color][BISHOP] & WHITE_SQUARES)
		&&  (board->pcs[color][BISHOP] & BLACK_SQUARES)) {
			ed.op += DOUBLE_BISHOPS_OP;
			LOG_OP(color, LOG_MATERIAL, DOUBLE_BISHOPS_OP);
			ed.eg += DOUBLE_BISHOPS_EG;
			LOG_EG(color, LOG_MATERIAL, DOUBLE_BISHOPS_EG);
		}

		ed.op = -ed.op;
		ed.eg = -ed.eg;
	}
	eval_pawns(board, &ed);
	king_attack_eval(board, &ed);

	phase = board->phase;
	if (phase < 0)
		phase = 0;
	print_eval_log(board);
	score = ((ed.op * (max_phase - phase)) + (ed.eg * phase)) / max_phase;
	
	return SIGN(board->color)*score;
}

