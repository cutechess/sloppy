/* Sloppy - game.c

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
#include <string.h>
#include <time.h>
#include <errno.h>
#include "sloppy.h"
#include "chess.h"
#include "debug.h"
#include "util.h"
#include "movegen.h"
#include "makemove.h"
#include "book.h"
#include "notation.h"
#include "search.h"
#include "input.h"
#include "game.h"

#define GAME_LOG "gamelog.txt"
#define GAMES_FILE "games.pgn"

typedef enum _MateType
{
	NO_MATE,
	WHITE_MATES,
	BLACK_MATES,
	STALEMATE
} MateType;


/* Returns true if both sides have insufficient mating material.  */
static bool
insuf_mat(Board *board)
{
	U64 *whites;
	U64 *blacks;

	ASSERT(1, board != NULL);

	whites = &board->pcs[WHITE][ALL];
	blacks = &board->pcs[BLACK][ALL];
	
	if (popcount(*whites) <= 2
	&&  *whites == (whites[KING] | whites[KNIGHT] | whites[BISHOP])
	&&  popcount(*blacks) <= 2
	&&  *blacks == (blacks[KING] | blacks[KNIGHT] | blacks[BISHOP]))
		return true;
	
	return false;
}

static MateType
get_mate_type(Board *board)
{
	MoveLst move_list;

	ASSERT(1, board != NULL);

	gen_moves(board, &move_list);
	if (move_list.nmoves > 0)
		return NO_MATE;

	if (board->posp->in_check) {
		if (board->color == BLACK)
			return WHITE_MATES;
		return BLACK_MATES;
	}
	return STALEMATE;
}

/* Print the result of the game and return true if the game is over.
   Otherwise just return false.  */
static bool
is_game_over(Board *board)
{
	bool game_over = true;

	ASSERT(1, board != NULL);

	switch (get_mate_type(board)) {
	case STALEMATE:
		printf("1/2-1/2 {Stalemate}\n");
		break;
	case WHITE_MATES:
		printf("1-0 {White mates}\n");
		break;
	case BLACK_MATES:
		printf("0-1 {Black mates}\n");
		break;
	case NO_MATE:
		if (get_nrepeats(board, 3) >= 2)
			printf("1/2-1/2 {Draw by repetition}\n");
		else if (insuf_mat(board))
			printf("1/2-1/2 {Insufficient mating material}\n");
		else if (board->posp->fifty >= 100)
			printf("1/2-1/2 {Draw by 50 move rule}\n");
		else
			game_over = false;
		break;
	}
	return game_over;
}

/* Make a move, update the log, display board, etc.  */
void
update_game(Chess *chess, U32 move)
{
	Board *board;

	ASSERT(1, chess != NULL);
	ASSERT(1, chess->game_over != true);

	board = &chess->board;

	make_move(board, move);
	if (chess->protocol == PROTO_NONE)
		print_board(board);
	if (is_game_over(board))
		chess->game_over = true;
}

/* Choose the best move (by searching or using the book), and make it.  */
static void
cpu_move(Chess *chess)
{
	bool book_used = false;
	char san_move[MAX_BUF];
	char str_move[MAX_BUF];
	U32 move = NULLMOVE;
	int score = 0;
	S64 timer;
	Board *board;

	ASSERT(1, chess != NULL);

	board = &chess->board;
	timer = get_ms();
	
	chess->sd.cmd_type = CMDT_CONTINUE;
	if (settings.book_type != BOOK_OFF)
		move = get_book_move(board, chess->show_pv, chess->book);
	if (move != NULLMOVE) {
		book_used = true;
		chess->in_book = true;
	} else {
		score = id_search(chess, NULLMOVE);
		if (chess->sd.cmd_type == CMDT_CANCEL) {
			chess->cpu_color = COLOR_NONE;
			return;
		}
		move = chess->sd.move;
		chess->in_book = false;
	}
	timer = get_ms() - timer;

	ASSERT(1, move != NULLMOVE);
	move_to_str(move, str_move);

	if (SIGN(board->color)*score < VAL_RESIGN) {
		if (board->color == WHITE)
			printf("0-1 {White resigns}\n");
		else
			printf("1-0 {Black resigns}\n");
		chess->game_over = true;
		return;
	}

	printf("move %s\n", str_move);
	if (chess->debug && chess->sd.nnodes > 0) {
		print_search_data(&chess->sd, (int)timer);
		printf("Score: %d\n", score);
	}

	move_to_san(san_move, board, move);
	update_game_log(board, san_move, score, book_used);
	update_game(chess, move);
}

/* Analyze mode for any supported chess protocol.  */
void
analyze_mode(Chess *chess)
{
	CmdType *cmd_type;

	ASSERT(1, chess != NULL);
	
	cmd_type = &chess->sd.cmd_type;
	chess->cpu_color = COLOR_NONE;
	*cmd_type = CMDT_CONTINUE;
	while (chess->analyze) {
		if (!chess->game_over && *cmd_type != CMDT_CANCEL) {
			id_search(chess, NULLMOVE);
			/* If the maximum search depth is reached, there's no
			   reason to search again until something changes. */
			if (*cmd_type == CMDT_CONTINUE)
				*cmd_type = CMDT_CANCEL;
		} else {
			read_input(chess);
			*cmd_type = CMDT_CONTINUE;
		}
	}
}

void
main_loop(Chess *chess)
{
	while (true) {
		if (chess->board.color == chess->cpu_color && !chess->game_over)
			cpu_move(chess);
		else if (read_input(chess) != 0)
			break;
	}
}

/* Get a date string for a PGN [Date] tag.  */
static void
get_date_for_pgn(char *date)
{
	time_t td;
	struct tm *dcp;

	ASSERT(1, date != NULL);
	
	time(&td);
	dcp = localtime(&td);
	if (dcp != NULL)
		strftime(date, MAX_BUF, "%Y.%m.%d", dcp);
	else {
		my_perror("Couldn't get local time");
		strlcpy(date, "<no date>", MAX_BUF);
	}
}

/* End the game and log it in PGN format.  */
void
log_game(const char *result, const char *wname, const char *bname)
{
	FILE *fp_games;
	FILE *fp_log;
	int c;
	char date[MAX_BUF];
	
	ASSERT(1, result != NULL);
	ASSERT(1, wname != NULL);
	ASSERT(1, bname != NULL);

	if (!settings.use_log)
		return;

	if ((fp_games = fopen(GAMES_FILE, "a")) == NULL) {
		my_perror("Can't open file %s", GAMES_FILE);
		return;
	}
	if ((fp_log = fopen(GAME_LOG, "r")) == NULL) {
		my_perror("Can't open file %s", GAME_LOG);
		my_close(fp_games, GAMES_FILE);
		return;
	}

	get_date_for_pgn(date);
	fprintf(fp_games, "[Event \"?\"]\n");
	fprintf(fp_games, "[Site \"?\"]\n");
	fprintf(fp_games, "[Date \"%s\"]\n", date);
	fprintf(fp_games, "[Round \"?\"]\n");
	fprintf(fp_games, "[White \"%s\"]\n", wname);
	fprintf(fp_games, "[Black \"%s\"]\n", bname);
	fprintf(fp_games, "[Result \"%s\"]\n", result);

	while ((c = fgetc(fp_log)) != EOF)
		fputc(c, fp_games);
	my_close(fp_log, GAME_LOG);

	fprintf(fp_games, " %s\n\n\n", result);
	my_close(fp_games, GAMES_FILE);
}

/* Updates the log file with details of the last move.  */
void
update_game_log(Board *board, const char *str_move, int score, bool book_used)
{
	int move_num;
	FILE *fp;

	ASSERT(1, board != NULL);
	ASSERT(1, str_move != NULL);
	
	if (!settings.use_log)
		return;
	
	move_num = (board->nmoves / 2) + 1;

	if ((fp = fopen(GAME_LOG, "a")) == NULL) {
		my_perror("Can't open file %s", GAME_LOG);
		return;
	}

	if (board->color == WHITE)
		fprintf(fp, "\n%d.", move_num);
	fprintf(fp, " %s", str_move);

	if (book_used)
		fprintf(fp, " {book}");
	else if (score != VAL_NONE) {
		if (score > 0)
			fprintf(fp, " {+%.2f}", (double)score / 100.0);
		else
			fprintf(fp, " {%.2f}", (double)score / 100.0);
	}

	my_close(fp, GAME_LOG);
}

/* Starts a new game in position <fen>.  */
void
new_game(Chess *chess, const char *fen, int new_cpu_color)
{
	ASSERT(1, chess != NULL);
	ASSERT(1, fen != NULL);

	if (fen_to_board(&chess->board, fen)) {
		printf("Invalid FEN string: %s\n", fen);
		return;
	}
	chess->game_over = false;
	chess->in_book = false;
	chess->cpu_color = new_cpu_color;

	/* Delete the (possibly) existing GAME_LOG file.  */
	if (remove(GAME_LOG) && errno != ENOENT)
		my_perror("Can't delete file %s", GAME_LOG);
}

