/* Sloppy - pgn.c
   Functions for parsing chess games in PGN files and storing the positions
   in an AVL tree. The tree can later be used as an opening book.

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
#include <ctype.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "avltree.h"
#include "makemove.h"
#include "book.h"
#include "notation.h"
#include "pgn.h"


/* The number of half moves per game that can be stored in the opening book.  */
#define MAX_BOOK_PLIES 26

typedef enum _PgnResult
{
	DRAWN_GAME, WHITE_WINS, BLACK_WINS, NO_RESULT, RESULT_ERROR
} PgnResult;

/* Find the next PGN game in a stream and return the result.  */
static PgnResult
get_pgn_result(FILE *fp)
{
	char line[MAX_BUF];

	while (fgetline(line, MAX_BUF, fp) >= 0) {
		if (!strncmp(line, "[Result ", 8)) {
			if (strstr(line, "1-0") != NULL)
				return WHITE_WINS;
			else if (strstr(line, "0-1") != NULL)
				return BLACK_WINS;
			else if (strstr(line, "1/2-1/2") != NULL)
				return DRAWN_GAME;
			else
				return NO_RESULT;
		}
	}

	return RESULT_ERROR;
}

/* Read a move string from a stream.
   Returns the length of the string if successfull (a move string is found).
   Returns -1 if EOF is reached, or 0 if the word isn't a move string.  */
static int
read_move(char *word, int lim, FILE *fp)
{
	int i;
	int c;

	/* Skip leading spaces.  */
	do {
		c = fgetc(fp);
	} while (isspace(c));

	if (c == EOF)
		return -1;

	/* Save the first character of the word.  */
	*word++ = (char)c;

	/* The word is not a move string.  */
	if (!isalpha(c)) {
		/* If the line starts with a '[' we skip the whole line.
		   Skipping until the closing ']' wouldn't be safe because
		   PGN files often have those pairs broken.  */
		if (c == '[')
			clear_buf(fp);
		/* Skip everything inside brackets.  */
		else if (c == '(' || c == '{') {
			int open_b = c;	/* opening bracket */
			int close_b;	/* closing bracket */
			int nb = 1;	/* num. of brackets */
			if (c == '(')
				close_b = ')';
			else
				close_b = '}';
			while ((c = fgetc(fp)) != EOF) {
				if (c == open_b)
					nb++;
				else if (c == close_b && --nb <= 0)
					break;
			}
		}
		return 0;
	}
	/* Get the rest of the move string.  */
	for (i = 1; i < lim - 1; i++) {
		c = fgetc(fp);
		if (isspace(c))
			break;
		*word++ = (char)c;
	}
	*word = '\0';

	return i;
}

#if DEBUG_LEVEL > 0
/* Returns the line number of the current position in a stream.  */
static int
get_line_num(FILE *fp)
{
	int line_num;
	long pos;
	long end_pos;
	
	ASSERT(1, fp != NULL);
	
	end_pos = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	pos = ftell(fp);
	line_num = 1;
	while (pos < end_pos) {
		int c = fgetc(fp);
		if (c == EOF || c == '\0')
			break;
		if (c == '\n')
			line_num++;
		pos++;
	}
	
	return line_num;
}
#endif /* DEBUG_LEVEL > 0 */

/* Read a PGN file (a collection of one or more games in PGN format) and store
   the positions and their win/loss ratios in an AVL tree (**tree).
   The AVL tree can later be written in an opening book file (book.bin).
   
   Returns the number of new positions added to the tree, or -1 on error.  */
int
pgn_to_tree(const char *filename, AvlNode **tree)
{
	PgnResult result;
	int prev_progress;
	int npos = 0;
	long file_len;
	FILE *fp;
	Board board;

	ASSERT(1, filename != NULL);

	if ((fp = fopen(filename, "r")) == NULL) {
		my_perror("Can't open PGN file %s", filename);
		return -1;
	}

	if (settings.book_type != BOOK_MEM) {
		settings.book_type = BOOK_MEM;
		printf("Changed book mode to \"book in memory\"\n");
	}
	if (*tree == NULL && book_exists(settings.book_file)) {
		printf("Loading opening book to memory...\n");
		book_to_tree(settings.book_file, tree);
	} else if (*tree == NULL)
		printf("Creating a new opening book...\n");

	/* Find out how big the file is.  */
	fseek(fp, 0, SEEK_END);
	file_len = ftell(fp);
	rewind(fp);

	printf("Reading PGN file %s...\n", filename);
	prev_progress = 0;
	progressbar(50, 0);
	while ((result = get_pgn_result(fp)) != RESULT_ERROR) {
		int depth;
		int progress;
		int len;
		char san_move[MAX_BUF];

		/* Games with an unknown result are ignored.  */
		ASSERT(1, result != RESULT_ERROR);
		if (result == NO_RESULT || result == DRAWN_GAME)
			continue;

		depth = 0;
		fen_to_board(&board, START_FEN);

		while ((len = read_move(san_move, MAX_BUF, fp)) >= 0) {
			int points = 0;
			U32 move;

			/* break out of the loop when a new game begins */
			if (depth > 0 && san_move[0] == '[')
				break;
			if (len < 2)
				continue;

			move = san_to_move(&board, san_move);
			if (move == NULLMOVE) {
				#if DEBUG_LEVEL > 0
				update_log("Illegal move in %s: %s, line: %d\n",
				          filename, san_move, get_line_num(fp));
				#endif /* DEBUG_LEVEL > 0 */
				break;
			}

			if ((result == WHITE_WINS && board.color == WHITE)
			||  (result == BLACK_WINS && board.color == BLACK))
				points = 2;

			make_move(&board, move);
			if (save_book_pos(board.posp->key, points, tree))
				npos++;

			if (++depth >= MAX_BOOK_PLIES)
				break;
		}
		progress = (ftell(fp) * 50) / file_len;
		if (progress > prev_progress) {
			progressbar(50, progress);
			prev_progress = progress;
		}
	}
	progressbar(50, 50);
	my_close(fp, filename);
	printf("\n");

	return npos;
}

