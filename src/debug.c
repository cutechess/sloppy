/* Sloppy - debug.c
   Functions that are mainly used in debug mode (DEBUG_LEVEL > 0).

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
#include "util.h"
#include "movegen.h"
#include "notation.h"
#include "eval.h"
#include "debug.h"


#if DEBUG_LEVEL > 0

/* Return true if <val> is a valid score or evaluation.  */
bool
val_is_ok(int val)
{
	return (val >= -VAL_INF && val <= VAL_INF);
}

/* Returns true if <board> isn't corrupted.
   This function isn't complete, but it's usually enough to see if
   there's something wrong with bitmasks, makemove, movegen, etc.  */
bool
board_is_ok(const Board *board)
{
	int sq;

	ASSERT(2, board != NULL);

	if (board->color != WHITE && board->color != BLACK)
		return false;
	
	if (!is_on_board(board->king_sq[WHITE])
	||  !is_on_board(board->king_sq[BLACK]))
		return false;

	for (sq = 0; sq < 64; sq++) {
		int pc;
		int nwhites = 0;
		int nblacks = 0;

		for (pc = ALL; pc <= KING; pc++) {
			if (board->pcs[WHITE][pc] & bit64[sq])
				nwhites++;
			if (board->pcs[BLACK][pc] & bit64[sq])
				nblacks++;
		}

		/* <sq> must either contain 0 or 2 (one in the ALL board,
		   one in the pc board) black or white pieces.  */
		if ((nwhites != 0 && nwhites != 2)
		||  (nblacks != 0 && nblacks != 2)
		||  (nblacks != 0 && nwhites != 0))
			return false;

		/* If there's a piece in <sq>, it must also be in the all_pcs
		   bitboard. Likewise, if <sq> is empty, the corresponding bit
		   of all_pcs must also be.  */
		if (nwhites || nblacks) {
			if (!(board->all_pcs & bit64[sq]))
				return false;
		} else if ((board->all_pcs & bit64[sq]))
			return false;
	}
	/* Both sides must have exactly one king, and it has to be in the
	   king_sq[color] square.  */
	if (!(board->pcs[WHITE][KING] & bit64[board->king_sq[WHITE]])
	||  !(board->pcs[BLACK][KING] & bit64[board->king_sq[BLACK]])
	||  popcount(board->pcs[WHITE][KING]) != 1
	||  popcount(board->pcs[BLACK][KING]) != 1)
		return false;

	return true;
}

/* Compare two chess boards to each other.
   Returns 0 if they have the same data.
   This function is really incomplete but also not that needed.  */
int
board_cmp(const Board *board1, const Board *board2)
{
	int pc;
	
	ASSERT(2, board1 != NULL);
	ASSERT(2, board2 != NULL);

	if (board1->posp->castle_rights != board2->posp->castle_rights)
		return 1;
	if (board1->posp->ep_sq != board2->posp->ep_sq)
		return 2;
	if (board1->posp->fifty != board2->posp->fifty)
		return 3;
	if (board1->all_pcs != board2->all_pcs)
		return 4;
	if (board1->posp->key != board2->posp->key)
		return 5;
	for (pc = ALL; pc <= KING; pc++) {
		if (board1->pcs[WHITE][pc] != board2->pcs[WHITE][pc])
			return 6;
		if (board1->pcs[BLACK][pc] != board2->pcs[BLACK][pc])
			return 7;
	}
	return 0;
}

/* Print a 64-bit unsigned integer in binary (bitmask) form.  */
void
print_bitmask_64(U64 mask)
{
	int i;
	
	for (i = 0; i < 64; i++) {
		if (SQ_FILE(i) == 0)
			printf("\n");
		if (mask & bit64[i])
			printf("1 ");
		else
			printf("0 ");
	}
	printf("\n");
}

/* Print an 8-bit unsigned integer in binary (bitmask) form.  */
void
print_bitmask_8(U8 mask)
{
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (mask & (1 << i))
			printf("1 ");
		else
			printf("0 ");
	}
	printf("\n");
}

#endif /* DEBUG_LEVEL > 0 */


/* Print detailed information about a move.  */
void
print_move_details(U32 move)
{
	char str_move[MAX_BUF];

	move_to_str(move, str_move);
	printf("Move: %s\n", str_move);
	printf("Moving piece: %c\n", get_pc_type_chr(GET_PC(move)));
	printf("Captured piece: %c\n", get_pc_type_chr(GET_CAPT(move)));
	printf("From square: %u\n", GET_FROM(move));
	printf("To square: %u\n", GET_TO(move));
	printf("Promotion: %c\n", get_pc_type_chr(GET_PROM(move)));
}

/* Print a list of legal moves.  */
void
print_moves(Board *board, bool san_notation)
{
	int i;
	char str_move[MAX_BUF];
	MoveLst move_list;

	ASSERT(1, board != NULL);

	gen_moves(board, &move_list);
	printf("Legal moves in the current position:\n");
	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = move_list.move[i];
		
		if (san_notation)
			move_to_san(str_move, board, move);
		else
			move_to_str(move, str_move);
		printf("  %s\n", str_move);
	}
	printf("%d moves in total.\n", move_list.nmoves);
}

/* Test the SEE to make sure it works correctly.  */
void
test_see(const char *fen, const char *san_move)
{
	U32 move;
	Board board;

	ASSERT(1, fen != NULL);
	ASSERT(1, san_move != NULL);

	fen_to_board(&board, fen);
	move = san_to_move(&board, san_move);
	print_board(&board);
	print_move_details(move);
	printf("\nSEE: %d\n", see(&board, move, board.color));
}

