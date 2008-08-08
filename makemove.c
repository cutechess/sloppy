/* Sloppy - makemove.c
   Functions for making and unmaking moves.

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
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "eval.h"
#include "hash.h"
#include "makemove.h"


/* Returns true if the position before the move can be later reached again.  */
static bool
is_reversible(U32 move)
{
	if (GET_PC(move) == PAWN || GET_CAPT(move) || IS_CASTLING(move))
		return false;
	return true;
}

static void
make_pawn_move(Board *board, U32 move)
{
	int color;
	int from;
	int to;
	int ep_sq;
	int prom;
	U64 *key;
	U64 *pawn_key;

	ASSERT(2, board != NULL);

	color = board->color;
	key = &board->posp->key;
	pawn_key = &board->posp->pawn_key;
	from = GET_FROM(move);
	to = GET_TO(move);
	prom = GET_PROM(move);
	ASSERT(2, !prom || (prom >= KNIGHT && prom <= QUEEN));
	ep_sq = GET_EPSQ(move);
	ASSERT(2, is_on_board(ep_sq));

	board->pcs[color][PAWN] ^= bit64[from];
	*pawn_key ^= zobrist.pc[color][PAWN][from];
	if (prom != 0) {
		board->mailbox[to] = prom;
		board->pcs[color][prom] ^= bit64[to];
		board->material[color] += pc_val[prom];
		board->phase -= phase_val[prom];
		*key ^= zobrist.pc[color][prom][to];
	} else {
		board->mailbox[to] = PAWN;
		board->pcs[color][PAWN] ^= bit64[to];
		*key ^= zobrist.pc[color][PAWN][to];
		*pawn_key ^= zobrist.pc[color][PAWN][to];
	}

	/* It's an enpassant move.  */
	if (ep_sq) {
		board->mailbox[ep_sq] = 0;
		board->pcs[!color][PAWN] ^= bit64[ep_sq];
		board->pcs[!color][ALL] ^= bit64[ep_sq];
		*key ^= zobrist.pc[!color][PAWN][ep_sq];
		*pawn_key ^= zobrist.pc[!color][PAWN][ep_sq];
	}

	/* The pawn is pushed two squares ahead, which means that the
	   opponent can make an enpassant capture on his turn.  */
	if (SIGN(color)*(to - from) == -16) {
		ep_sq = to + SIGN(color)*8;
		board->posp->ep_sq = ep_sq;
		*key ^= zobrist.enpassant[ep_sq];
	}
}

static void
make_rook_move(Board *board, U32 move)
{
	int color;
	int from;
	unsigned *castle_rights;
	U64 *key;

	ASSERT(2, board != NULL);
	
	color = board->color;
	key = &board->posp->key;
	from = GET_FROM(move);
	castle_rights = &board->posp->castle_rights;
	
	/* Make sure the player loses his castling rights in the corner
	   the rook moves from, if any.  */
	if (from == castling.rook_sq[color][C_KSIDE][C_FROM]
	&& *castle_rights & castling.rights[color][C_KSIDE]) {
		*key ^= zobrist.castle[color][C_KSIDE];
		*castle_rights &= ~castling.rights[color][C_KSIDE];
	} else if (from == castling.rook_sq[color][C_QSIDE][C_FROM]
	&& *castle_rights & castling.rights[color][C_QSIDE]) {
		*key ^= zobrist.castle[color][C_QSIDE];
		*castle_rights &= ~castling.rights[color][C_QSIDE];
	}
}

static void
make_king_move(Board *board, U32 move)
{
	int color;
	unsigned *castle_rights;
	U64 *key;

	ASSERT(2, board != NULL);

	color = board->color;
	key = &board->posp->key;
	castle_rights = &board->posp->castle_rights;

	/* Take away the castling rights.  */
	if (*castle_rights & castling.all_rights[color]) {
		if (*castle_rights & castling.rights[color][C_KSIDE])
			*key ^= zobrist.castle[color][C_KSIDE];
		if (*castle_rights & castling.rights[color][C_QSIDE])
			*key ^= zobrist.castle[color][C_QSIDE];
		*castle_rights &= ~castling.all_rights[color];
	}
	board->king_sq[color] = GET_TO(move);

	if (IS_CASTLING(move)) {
		int castle;
		int rook_from;
		int rook_to;
		U64 rook_mask;
		
		castle = GET_CASTLE(move);
		ASSERT(2, castle == C_KSIDE || castle == C_QSIDE);
		
		rook_from = castling.rook_sq[color][castle][C_FROM];
		rook_to = castling.rook_sq[color][castle][C_TO];
		rook_mask = bit64[rook_from] | bit64[rook_to];

		board->mailbox[rook_from] = 0;
		board->mailbox[rook_to] = ROOK;
		board->pcs[color][ROOK] ^= rook_mask;
		board->pcs[color][ALL] ^= rook_mask;
		*key ^= zobrist.pc[color][ROOK][rook_from];
		*key ^= zobrist.pc[color][ROOK][rook_to];
	}
}

void
make_move(Board *board, U32 move)
{
	int color;
	int from;
	int to;
	int pc;
	int capt;

	U64 *my_pcs;
	U64 *op_pcs;
	U64 from_to_mask;
	U64 *key;
	U64 *pawn_key;
	PosInfo *pos;

	ASSERT(2, board != NULL);
	ASSERT(2, move != NULLMOVE);

	color = board->color;
	from = GET_FROM(move);
	ASSERT(2, from >= 0 && from <= 63);
	to = GET_TO(move);
	ASSERT(2, to >= 0 && to <= 63);
	pc = GET_PC(move);
	ASSERT(2, pc >= PAWN && pc <= KING);
	capt = GET_CAPT(move);
	ASSERT(2, !capt || (capt >= PAWN && capt <= QUEEN));

	my_pcs = &board->pcs[color][ALL];
	op_pcs = &board->pcs[!color][ALL];
	from_to_mask = bit64[from] | bit64[to];

	/* Initialize the new position.  */
	(board->posp)++;
	pos = board->posp;
	*pos = *(pos - 1);
	key = &pos->key;
	pawn_key = &pos->pawn_key;
	pos->move = move;
	
	if (IS_CHECK(move))
		pos->in_check = true;
	else
		pos->in_check = false;
	if (is_reversible(move))
		pos->fifty = (pos - 1)->fifty + 1;
	else
		pos->fifty = 0;

	*key ^= zobrist.pc[color][pc][from];
	
	/* En passant capture at pos->ep_sq isn't possible anymore.  */
	if (pos->ep_sq) {
		*key ^= zobrist.enpassant[pos->ep_sq];
		pos->ep_sq = 0;
	}

	if (pc == PAWN)
		make_pawn_move(board, move);
	else {
		board->mailbox[to] = pc;
		my_pcs[pc] ^= from_to_mask;
		if (pc == KING)
			make_king_move(board, move);
		else if (pc == ROOK)
			make_rook_move(board, move);
		*key ^= zobrist.pc[color][pc][to];
	}
	*my_pcs ^= from_to_mask;
	board->mailbox[from] = 0;

	/* En passant captures are handled by make_pawn_move().
	   This is for all other captures.  */
	if (capt && !GET_EPSQ(move)) {
		op_pcs[capt] ^= bit64[to];
		*op_pcs ^= bit64[to];
		if (capt != PAWN) {
			board->material[!color] -= pc_val[capt];
			board->phase += phase_val[capt];
		} else
			*pawn_key ^= zobrist.pc[!color][PAWN][to];
		*key ^= zobrist.pc[!color][capt][to];
		op_pcs[BQ] = op_pcs[BISHOP] | op_pcs[QUEEN];
		op_pcs[RQ] = op_pcs[ROOK] | op_pcs[QUEEN];
	}
	
	my_pcs[BQ] = my_pcs[BISHOP] | my_pcs[QUEEN];
	my_pcs[RQ] = my_pcs[ROOK] | my_pcs[QUEEN];
	board->all_pcs = *my_pcs | *op_pcs;
	*key ^= zobrist.color;

	board->color = !color;
	(board->nmoves)++;
	ASSERT(2, board->nmoves <= MAX_NMOVES_PER_GAME);
	ASSERT(3, board_is_ok(board));
}

void
make_nullmove(Board *board)
{
	PosInfo *pos;

	ASSERT(2, board != NULL);
	ASSERT(2, !board->posp->in_check);

	(board->posp)++;
	pos = board->posp;
	*pos = *(pos - 1);
	pos->move = NULLMOVE;

	/* Ignore repetition draws after a nullmove.  */
	pos->fifty = 0;

	pos->key ^= zobrist.color;
	board->color = !board->color;
	if (pos->ep_sq) {
		pos->key ^= zobrist.enpassant[pos->ep_sq];
		pos->ep_sq = 0;
	}

	(board->nmoves)++;
	ASSERT(2, board->nmoves <= MAX_NMOVES_PER_GAME);
}

static void
undo_pawn_move(Board *board, U32 move)
{
	int color;
	int from;
	int to;
	int ep_sq;
	int prom;

	ASSERT(2, board != NULL);

	color = !board->color;
	from = GET_FROM(move);
	to = GET_TO(move);
	prom = GET_PROM(move);
	ASSERT(2, !prom || (prom >= KNIGHT && prom <= QUEEN));
	ep_sq = GET_EPSQ(move);
	ASSERT(2, ep_sq >= 0 && ep_sq <= 63);

	board->pcs[color][PAWN] ^= bit64[from];
	if (prom) {
		board->pcs[color][prom] ^= bit64[to];
		board->material[color] -= pc_val[prom];
		board->phase += phase_val[prom];
	} else
		board->pcs[color][PAWN] ^= bit64[to];

	if (ep_sq) {
		board->mailbox[ep_sq] = PAWN;
		board->pcs[!color][PAWN] ^= bit64[ep_sq];
		board->pcs[!color][ALL] ^= bit64[ep_sq];
	}
}

static void
undo_king_move(Board *board, U32 move)
{
	int color;
	
	ASSERT(2, board != NULL);

	color = !board->color;
	board->king_sq[color] = GET_FROM(move);

	if (IS_CASTLING(move)) {
		int castle;
		int rook_from;
		int rook_to;
		U64 rook_mask;

		castle = GET_CASTLE(move);
		ASSERT(2, castle == C_KSIDE || castle == C_QSIDE);

		rook_from = castling.rook_sq[color][castle][C_FROM];
		rook_to = castling.rook_sq[color][castle][C_TO];
		rook_mask = bit64[rook_from] | bit64[rook_to];

		board->mailbox[rook_to] = 0;
		board->mailbox[rook_from] = ROOK;
		board->pcs[color][ROOK] ^= rook_mask;
		board->pcs[color][ALL] ^= rook_mask;
	}
}

void
undo_move(Board *board)
{
	int color;
	int from;
	int to;
	int pc;
	int capt;
	U32 move;
	U64 *my_pcs;
	U64 *op_pcs;
	U64 mask;

	ASSERT(2, board != NULL);

	move = board->posp->move;
	color = !board->color;
	from = GET_FROM(move);
	ASSERT(2, from >= 0 && from <= 63);
	to = GET_TO(move);
	ASSERT(2, to >= 0 && to <= 63);
	pc = GET_PC(move);
	ASSERT(2, pc >= PAWN && pc <= KING);
	capt = GET_CAPT(move);
	ASSERT(2, !capt || (capt >= PAWN && capt <= QUEEN));

	my_pcs = &board->pcs[color][ALL];
	op_pcs = &board->pcs[!color][ALL];
	mask = bit64[from] | bit64[to];

	if (pc == PAWN)
		undo_pawn_move(board, move);
	else {
		my_pcs[pc] ^= mask;
		if (pc == KING)
			undo_king_move(board, move);
	}
	*my_pcs ^= mask;

	board->mailbox[from] = pc;

	if (capt && !GET_EPSQ(move)) {
		board->mailbox[to] = capt;
		op_pcs[capt] ^= bit64[to];
		*op_pcs ^= bit64[to];
		if (capt != PAWN) {
			board->material[!color] += pc_val[capt];
			board->phase -= phase_val[capt];
		}
		op_pcs[BQ] = op_pcs[BISHOP] | op_pcs[QUEEN];
		op_pcs[RQ] = op_pcs[ROOK] | op_pcs[QUEEN];
	} else
		board->mailbox[to] = 0;

	my_pcs[BQ] = my_pcs[BISHOP] | my_pcs[QUEEN];
	my_pcs[RQ] = my_pcs[ROOK] | my_pcs[QUEEN];
	board->all_pcs = *my_pcs | *op_pcs;

	board->color = color;

	(board->posp)--;
	(board->nmoves)--;

	ASSERT(2, board->nmoves >= 0);
	ASSERT(3, board_is_ok(board));
}

void
undo_nullmove(Board *board)
{
	ASSERT(2, board != NULL);
	ASSERT(2, !board->posp->in_check);

	board->color = !board->color;

	(board->posp)--;
	(board->nmoves)--;

	ASSERT(2, board->nmoves >= 0);
}

/* Returns the number of times the current position has been reached
   in the game.  */
int
get_nrepeats(Board *board, int max_repeats)
{
	int i;
	int nrepeats;

	ASSERT(2, board != NULL);
	ASSERT(2, board->nmoves >= board->posp->fifty);
	ASSERT(2, board->posp->fifty >= 0);

	/* If the num. of reversible moves in a row is less than 4, then
	   there's no way we could already have a repetition.  */
	if (board->posp->fifty < 4)
		return 0;

	nrepeats = 0;
	for (i = 1; i <= board->posp->fifty; i++) {
		if ((board->posp - i)->key == board->posp->key) {
			nrepeats++;
			if (nrepeats >= max_repeats)
				return nrepeats;
		}
	}

	return nrepeats;
}

