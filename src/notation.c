/* Sloppy - notation.c
   Functions for converting moves, board, etc. between Sloppy's own formats
   and text formats.

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
#include <ctype.h>
#include <string.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "movegen.h"
#include "makemove.h"
#include "eval.h"
#include "hash.h"
#include "notation.h"


/* Convert a piece type from character into an integer.  */
static int
get_pc_type_int(char pc_type_chr)
{
	switch (pc_type_chr) {
	case 'P':
		return PAWN;
	case 'N':
		return KNIGHT;
	case 'B':
		return BISHOP;
	case 'R':
		return ROOK;
	case 'Q':
		return QUEEN;
	case 'K':
		return KING;
	default:
		return 0;
	}
}

/* Convert a piece type from integer into a character.  */
char
get_pc_type_chr(int pc_type_int)
{
	switch (pc_type_int) {
	case PAWN:
		return 'P';
	case KNIGHT:
		return 'N';
	case BISHOP:
		return 'B';
	case ROOK:
		return 'R';
	case QUEEN:
		return 'Q';
	case KING:
		return 'K';
	default:
		return '\0';
	}
}

/* Convert a chess board file letter ('a' to 'h') into an integer of 0 to 7.  */
static int
get_file_int(char file_chr)
{
	if (file_chr >= 'a' && file_chr <= 'h')
		return (int)(file_chr - 'a');
	return -1;
}

/* Convert a chess board file (0 to 7) into a character ('a' to 'h').  */
static char
get_file_chr(int file_int)
{
	if (file_int >= 0 && file_int <= 7)
		return (char)('a' + file_int);
	return '\0';
}

/* Convert a chess board rank number ('1' to '8') into an integer of 0 to 7.  */
static int
get_rank_int(char rank_chr)
{
	if (rank_chr >= '1' && rank_chr <= '8')
		return 7 - (int)(rank_chr - '1');
	return -1;
}

/* Convert a chess board rank (0 to 7) into a character ('1' to '8').  */
static char
get_rank_chr(int rank_int)
{
	if (rank_int >= 0 && rank_int <= 7)
		return (char)('1' + (7 - rank_int));
	return '\0';
}

/* Convert a promotion character into int format.  */
static int
get_promotion_int(char prom_chr)
{
	switch (prom_chr) {
	case 'r':
		return ROOK;
	case 'b':
		return BISHOP;
	case 'n':
		return KNIGHT;
	case 'q':
		return QUEEN;
	default:
		return -1;
	}
}

/* Convert a promotion from integer to character.  */
static char
get_promotion_chr(int prom_int)
{
	switch (prom_int) {
	case KNIGHT:
		return 'N';
	case BISHOP:
		return 'B';
	case ROOK:
		return 'R';
	case QUEEN:
		return 'Q';
	default:
		return '\0';
	}
}

/* Convert a chess square from string (eg. "e4") to int format (0 to 63).  */
static int
get_sq_from_str(const char *sq_str)
{
	int file;
	int rank;
	
	ASSERT(1, sq_str != NULL);
	
	/* Strings shorter than 2 character aren't accepted, but longer ones
	   are because the string is often a part of a bigger string.  */
	if (strlen(sq_str) < 2)
		return -1;

	file = get_file_int(sq_str[0]);
	rank = get_rank_int(sq_str[1]);

	if (file == -1 || rank == -1)
		return -1;

	return (rank * 8) + file;
}

/* Convert an enpassant square from string to int format (0 to 63).  */
static int
get_ep_sq_int(const char *sq_str)
{
	int sq;
	
	ASSERT(1, sq_str != NULL);
	
	/* Enpassant capture is not possible.  */
	if (sq_str[0] == '-')
		return 0;

	sq = get_sq_from_str(sq_str);
	if (sq == -1)
		return -1;

	return sq;
}

/* Convert a chess game move count from string to int format.  */
static int
get_move_count_int(const char *nmoves_str)
{
	int nmoves;
	
	ASSERT(1, nmoves_str != NULL);
	
	if (strlen(nmoves_str) < 1)
		return -1;

	nmoves = atoi(nmoves_str);
	if (nmoves < 0 || (nmoves * 2) >= MAX_NMOVES_PER_GAME)
		return -1;

	return nmoves;
}

/* Converts the color from a character ('w' or 'b')
   into integer format (WHITE or BLACK).  */
static int
get_color_int(char color_chr)
{
	switch (color_chr) {
	case 'w':
		return WHITE;
	case 'b':
		return BLACK;
	default:
		return -1;
	}
}

/* Returns true if <word> is a move string (in coordinate notation).
   It doesn't have to be a legal or even a pseudo-legal move though.  */
bool
is_move_str(const char *word)
{
	int from;
	int to;

	ASSERT(1, word != NULL);
	
	from = get_sq_from_str(&word[0]);
	if (from == -1)
		return false;

	to = get_sq_from_str(&word[2]);
	if (to == -1)
		return false;

	if (strlen(word) > 4) {
		int prom = get_promotion_int(word[4]);
		if (prom == -1)
			return false;
	}
	
	return true;
}

/* Convert a move string (in coordinate notation) into a move.  */
U32
str_to_move(Board *board, const char *str_move)
{
	int i;
	int pc;
	int from;
	int to;
	int prom = 0;
	MoveLst move_list;

	ASSERT(1, board != NULL);
	ASSERT(1, str_move != NULL);

	from = get_sq_from_str(&str_move[0]);
	if (from == -1)
		return MOVE_ERROR;

	to = get_sq_from_str(&str_move[2]);
	if (to == -1)
		return MOVE_ERROR;

	if (strlen(str_move) > 4) {
		prom = get_promotion_int(str_move[4]);
		if (prom == -1)
			return MOVE_ERROR;
	}

	pc = board->mailbox[from];
	gen_pc_moves(board, &move_list, pc, to);
	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = move_list.move[i];

		if (GET_FROM(move) == from && GET_PROM(move) == prom)
			return move;
	}

	return NULLMOVE;
}

/* Convert a move into a move string (in coordinate notation).  */
void
move_to_str(U32 move, char *str_move)
{
	int prom;

	ASSERT(1, move != NULLMOVE);
	ASSERT(1, str_move != NULL);

	prom = GET_PROM(move);
	*str_move++ = get_file_chr(SQ_FILE(GET_FROM(move)));
	*str_move++ = get_rank_chr(SQ_RANK(GET_FROM(move)));
	*str_move++ = get_file_chr(SQ_FILE(GET_TO(move)));
	*str_move++ = get_rank_chr(SQ_RANK(GET_TO(move)));
	if (prom != 0)
		*str_move++ = tolower(get_promotion_chr(prom));
	*str_move++ = '\0';
}

/* Find out how much detail is needed to describe a move by <pc> to square <to>
   so that it can't be mixed up with any other legal move.  */
#define SAN_UNIQUE_MOVE 00
#define SAN_FILE_NEEDED 01
#define SAN_RANK_NEEDED 02
static unsigned
needed_move_details(Board *board, int pc, int from, int to)
{
	bool unique = true;
	bool unique_rank = true;
	bool unique_file = true;
	unsigned ret_val = SAN_UNIQUE_MOVE;
	int i;
	MoveLst move_list;
	
	ASSERT(1, board != NULL);

	gen_pc_moves(board, &move_list, pc, to);
	for (i = 0; i < move_list.nmoves; i++) {
		int from2 = GET_FROM(move_list.move[i]);

		if (from2 != from) {
			unique = false;
			if (SQ_FILE(from2) == SQ_FILE(from))
				unique_file = false;
			if (SQ_RANK(from2) == SQ_RANK(from))
				unique_rank = false;
		}
	}
	if (!unique) {
		if (!unique_rank || unique_file)
			ret_val |= SAN_FILE_NEEDED;
		if (!unique_file)
			ret_val |= SAN_RANK_NEEDED;
	}

	return ret_val;
}

typedef enum _MoveType
{
	MT_NORMAL, MT_CHECK, MT_MATE, MT_ERROR
} MoveType;

/* Find out whether a move is a normal move, a check, or a mate.  */
static MoveType
get_move_type(Board *board, U32 move)
{
	MoveType move_type;
	MoveLst move_list;

	ASSERT(1, board != NULL);
	
	if (!IS_CHECK(move))
		return MT_NORMAL;

	move_type = MT_CHECK;
	make_move(board, move);

	ASSERT(1, board->posp->in_check);
	gen_moves(board, &move_list);
	if (move_list.nmoves == 0)
		move_type = MT_MATE;

	undo_move(board);

	return move_type;
}

/* Convert a move into a move string (in SAN notation).  */
void
move_to_san(char *san_move, Board *board, U32 move)
{
	MoveType move_type;
	int pc;
	int from;
	int to;

	ASSERT(1, san_move != NULL);
	ASSERT(1, board != NULL);
	ASSERT(1, move != NULLMOVE);

	move_type = get_move_type(board, move);

	if (IS_CASTLING(move)) {
		if (GET_CASTLE(move) == C_KSIDE)
			strlcpy(san_move, "O-O", MAX_BUF);
		else
			strlcpy(san_move, "O-O-O", MAX_BUF);

		if (move_type == MT_CHECK)
			strlcat(san_move, "+", MAX_BUF);
		else if (move_type == MT_MATE)
			strlcat(san_move, "#", MAX_BUF);

		return;
	}

	pc = GET_PC(move);
	from = GET_FROM(move);
	to = GET_TO(move);
	if (pc != PAWN) {
		unsigned nmd;

		*san_move++ = get_pc_type_chr(pc);
		nmd = needed_move_details(board, pc, from, to);
		if (nmd & SAN_FILE_NEEDED)
			*san_move++ = get_file_chr(SQ_FILE(from));
		if (nmd & SAN_RANK_NEEDED)
			*san_move++ = get_rank_chr(SQ_RANK(from));
	}

	if (GET_CAPT(move)) {
		if (pc == PAWN)
			*san_move++ = get_file_chr(SQ_FILE(from));
		*san_move++ = 'x';
	}

	*san_move++ = get_file_chr(SQ_FILE(to));
	*san_move++ = get_rank_chr(SQ_RANK(to));
	
	if (GET_PROM(move)) {
		*san_move++ = '=';
		*san_move++ = get_promotion_chr(GET_PROM(move));
	}

	if (move_type == MT_CHECK)
		*san_move++ = '+';
	else if (move_type == MT_MATE)
		*san_move++ = '#';

	*san_move++ = '\0';
}

/* Convert a move string (in SAN notation) into a move.  */
U32
san_to_move(Board *board, const char *san_move)
{
	int i;
	int to;
	int pc;
	MoveLst move_list;
	
	ASSERT(1, board != NULL);
	ASSERT(1, san_move != NULL);

	/* In the SAN format every move must start with a letter.  */
	if (!isalpha(*san_move))
		return NULLMOVE;

	/* Castling moves.  */
	if (!strncmp(san_move, "O-O-O", 5)) {
		pc = KING;
		to = castling.king_sq[board->color][C_QSIDE][C_TO];
	} else if (!strncmp(san_move, "O-O", 3)) {
		pc = KING;
		to = castling.king_sq[board->color][C_KSIDE][C_TO];
	} else {
		const char *lastc = san_move + strlen(san_move) - 1;

		/* Ignore the possible check or checkmate symbol
		   at the end of the move string.  */
		if (*lastc == '#' || *lastc == '+')
			lastc--;

		pc = get_pc_type_int(*san_move);
		if (pc == 0)
			pc = PAWN;
		/* Ignore the possible promotion.  */
		if (pc == PAWN && get_pc_type_int(*lastc) != 0)
			lastc -= 2;

		to = get_sq_from_str(lastc - 1);
		if (to == -1)
			return NULLMOVE;
	}

	gen_pc_moves(board, &move_list, pc, to);
	if (move_list.nmoves == 1)
		return move_list.move[0];
	for (i = 0; i < move_list.nmoves; i++) {
		char tmp_san_move[MAX_BUF];

		move_to_san(tmp_san_move, board, move_list.move[i]);
		if (!strcmp(tmp_san_move, san_move))
			return move_list.move[i];
	}

	return NULLMOVE;
}

/* Convert castling rights from a part of a FEN string,
   into Sloppy's own format.  */
static int
get_castle_rights(const char *fen)
{
	unsigned castle_rights = 0;
	size_t len;
	
	ASSERT(1, fen != NULL);

	len = strlen(fen);
	if (len < 1 || len > 4)
		return -1;

	if (*fen == '-' && len == 1)
		return 0;

	while (*fen) {
		switch (*fen++) {
		case 'K':
			castle_rights |= castling.rights[WHITE][C_KSIDE];
			break;
		case 'Q':
			castle_rights |= castling.rights[WHITE][C_QSIDE];
			break;
		case 'k':
			castle_rights |= castling.rights[BLACK][C_KSIDE];
			break;
		case 'q':
			castle_rights |= castling.rights[BLACK][C_QSIDE];
			break;
		default:
			return -1;
		}
	}

	return (int)castle_rights;
}

/* Convert a FEN board into mailbox format.  */
static int
fen_to_mailbox(const char *fen, int *mailbox)
{
	int sq = 0;
	int rank_end_sq = 0;	/* last square of the previous rank */
	
	ASSERT(1, fen != NULL);
	ASSERT(1, mailbox != NULL);

	if (strlen(fen) < 15)
		return -1;

	while (*fen) {
		char c = *fen++;
		int pc = 0;

		/* Move to the next rank.  */
		if (c == '/') {
			/* Reject the FEN string if the rank didn't
			   have exactly 8 squares.  */
			if (sq - rank_end_sq != 8)
				return -1;
			rank_end_sq = sq;
			continue;
		}
		/* Add 1-8 empty squares.  */ 
		if (isdigit(c)) {
			int j;
			int nempty = atoi(fen - 1);

			if (nempty < 1 || nempty > 8 || sq + nempty > 64)
				return -1;
			for (j = 0; j < nempty; j++)
				mailbox[sq++] = 0;
			continue;
		}
		/* Add a white piece.  */
		else if (isupper(c))
			pc = get_pc_type_int(c);
		/* Add a black piece.  */
		else if (islower(c))
			pc = -get_pc_type_int(toupper(c));
		if (pc == 0 || sq > 63)
			return -1;
		mailbox[sq++] = pc;
	}
	/* The board must have exactly 64 squares (0 to 63) and each rank
	   must have exactly 8 squares.  */
	if (sq != 64 || sq - rank_end_sq != 8)
		return -1;

	return 0;
}

/* Set piece masks in a board according to a mailbox encoding.  */
static void
set_squares(Board *board, const int *mailbox)
{
	int color;
	
	ASSERT(1, board != NULL);
	ASSERT(1, mailbox != NULL);

	board->all_pcs = 0;
	for (color = WHITE; color <= BLACK; color++) {
		int i;
		int sign = SIGN(color);
		U64 *my_pcs = &board->pcs[color][ALL];
		for (i = ALL; i <= KING; i++)
			my_pcs[i] = 0;
		for (i = 0; i < 64; i++) {
			int pc = abs(mailbox[i]);
			board->mailbox[i] = pc;
			if ((sign * mailbox[i]) > 0) {
				my_pcs[pc] |= bit64[i];
				my_pcs[ALL] |= bit64[i];
			}
		}
		my_pcs[BQ] = my_pcs[BISHOP] | my_pcs[QUEEN];
		my_pcs[RQ] = my_pcs[ROOK] | my_pcs[QUEEN];
		board->all_pcs |= my_pcs[ALL];
		board->king_sq[color] = get_lsb(my_pcs[KING]);
	}
	ASSERT(1, board_is_ok(board));
}

/* Compute the amount of material each side (color) has,
   and store it in board->material[color].  */
static void
comp_material(Board *board)
{
	int color;
	int phase;

	ASSERT(1, board != NULL);

	phase = max_phase;
	for (color = WHITE; color <= BLACK; color++) {
		int pc;
		int score = 0;

		for (pc = KNIGHT; pc <= QUEEN; pc++) {
			int npieces = popcount(board->pcs[color][pc]);
			score += npieces * pc_val[pc];
			phase -= npieces * phase_val[pc];
		}
		ASSERT(1, score >= 0);
		board->material[color] = score;
	}

	/* If <phase> == <max_phase> then there are only kings and pawns on the
	   board. If there's more material on the board than in the default
	   starting position, phase gets a negative value. It's not a bug,
	   it's a feature!  */
	ASSERT(1, phase <= max_phase);
	board->phase = phase;
}

/* Set the board to position <fen> which uses the Forsyth-Edwards notation:
   http://en.wikipedia.org/wiki/Forsyth-Edwards_Notation  */
int
fen_to_board(Board *board, const char *fen)
{
	char tmp_fen[MAX_BUF];
	char *fen_item = NULL;
	char *svptr;
	int mailbox[64];
	int color;
	int castle_rights;
	int ep_sq;
	int fifty;
	int nmoves;
	int i;

	ASSERT(1, board != NULL);
	ASSERT(1, fen != NULL);

	strlcpy(tmp_fen, fen, MAX_BUF);

	/* Get squares and piece positions.  */
	if ((fen_item = strtok_r(tmp_fen, " ", &svptr)) == NULL)
		return -1;
	if (fen_to_mailbox(fen_item, mailbox))
		return -1;

	/* Get side to move.  */
	if ((fen_item = strtok_r(NULL, " ", &svptr)) == NULL)
		return -1;
	if (strlen(fen_item) != 1)
		return -1;
	color = get_color_int(fen_item[0]);
	if (color == -1)
		return -1;

	/* Get castling rights.  */
	if ((fen_item = strtok_r(NULL, " ", &svptr)) == NULL)
		return -1;
	castle_rights = get_castle_rights(fen_item);
	if (castle_rights == -1)
		return -1;

	/* Get enpassant square.  */
	if ((fen_item = strtok_r(NULL, " ", &svptr)) == NULL)
		return -1;
	ep_sq = get_ep_sq_int(fen_item);
	if (ep_sq == -1)
		return -1;

	/* Get move count for the fifty-move rule.  */
	if ((fen_item = strtok_r(NULL, " ", &svptr)) == NULL)
		fifty = 0;
	else
		fifty = get_move_count_int(fen_item);
	if (fifty < 0 || fifty > 99)
		return -1;

	/* Get move number of the current full move.  */
	if ((fen_item = strtok_r(NULL, " ", &svptr)) == NULL)
		nmoves = 0;
	else
		nmoves = (get_move_count_int(fen_item) - 1) * 2;
	if (color == BLACK)
		nmoves++;
	if (nmoves < 0 || nmoves >= MAX_NMOVES_PER_GAME)
		return -1;

	board->color = color;
	board->nmoves = nmoves;

	/* Clear game history in case we're not at move no. 1.  */
	for (i = 0; i < nmoves; i++) {
		PosInfo *tmp_pos = &board->pos[i];
		
		tmp_pos->key = 0;
		tmp_pos->move = NULLMOVE;
		tmp_pos->castle_rights = 0;
		tmp_pos->ep_sq = 0;
		tmp_pos->fifty = 0;
		tmp_pos->in_check = false;
	}

	board->posp = &board->pos[nmoves];
	board->posp->castle_rights = (unsigned)castle_rights;
	board->posp->ep_sq = ep_sq;
	board->posp->fifty = fifty;
	board->posp->move = NULLMOVE;
	
	set_squares(board, mailbox);
	comp_material(board);

	if (board_is_check(board))
		board->posp->in_check = true;
	else
		board->posp->in_check = false;

	comp_hash_key(board);
	
	return 0;
}

/* Convert a board into a board string (in FEN notation).  */
void
board_to_fen(const Board *board, char *fen)
{
	int sq;
	int nempty = 0;	/* num. of successive empty squares on a rank */
	int ep_sq;	/* enpassant square */
	int fifty;	/* num. of successive reversible moves */
	unsigned castle_rights;
	
	ASSERT(1, board != NULL);
	ASSERT(1, fen != NULL);

	castle_rights = board->posp->castle_rights;
	ep_sq = board->posp->ep_sq;
	fifty = board->posp->fifty;

	for (sq = 0; sq < 64; sq++) {
		/* Add a slash character between the ranks.  */
		if (sq > 0 && SQ_FILE(sq) == 0)
			*fen++ = '/';
		/* There's a piece on <sq>.  */
		if (board->mailbox[sq] != 0) {
			char pc = get_pc_type_chr(board->mailbox[sq]);

			/* Add the num. of empty squares before <sq>.  */
			if (nempty > 0) {
				*fen++ = (char)('0' + nempty);
				nempty = 0;
			}

			if (board->pcs[WHITE][ALL] & bit64[sq])
				*fen++ = pc;
			else
				*fen++ = tolower(pc);
		/* When the H file is reached we add the number of
		   successive empty squares, and reset the counter.  */
		} else if (SQ_FILE(sq) == 7) {
			*fen++ = (char)('0' + (nempty + 1));
			nempty = 0;
		/* <sq> is empty, so increase the counter.  */
		} else
			nempty++;
	}
	*fen++ = ' ';
	
	/* Add the side to move.  */
	if (board->color == WHITE)
		*fen++ = 'w';
	else
		*fen++ = 'b';
	*fen++ = ' ';

	/* Add castling rights.  */
	if (!(castle_rights & castling.all_rights[WHITE])
	&&  !(castle_rights & castling.all_rights[BLACK]))
		*fen++ = '-';
	else {
		if ((castle_rights & castling.rights[WHITE][C_KSIDE]) != 0)
			*fen++ = 'K';
		if ((castle_rights & castling.rights[WHITE][C_QSIDE]) != 0)
			*fen++ = 'Q';
		if ((castle_rights & castling.rights[BLACK][C_KSIDE]) != 0)
			*fen++ = 'k';
		if ((castle_rights & castling.rights[BLACK][C_QSIDE]) != 0)
			*fen++ = 'q';
	}
	*fen++ = ' ';

	/* Add enpassant square.  */
	if (ep_sq != 0) {
		*fen++ = get_file_chr(SQ_FILE(ep_sq));
		*fen++ = get_rank_chr(SQ_RANK(ep_sq));
	} else
		*fen++ = '-';
	*fen++ = ' ';

	/* Add the num. of successive reversible moves.  */
	{
		char str_fifty[MAX_BUF];
		int i;

		snprintf(str_fifty, MAX_BUF, "%d", fifty);
		for (i = 0; i < (int)strlen(str_fifty); i++)
			*fen++ = str_fifty[i];
		*fen++ = ' ';
	}

	/* Add the move number of the current full move.  */
	{
		char str_nfullmoves[MAX_BUF];
		int nmoves = (board->nmoves / 2) + 1;
		int i;

		snprintf(str_nfullmoves, MAX_BUF, "%d", nmoves);
		for (i = 0; i < (int)strlen(str_nfullmoves); i++)
			*fen++ = str_nfullmoves[i];
	}

	*fen++ = '\0';
}

