/* Sloppy - movegen.c
   Functions for generating chess moves.

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <stdlib.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "magicmoves.h"
#include "movegen.h"


/* Shift a bitboard's pieces forward, forward left or forward right.
   Naturally white's forward is black's backward and vice versa.  */
#define FWD(mask, color) ((color) == WHITE ? (mask) >> 8 : (mask) << 8)
#define FWD_LEFT(mask, color) ((color) == WHITE ? (mask) >> 9 : (mask) << 7)
#define FWD_RIGHT(mask, color) ((color) == WHITE ? (mask) >> 7 : (mask) << 9)

/* Macros for building a Move from parts.  */
#define SET_FROM(sq)	(sq)
#define SET_TO(sq)	((sq) << 6)
#define SET_PC(pc)	((pc) << 12)
#define SET_CAPT(pc)	((pc) << 15)
#define SET_PROM(pc)	((pc) << 18)
#define SET_EPSQ(sq)	((sq) << 21)


typedef struct _MoveData
{
	int from;		/* from square */
	int to;			/* to square */
	int ep_sq;		/* enpassant square */
	int prom;		/* promotion piece */
	int castle;		/* castling (none, kingside or queenside) */
	U64 r_chk;		/* check mask for bishops (and queens) */
	U64 b_chk;		/* check mask for rooks (and queens) */
	U64 pins;		/* pinned pieces */
	U64 discov_chk;		/* pieces able to give a discovered check */
	U64 target;		/* target mask for the moving piece */
} MoveData;


/* Pre-calculated move bitmasks.  */
MoveMasks move_masks;
/* X-ray attacks for rooks.  */
U64 rook_xray[64];
/* X-ray attacks for bishops.  */
U64 bishop_xray[64];

const U64 seventh_rank[2] = { 0x000000000000FF00, 0x00FF000000000000 };


/* A mask of a straight vertical, horizontal or diagonal line
   between two squares. The two squares are also included in the mask.
   If there is no such line between the squares the mask is empty.  */
static U64 connect_mask[64][64];
/* Just like <connect_mask>, except that the line from A to B continues
   through B until it reaches the edge of the board.  */
static U64 pin_mask[64][64];


/* Initialize <connect_mask> and <pin_mask>.  */
static void
init_connect_and_pin_masks(void)
{
	int sq1;
	int sq2;
	int file1;
	int file2;
	int rank1;
	int rank2;
	int dir;
	U64 mask;
	
	for (sq1 = 0; sq1 < 64; sq1++) {
		for (sq2 = 0; sq2 < 64; sq2++) {
			int i;
			
			connect_mask[sq1][sq2] = 0;
			pin_mask[sq1][sq2] = 0;

			file1 = SQ_FILE(sq1);
			file2 = SQ_FILE(sq2);
			rank1 = SQ_RANK(sq1);
			rank2 = SQ_RANK(sq2);
			
			/* Check if <sq1> and <sq2> are two different squares
			   that can be connected horizontally, vertically
			   or diagonally.  */
			if (sq1 == sq2)
				continue;
			if (file1 != file2 && rank1 != rank2
			&&  abs(file1 - file2) != abs(rank1 - rank2))
				continue;

			/* Get the direction of the line.  */
			dir = 0;
			if (file1 < file2)
				dir = 1;
			else if (file1 > file2)
				dir = -1;
			if (rank1 < rank2)
				dir += 8;
			else if (rank1 > rank2)
				dir -= 8;

			/* Get <connect_mask>.  */
			mask = 0;
			for (i = sq1 + dir; i != sq2; i += dir)
				mask |= bit64[i];
			connect_mask[sq1][sq2] = mask | bit64[sq2];

			/* Get <pin_mask>.  */
			mask = 0;
			for (i = sq1 + dir; i >= 0 && i < 64; i += dir) {
				mask |= bit64[i];
				if (SQ_FILE(i) == 0
				&&  (dir == -1 || dir == 7 || dir == -9))
					break;
				if (SQ_FILE(i) == 7
				&&  (dir == 1 || dir == -7 || dir == 9))
					break;
			}
			pin_mask[sq1][sq2] = mask;
		}
	}
}

/* Initialize bitmasks for rooks' and bishops' xray attacks.  */
static void
init_xrays(void)
{
	int sq;
	int file;
	int rank;
	int x;
	U64 tmp1;
	U64 tmp2;

	const U64 file_a = 0x00000000000000FF;
	const U64 rank_1 = 0x0101010101010101;
	const U64 diag_1 = 0x8040201008040201;
	const U64 diag_2 = 0x0102040810204080;
	
	for (sq = 0; sq < 64; sq++) {
		int i;
		file = SQ_FILE(sq);
		rank = SQ_RANK(sq);

		/* rook x-rays */
		rook_xray[sq] = (rank_1 << (unsigned)file) |
		                (file_a << (unsigned)(rank * 8));

		/* bishop x-rays */
		tmp1 = diag_1;
		tmp2 = diag_2;
		x = file - rank;
		if (x > 0) {
			for (i = 7; (int)i > 7 - x; i--)
				tmp1 &= ~(rank_1 << (unsigned)i);
			tmp1 = tmp1 << (unsigned)x;
		} else if (-x > 0) {
			for (i = 0; i < -x; i++)
				tmp1 &= ~(rank_1 << (unsigned)i);
			tmp1 = tmp1 >> (unsigned)-x;
		}
		x = rank - (7 - file);
		if (x > 0) {
			for (i = 7; (int)i > 7 - x; i--)
				tmp2 &= ~(rank_1 << (unsigned)i);
			tmp2 = tmp2 << (unsigned)x;
		} else if (-x > 0) {
			for (i = 0; i < -x; i++)
				tmp2 &= ~(rank_1 << (unsigned)i);
			tmp2 = tmp2 >> (unsigned)-x;
		}
		bishop_xray[sq] = tmp1 | tmp2;
	}
}

/* Initialize bitmasks for knight moves.  */
static void
init_knight_moves(void)
{
	int sq;
	int to[8];
	U64 mask;

	for (sq = 0; sq < 64; sq++) {
		int j;
		mask = 0;
		to[0] = sq + 10;  /* 1 down, 2 right */
		to[1] = sq + 6;   /* 1 down, 2 left  */
		to[2] = sq + 17;  /* 2 down, 1 right */
		to[3] = sq + 15;  /* 2 down, 1 left  */
		to[4] = sq - 10;  /* 1 up, 2 left    */
		to[5] = sq - 6;   /* 1 up, 2 right   */
		to[6] = sq - 17;  /* 2 up, 1 left    */
		to[7] = sq - 15;  /* 2 up, 1 right   */

		for (j = 0; j < 8; j++) {
			if (is_on_board(to[j])
			&&  abs(SQ_FILE(sq) - SQ_FILE(to[j])) <= 2)
				mask |= bit64[to[j]];
		}
		move_masks.knight[sq] = mask;
	}
}

/* Initialize bitmasks for king moves.  */
static void
init_king_moves(void)
{
	int sq;
	int to[8];
	U64 mask;

	for (sq = 0; sq < 64; sq++) {
		int j;
		mask = 0;
		to[0] = sq + 8;   /* up         */
		to[1] = sq - 8;   /* down       */
		to[2] = sq + 9;   /* up-left    */
		to[3] = sq - 9;   /* down-right */
		to[4] = sq + 1;   /* left       */
		to[5] = sq - 1;   /* right      */
		to[6] = sq + 7;   /* up-right   */
		to[7] = sq - 7;   /* down-left  */

		for (j = 0; j < 8; j++) {
			if (is_on_board(to[j])
			&& abs(SQ_FILE(sq) - SQ_FILE(to[j])) <= 1)
				mask |= bit64[to[j]];
		}
		move_masks.king[sq] = mask;
	}
}

/* Initialize bitmasks for pawn captures.  */
static void
init_pawn_captures(void)
{
	const int sq_limit[2] = { H8, A1 };
	int color;
	int to[2];
	U64 mask;

	for (color = WHITE; color <= BLACK; color++) {
		int i;
		int sign = SIGN(color);
		for (i = 0; i < 64; i++) {
			mask = 0;
			to[0] = i - sign*7; /* forward-right/forward-left */
			to[1] = i - sign*9; /* forward-left/forward-right */

			if (sign*i > sign*sq_limit[color]) {
				int j;
				for (j = 0; j < 2; j++) {
					int from_file = SQ_FILE(i);
					int to_file = SQ_FILE(to[j]);
					if (to[j] < 0 || to[j] > 63
					||  abs(to_file - from_file) > 1)
						continue;
					mask |= bit64[to[j]];
				}
			}
			move_masks.pawn_capt[color][i] = mask;
		}
	}
}

/* Initialize the move generators and masks.  */
void
init_movegen(void)
{
	initmagicmoves();
	init_xrays();
	init_connect_and_pin_masks();
	init_pawn_captures();
	init_knight_moves();
	init_king_moves();
}

/* Returns true if the side to move is in check. This function is mainly
   needed only for debugging, because Sloppy encodes an IS_CHECK bit
   in the move structure.  */
bool
board_is_check(Board *board)
{
	int color;
	int king_sq;
	U64 *op_pcs;

	ASSERT(1, board != NULL);

	color = board->color;
	king_sq = board->king_sq[color];
	op_pcs = &board->pcs[!color][ALL];

	if ((move_masks.pawn_capt[color][king_sq] & op_pcs[PAWN])
	||  (move_masks.knight[king_sq] & op_pcs[KNIGHT])
	||  (B_MAGIC(king_sq, board->all_pcs) & op_pcs[BQ])
	||  (R_MAGIC(king_sq, board->all_pcs) & op_pcs[RQ]))
	//||  (move_masks.king[king_sq] & op_pcs[KING]))
		return true;
	return false;
}

/* Get a check threat mask, or a mask of squares where the opposing king
   can't move without being checked.  */
static U64
get_threat_mask(Board *board, int color)
{
	int sq;
	U64 mask;
	U64 attacks;
	U64 pcs;
	
	ASSERT(2, board != NULL);
	
	/* Create a mask of all pieces except for the opposing king. This
	   allows the sliding attacks to go through him.  */
	pcs = board->all_pcs ^ board->pcs[!color][KING];

	/* Pawn threats.  */
	attacks = (FWD_LEFT(board->pcs[color][PAWN], color) & FILE_A_G) |
	          (FWD_RIGHT(board->pcs[color][PAWN], color) & FILE_B_H);

	/* Knight threats.  */
	mask = board->pcs[color][KNIGHT];
	while (mask) {
		sq = pop_lsb(&mask);
		attacks |= move_masks.knight[sq];
	}
	/* Bishop (and queen) threats.  */
	mask = board->pcs[color][BQ];
	while (mask) {
		sq = pop_lsb(&mask);
		attacks |= B_MAGIC(sq, pcs);
	}
	/* Rook (and queen) threats.  */
	mask = board->pcs[color][RQ];
	while (mask) {
		sq = pop_lsb(&mask);
		attacks |= R_MAGIC(sq, pcs);
	}
	/* King threats.  */
	attacks |= move_masks.king[board->king_sq[color]];
	
	return attacks;
}

/* How Sloppy knows if a move is a check:
   1. Create a rook+bishop checkmask of attacks against the opposing king.
   2. For non-sliding pieces it's trivial to detect a direct check.
      Enpassant moves and promotions are an annoying exception though,
      but since they're somewhat rare it's not a problem.
   2. For sliders, if the <to> square is in the checkmask the move is a check.

   Discovered checks:
   1. Queens can't give discovered checks.
   2. Create a pinmask for the opponent's king where the pinned pieces
      are the attackers who may give the discovered check.
   3. If a move's <from> square is in the pinmask and the <to> square is
      not in pin_mask[king_sq][from], the move is a check.

   This detection shouldn't consume too many CPU cycles:
   -  Direct checks need only 2 simple masks and 1 AND per piece.
   -  Discovered checks need the pinmask and a couple of ANDs.  */

/* Returns true if <move> checks the opposing king.  */
static bool
move_is_check(Board *board, U32 move, MoveData *md)
{
	int color;
	int pc;
	int from;
	int to;
	int king_sq;
	
	ASSERT(2, board != NULL);
	ASSERT(2, move != NULLMOVE);
	ASSERT(2, md != NULL);

	color = board->color;
	pc = GET_PC(move);
	from = GET_FROM(move);
	to = GET_TO(move);
	king_sq = board->king_sq[!color];
	
	switch (pc) {
	case PAWN:
		/* Direct check.  */
		if (!GET_PROM(move)
		&& (move_masks.pawn_capt[color][to] & bit64[king_sq]))
			return true;
		/* Discovered check.  */
		if ((bit64[from] & md->discov_chk)
		&& !(bit64[to] & pin_mask[king_sq][from]))
			return true;
		/* Discovered check by enpassant move.  */
		if (GET_EPSQ(move)) {
			int ep = GET_EPSQ(move);
			U64 all_pcs = board->all_pcs;
			all_pcs ^= bit64[ep] | bit64[from] | bit64[to];
			if (B_MAGIC(king_sq, all_pcs) & board->pcs[color][BQ])
				return true;
			if (R_MAGIC(king_sq, all_pcs) & board->pcs[color][RQ])
				return true;
		/* Direct check by promotion.  */
		} else if (GET_PROM(move)) {
			U64 all_pcs = board->all_pcs ^ bit64[from];
			switch (GET_PROM(move)) {
			case KNIGHT:
				if (move_masks.knight[to] & bit64[king_sq])
					return true;
				break;
			case BISHOP:
				if (B_MAGIC(to, all_pcs) & bit64[king_sq])
					return true;
				break;
			case ROOK:
				if (R_MAGIC(to, all_pcs) & bit64[king_sq])
					return true;
				break;
			case QUEEN:
				if (B_MAGIC(to, all_pcs) & bit64[king_sq])
					return true;
				if (R_MAGIC(to, all_pcs) & bit64[king_sq])
					return true;
				break;
			}
		}
		break;
	case KNIGHT:
		/* Direct check.  */
		if (move_masks.knight[to] & bit64[king_sq])
			return true;
		/* Discovered check.  */
		if (bit64[from] & md->discov_chk)
			return true;
		break;
	case BISHOP:
		/* Direct check.  */
		if (md->b_chk & bit64[to])
			return true;
		/* Discovered check.  */
		if (bit64[from] & md->discov_chk)
			return true;
		break;
	case ROOK:
		/* Direct check.  */
		if (md->r_chk & bit64[to])
			return true;
		/* Discovered check.  */
		if (bit64[from] & md->discov_chk)
			return true;
		break;
	case QUEEN:
		/* Direct check.  */
		if ((md->b_chk | md->r_chk) & bit64[to])
			return true;
		break;
	case KING:
		/* Discovered check.  */
		if ((bit64[from] & md->discov_chk)
		&& !(bit64[to] & pin_mask[king_sq][from]))
			return true;
		/* Direct(?) check by a castling move.  */
		if (IS_CASTLING(move)) {
			int castle;
			int rook_sq;
			U64 all_pcs;
			
			castle = GET_CASTLE(move);
			rook_sq = castling.rook_sq[color][castle][C_TO];
			all_pcs = board->all_pcs ^ board->pcs[color][KING];
			if (R_MAGIC(rook_sq, all_pcs) & bit64[king_sq])
				return true;
		}
		break;
	default:
		fatal_error("move_is_check: invalid piece type");
		break;
	}
	
	return false;
}

/* Form a simple (incomplete) move from the piece type, from and to
   squares, and the promotion piece.  */
U32
simple_move(int pc, int from, int to, int prom)
{
	return SET_FROM(from) | SET_TO(to) | SET_PC(pc) | SET_PROM(prom);
}

/* Form a new move and add it to a move list.  */
#define CHECK_BIT 04000000000
static void
add_move(Board *board, MoveData *md, MoveLst *move_list)
{
	const unsigned castle_bits[2] = { BIT(27U), BIT(28U) | BIT(27U) };
	int pc;
	int capt;
	U32 move;
	
	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);
	ASSERT(2, move_list->nmoves < MAX_NMOVES);
	ASSERT(2, is_on_board(md->from));
	ASSERT(2, is_on_board(md->to));
	ASSERT(2, !md->prom || (md->prom >= KNIGHT && md->prom <= QUEEN));

	pc = board->mailbox[md->from];
	if (md->ep_sq != 0)
		capt = PAWN;
	else
		capt = board->mailbox[md->to];
	move = SET_FROM(md->from) | SET_TO(md->to) | SET_PC(pc) |
	       SET_CAPT(capt) | SET_PROM(md->prom) | SET_EPSQ(md->ep_sq);
	if (md->castle != -1)
		move |= castle_bits[md->castle];
	if (move_is_check(board, move, md))
		move |= CHECK_BIT;
	
	move_list->move[(move_list->nmoves)++] = move;
}

/* Make sure a pawn capture is legal, then call add_move() to add it to the
   move list. If the capture is also a promotion then all the possible
   promotions will be added. */
static void
add_pawn_capt(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int king_sq;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	king_sq = board->king_sq[color];

	if ((bit64[md->from] & md->pins)
	&& !(bit64[md->to] & pin_mask[king_sq][md->from]))
		return;

	if (md->to && board->posp->ep_sq == md->to) {
		md->ep_sq = md->to + SIGN(color)*8;
		if (SQ_RANK(md->from) == SQ_RANK(king_sq)) {
			U64 all_pcs = board->all_pcs;
			all_pcs ^=  bit64[md->ep_sq] | bit64[md->from];
			if (R_MAGIC(king_sq, all_pcs) & board->pcs[!color][RQ])
				return;
		}
	} else
		md->ep_sq = 0;

	if (bit64[md->from] & seventh_rank[color]) {
		for (md->prom = QUEEN; md->prom >= KNIGHT; md->prom--)
			add_move(board, md, move_list);
	} else {
		md->prom = 0;
		add_move(board, md, move_list);
	}
}

/* Generate pawn captures.  */
static void
gen_pawn_capts(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int side;	/* capture side, 0 = left, 1 = right */
	int ep_sq;
	U64 target;
	static const int direction[2][2] = { {9, 7}, {-7, -9} };

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	ep_sq = board->posp->ep_sq;
	target = board->pcs[!color][ALL] & md->target;
	
	md->castle = -1;
	
	/* Enpassant capture is possible.  */
	if (ep_sq && (bit64[ep_sq + SIGN(color)*8] & target))
		target |= bit64[ep_sq];

	for (side = 0; side < 2; side++) {
		U64 mask = target;
		U64 my_pawns = board->pcs[color][PAWN];

		if (side == 0)
			mask &= FWD_LEFT(my_pawns, color) & FILE_A_G;
		else
			mask &= FWD_RIGHT(my_pawns, color) & FILE_B_H;

		while (mask) {
			md->to = pop_lsb(&mask);
			md->from = md->to + direction[color][side];
			add_pawn_capt(board, md, move_list);
		}
	}
}

/* Make sure a pawn move is legal, then call add_move() to add it to the
   move list. If the move is a promotion then all the possible
   promotions will be added. */
static void
add_pawn_move(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int king_sq;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	king_sq = board->king_sq[color];

	if ((bit64[md->from] & md->pins)
	&& !(bit64[md->to] & pin_mask[king_sq][md->from]))
		return;

	if (bit64[md->from] & seventh_rank[color]) {
		for (md->prom = QUEEN; md->prom >= KNIGHT; md->prom--)
			add_move(board, md, move_list);
	} else {
		md->prom = 0;
		add_move(board, md, move_list);
	}
}

/* Generate pawn moves (non-captures).  */
static void
gen_pawn_moves(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int sign;
	U64 target1;	/* 1 square forward target */
	U64 target2;	/* 2 squares forward target */
	static const U64 rank_4[] = { 0x000000FF00000000, 0x00000000FF000000 };

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	sign = SIGN(color);
	md->ep_sq = 0;
	md->castle = -1;

	/* 1 square forward.  */
	target1 = FWD(board->pcs[color][PAWN], color) & ~board->all_pcs;
	/* 2 squares forward.  */
	target2 = FWD(target1, color) & (~board->all_pcs & rank_4[color]);

	target1 &= md->target;
	target2 &= md->target;

	while (target1) {
		md->to = pop_lsb(&target1);
		md->from = md->to + sign*8;
		add_pawn_move(board, md, move_list);
	}
	while (target2) {
		md->to = pop_lsb(&target2);
		md->from = md->to + sign*16;
		add_pawn_move(board, md, move_list);
	}
}

/* Generate knight moves.  */
static void
gen_knight_moves(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	U64 mask;
	U64 target;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);
	
	color = board->color;
	md->prom = 0;
	md->ep_sq = 0;
	md->castle = -1;
	target = ~board->pcs[color][ALL] & md->target;
	mask = board->pcs[color][KNIGHT] & ~md->pins;

	while (mask) {
		U64 attacks;

		md->from = pop_lsb(&mask);
		attacks = (move_masks.knight[md->from] & target);
		while (attacks) {
			md->to = pop_lsb(&attacks);
			add_move(board, md, move_list);
		}
	}
}

/* Generate bishop moves.  */
static void
gen_bishop_moves(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int king_sq;
	U64 mask;
	U64 target;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);
	
	color = board->color;
	md->prom = 0;
	md->ep_sq = 0;
	md->castle = -1;
	king_sq = board->king_sq[color];
	mask = board->pcs[color][BQ];

	target = ~board->pcs[color][ALL] & md->target;
	while (mask) {
		U64 attacks;

		md->from = pop_lsb(&mask);
		attacks = B_MAGIC(md->from, board->all_pcs) & target;
		if (bit64[md->from] & md->pins)
			attacks &= pin_mask[king_sq][md->from];
		while (attacks) {
			md->to = pop_lsb(&attacks);
			add_move(board, md, move_list);
		}
	}
}

/* Generate rook moves.  */
static void
gen_rook_moves(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int king_sq;
	U64 mask;
	U64 target;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	md->prom = 0;
	md->ep_sq = 0;
	md->castle = -1;
	king_sq = board->king_sq[color];
	mask = board->pcs[color][RQ];

	target = ~board->pcs[color][ALL] & md->target;
	while (mask) {
		U64 attacks;

		md->from = pop_lsb(&mask);
		attacks = R_MAGIC(md->from, board->all_pcs) & target;
		if (bit64[md->from] & md->pins)
			attacks &= pin_mask[king_sq][md->from];
		while (attacks) {
			md->to = pop_lsb(&attacks);
			add_move(board, md, move_list);
		}
	}
}

/* Generate king captures.  */
static void
gen_king_capts(Board *board, MoveData *md, MoveLst *move_list)
{
	U64 attacks;
	U64 target;

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	md->prom = 0;
	md->ep_sq = 0;
	md->castle = -1;
	target = ~get_threat_mask(board, !board->color) & md->target;

	md->from = board->king_sq[board->color];

	attacks = move_masks.king[md->from] & target;
	while (attacks) {
		md->to = pop_lsb(&attacks);
		add_move(board, md, move_list);
	}
}

/* Generate king moves (non-captures).  */
static void
gen_king_moves(Board *board, MoveData *md, MoveLst *move_list)
{
	int color;
	int i;
	U64 attacks;
	U64 threats;
	U64 target;
	
	/* A mask of squares that mustn't be checked by
	   the opponent when castling.  */
	static const U64 castle_check_mask[2][2] =
	{
		{ 0x7000000000000000, 0x1C00000000000000 },
		{ 0x0000000000000070, 0x000000000000001C }
	};
	/* A mask of squares that have to be empty for a castling move
	   to be legal.  */
	static const U64 castle_empty_mask[2][2] = {
		{ 0x6000000000000000, 0x0e00000000000000 },
		{ 0x0000000000000060, 0x000000000000000e }
	};

	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	md->prom = 0;
	md->ep_sq = 0;
	md->castle = -1;
	md->from = board->king_sq[color];
	threats = get_threat_mask(board, !color);
	target = ~threats & md->target;

	/* Normal moves (not castling moves).  */
	attacks = move_masks.king[md->from] & target;
	while (attacks) {
		md->to = pop_lsb(&attacks);
		add_move(board, md, move_list);
	}

	/* Castling moves

	   If the king is not in square E1 (white) or E8 (black), if it
	   doesn't have any castling rights left, or if the king is in
	   check, castling isn't possible.  */
	if (bit64[md->from] & threats
	|| md->from != castling.king_sq[color][C_KSIDE][C_FROM]
	|| !(board->posp->castle_rights & castling.all_rights[color]))
		return;

	for (i = 0; i < 2; i++) {
		int rook_sq = castling.rook_sq[color][i][C_FROM];
		md->to = castling.king_sq[color][i][C_TO];
		if ((board->posp->castle_rights & castling.rights[color][i])
		&& !(board->all_pcs & castle_empty_mask[color][i])
		&& !(threats & castle_check_mask[color][i])
		&& (bit64[md->to] & target)
		&& (board->pcs[color][ROOK] & bit64[rook_sq])) {
			md->castle = i;
			add_move(board, md, move_list);
		}
	}
}

/* Get a mask of <pinned_color>'s pieces that are pinned to <color>'s king.
   <color> and <pinned_color> may or may not be the same. If they're not, then
   the pieces aren't really pinned, but they can give a discovered check.  */
static U64
get_pins(Board *board, int color, int pinned_color)
{
	int king_sq;
	U64 pinners;
	
	ASSERT(2, board != NULL);
	
	king_sq = board->king_sq[color];
	pinners = (bishop_xray[king_sq] & board->pcs[!color][BQ]) |
	          (rook_xray[king_sq] & board->pcs[!color][RQ]);

	if (pinners) {
		U64 pins = 0;
		U64 b1 = board->all_pcs;
		U64 b3;
		
		U64 b2 = B_MAGIC(king_sq, b1);
		pinners &= ~b2;
		b2 &= board->pcs[pinned_color][ALL];
		while (b2) {
			b3 = b2 & -b2;
			b2 ^= b3;
			if (B_MAGIC(king_sq, b1 ^ b3) & pinners) pins |= b3;
		}

		b2 = R_MAGIC(king_sq, b1);
		pinners &= ~b2;
		b2 &= board->pcs[pinned_color][ALL];
		while (b2) {
			b3 = b2 & -b2;
			b2 ^= b3;
			if (R_MAGIC(king_sq, b1 ^ b3) & pinners) pins |= b3;
		}
		
		return pins;
	}
	
	return 0;
}

/* Get a mask of opponent's pieces that check the king, and all
   the other squares where pieces (not the king) can move to
   evade the check.  */
static U64
get_check_mask(Board *board)
{
	int king_sq;
	U64 *op_pcs;
	U64 sliders;
	U64 check_mask;

	ASSERT(2, board != NULL);

	king_sq = board->king_sq[board->color];
	op_pcs = &board->pcs[!board->color][ALL];

	check_mask =
		(move_masks.pawn_capt[board->color][king_sq] & op_pcs[PAWN]) |
		(move_masks.knight[king_sq] & op_pcs[KNIGHT]);
	sliders =
		(B_MAGIC(king_sq, board->all_pcs) & op_pcs[BQ]) |
		(R_MAGIC(king_sq, board->all_pcs) & op_pcs[RQ]);

	if (check_mask == 0 && sliders == 0)
		return 0;

	while (sliders)
		check_mask |= connect_mask[king_sq][pop_lsb(&sliders)];

	return check_mask;
}

/* Generate some bitmasks for move generation.  */
static void
gen_movegen_masks(Board *board, MoveData *md)
{
	int color;
	int king_sq;
	
	ASSERT(2, board != NULL);
	ASSERT(2, md != NULL);
	
	color = board->color;
	king_sq = board->king_sq[!color];
	
	md->b_chk = B_MAGIC(king_sq, board->all_pcs);
	md->r_chk = R_MAGIC(king_sq, board->all_pcs);
	md->pins = get_pins(board, color, color);
	md->discov_chk = get_pins(board, !color, color);
}

/* Generate moves that are played in the quiescence search.  */
void
gen_qs_moves(Board *board, MoveLst *move_list)
{
	int color;
	MoveData md;

	ASSERT(2, board != NULL);
	ASSERT(2, move_list != NULL);
	ASSERT(2, !board->posp->in_check);
	
	color = board->color;
	move_list->nmoves = 0;

	md.target = board->pcs[!color][ALL];
	gen_movegen_masks(board, &md);

	gen_king_capts(board, &md, move_list);
	gen_pawn_capts(board, &md, move_list);
	gen_knight_moves(board, &md, move_list);
	gen_bishop_moves(board, &md, move_list);
	gen_rook_moves(board, &md, move_list);
}

/* Generate all legal moves.  */
void
gen_moves(Board *board, MoveLst *move_list)
{
	int color;
	MoveData md;

	ASSERT(2, board != NULL);
	ASSERT(2, move_list != NULL);

	color = board->color;
	move_list->nmoves = 0;

	md.target = ~board->pcs[color][ALL];
	gen_movegen_masks(board, &md);

	gen_king_moves(board, &md, move_list);
	if (board->posp->in_check) {
		U64 check_mask = get_check_mask(board);
		ASSERT(2, check_mask != 0);
		ASSERT(2, board_is_check(board));
		/* In a double check we only generate king moves
		   because everything else is illegal.  */
		if (popcount(check_mask & board->pcs[!color][ALL]) > 1)
			return;
		md.target &= check_mask;
	}
	
	gen_pawn_moves(board, &md, move_list);
	gen_pawn_capts(board, &md, move_list);
	gen_knight_moves(board, &md, move_list);
	gen_bishop_moves(board, &md, move_list);
	gen_rook_moves(board, &md, move_list);
}

/* Generate moves for a specific piece type with a specific <to> square.  */
void
gen_pc_moves(Board *board, MoveLst *move_list, int pc, int to)
{
	int color;
	MoveData md;
	U64 tmp_mask;

	ASSERT(2, board != NULL);
	ASSERT(2, move_list != NULL);
	ASSERT(2, pc >= PAWN && pc <= KING);
	ASSERT(2, is_on_board(to));

	color = board->color;
	move_list->nmoves = 0;

	if (pc == PAWN && to != 0 && to == board->posp->ep_sq)
		to += SIGN(color)*8;

	md.target = ~board->pcs[color][ALL] & bit64[to];
	gen_movegen_masks(board, &md);

	if (pc == KING) {
		gen_king_moves(board, &md, move_list);
		return;
	}

	if (board->posp->in_check) {
		U64 check_mask = get_check_mask(board);
		ASSERT(2, check_mask != 0);
		ASSERT(2, board_is_check(board));

		/* In a double check we return without any moves.  */
		if (popcount(check_mask & board->pcs[!color][ALL]) > 1)
			return;
		md.target &= check_mask;
	}

	switch (pc) {
	case PAWN:
		gen_pawn_moves(board, &md, move_list);
		gen_pawn_capts(board, &md, move_list);
		break;
	case KNIGHT:
		gen_knight_moves(board, &md, move_list);
		break;
	case BISHOP:
		tmp_mask = board->pcs[color][BQ];
		board->pcs[color][BQ] = board->pcs[color][BISHOP];
		gen_bishop_moves(board, &md, move_list);
		board->pcs[color][BQ] = tmp_mask;
		break;
	case ROOK:
		tmp_mask = board->pcs[color][RQ];
		board->pcs[color][RQ] = board->pcs[color][ROOK];
		gen_rook_moves(board, &md, move_list);
		board->pcs[color][RQ] = tmp_mask;
		break;
	case QUEEN:
		tmp_mask = board->pcs[color][BQ];
		board->pcs[color][BQ] = board->pcs[color][QUEEN];
		gen_bishop_moves(board, &md, move_list);
		board->pcs[color][BQ] = tmp_mask;

		tmp_mask = board->pcs[color][RQ];
		board->pcs[color][RQ] = board->pcs[color][QUEEN];
		gen_rook_moves(board, &md, move_list);
		board->pcs[color][RQ] = tmp_mask;
		break;
	default:
		fatal_error("gen_pc_moves: invalid piece type");
		break;
	}
}

