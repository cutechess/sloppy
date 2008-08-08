/* Sloppy - book.c
   Functions for creating, searching and updating Sloppy's opening book.

   Sloppy's binary book format:
     U64 key -- the hash key
     U16 games -- the number of times the position was reached
     U16 wins -- the number of times reaching the position turned into a win
   This is repeated for every position in the book. All in little-endian.
   

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
#include <string.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "avltree.h"
#include "movegen.h"
#include "makemove.h"
#include "notation.h"
#include "book.h"


#define BOOK_NODE_SIZE (sizeof(U64) + sizeof(U16) + sizeof(U16))

static bool book_modified = false;


/* Returns true if <filename> exists.  */
bool
book_exists(const char *filename)
{
	FILE *fp;
	if ((fp = fopen(filename, "rb")) == NULL)
		return false;
	my_close(fp, filename);

	return true;
}

/* Returns the number of positions in a book file.  */
static int
get_pos_count(FILE *fp)
{
	long cur_fpos;
	long end_fpos;

	ASSERT(1, fp != NULL);

	cur_fpos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	end_fpos = ftell(fp);
	fseek(fp, cur_fpos, SEEK_SET);
	
	return (int)(end_fpos / BOOK_NODE_SIZE);
}

/* Read an opening book file and store its positions in an AVL tree.
   Returns 0 if successfull.  */
int
book_to_tree(const char *filename, AvlNode **tree)
{
	int i;
	U16 games;
	U16 wins;
	int npos;
	U64 key;
	FILE *fp;

	ASSERT(1, filename != NULL);
	ASSERT(1, tree != NULL);

	if ((fp = fopen(filename, "rb")) == NULL) {
		my_perror("Can't open file %s", filename);
		return -1;
	}
	clear_avl(*tree);
	*tree = NULL;

	npos = get_pos_count(fp);
	for (i = 0; i < npos; i++) {
		fread(&key, sizeof(U64), 1, fp);
		fread(&games, sizeof(U16), 1, fp);
		fread(&wins, sizeof(U16), 1, fp);
		
		/* Make sure the data is in the right endian format.  */
		key = fix_endian_u64(key);
		games = fix_endian_u16(games);
		wins = fix_endian_u16(wins);
		
		*tree = (AvlNode*)insert_avl(*tree, key, games, wins);
	}
	my_close(fp, filename);

	return 0;
}

/* Give the book position a score based on the number of games and wins.  */
static int
get_book_score(U16 games, U16 wins)
{
	ASSERT(2, games > 0);

	return (wins * wins) / games;
}

/* Do a binary search in file <fp> to find the book position with key <key>.
   If the position is found, return its score. Else return VAL_NONE.  */
static int
find_disk_pos(FILE *fp, U64 key, int npos)
{
	long left;
	long right;
	long mid;
	U64 tmp_key;
	U16 games;
	U16 wins;

	ASSERT(1, fp != NULL);
	ASSERT(1, npos > 0);

	left = 0;
	right = npos - 1;
	/* Seach the positions.  */
	while (right >= left) {
		mid = (left + right) / 2;
		fseek(fp, BOOK_NODE_SIZE * mid, SEEK_SET);
		fread(&tmp_key, sizeof(U64), 1, fp);
		fread(&games, sizeof(U16), 1, fp);
		fread(&wins, sizeof(U16), 1, fp);
		
		/* Make sure the data is in the right endian format.  */
		tmp_key = fix_endian_u64(tmp_key);
		games = fix_endian_u16(games);
		wins = fix_endian_u16(wins);
		
		if (key < tmp_key)
			right = mid - 1;
		else if (key > tmp_key)
			left = mid + 1;
		else
			return get_book_score(games, wins);
	}

	return VAL_NONE;
}

/* Search the binary tree <book> to find the position with key <key>.
   If the position is found, return its score. Else return VAL_NONE.  */
static int
find_ram_pos(U64 key, AvlNode *book)
{
	AvlNode *n;
	
	ASSERT(2, book != NULL);

	if ((n = (AvlNode*)find_avl(book, key)) != NULL)
		return get_book_score(n->games, n->wins);

	return VAL_NONE;
}

/* Get a list of available book moves.
   The book can be a tree (AvlNode *book), or a file if <book> is NULL.
   Returns the combined score of all the moves if successfull.  */
static int
get_book_move_list(Board *board, MoveLst *move_list, AvlNode *book)
{
	int i, npos, tot_score;
	FILE *fp = NULL;

	ASSERT(1, board != NULL);
	ASSERT(1, move_list != NULL);

	npos = 0;
	tot_score = 0;
	if (book == NULL) {
		if ((fp = fopen(settings.book_file, "rb")) == NULL) {
			my_perror("Can't open file %s", settings.book_file);
			return -1;
		}

		npos = get_pos_count(fp);
		if (npos <= 0) {
			fprintf(stderr, "The opening book is empty\n");
			return -1;
		}
	}

	gen_moves(board, move_list);
	for (i = 0; i < move_list->nmoves; i++) {
		U32 move = move_list->move[i];
		int *score = &move_list->score[i];

		make_move(board, move);
		if (get_nrepeats(board, 1) == 0) {
			U64 key = board->posp->key;
			if (book == NULL)
				*score = find_disk_pos(fp, key, npos);
			else
				*score = find_ram_pos(key, book);
		} else
			*score = VAL_NONE;
		if (*score != VAL_NONE)
			tot_score += *score;
		undo_move(board);
	}
	if (book == NULL)
		my_close(fp, settings.book_file);
	
	return tot_score;
}

/* Displays a list of the available book moves.  */
void
print_book(Board *board, AvlNode *book)
{
	int i;
	int nmoves;
	int tot_score;
	MoveLst move_list;
	
	ASSERT(1, board != NULL);

	if (settings.book_type == BOOK_MEM && book == NULL) {
		printf("The opening book is empty or it doesn't exist\n");
		return;
	}

	tot_score = get_book_move_list(board, &move_list, book);
	if (tot_score == 0)
		printf("There are no book moves for the current position\n");
	if (tot_score <= 0)
		return;
	
	printf("Available book moves:\n");
	nmoves = 0;
	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = move_list.move[i];
		int score = move_list.score[i];

		if (score != VAL_NONE) {
			char san_move[MAX_BUF] = "";
			move_to_san(san_move, board, move);
			printf("  %s: %d\n", san_move, score);
			nmoves++;
		}
	}
	printf("%d book moves were found\n", nmoves);
	ASSERT(1, nmoves > 0);
}

/* Displays a list of the available book moves,
   in a format Xboard understands.  */
static void
print_book_x(Board *board, MoveLst *move_list, int tot_score)
{
	int i;
	int nmoves = 0;

	ASSERT(1, board != NULL);
	ASSERT(1, move_list != NULL);
	ASSERT(1, tot_score > 0);

	printf("0 0 0 0 (");
	for (i = 0; i < move_list->nmoves; i++) {
		int score = move_list->score[i];

		if (score != VAL_NONE) {
			double percent;
			char san_move[MAX_BUF];

			percent = ((double)score / tot_score) * 100;
			if (percent < 1)
				continue;
			if (nmoves++ > 0)
				printf(", ");
			move_to_san(san_move, board, move_list->move[i]);
			printf("%s %.0f%%", san_move, percent);
		}
	}
	printf(")\n");
	ASSERT(1, nmoves > 0);
}

/* Probe the book (already in RAM) for a good move to be played.
   Returns NULLMOVE if no book moves with a score above 0 are found.
   If <show_book> is true, a list of available moves is displayed.  */
U32
get_book_move(Board *board, bool show_book, AvlNode *book)
{
	int i;
	int tot_score;
	int cur_score;
	int rand_val;
	MoveLst move_list;

	ASSERT(1, board != NULL);
	
	if (settings.book_type == BOOK_MEM && book == NULL)
		return NULLMOVE;
	cur_score = 0;

	tot_score = get_book_move_list(board, &move_list, book);
	if (tot_score <= 0)
		return NULLMOVE;
	if (show_book)
		print_book_x(board, &move_list, tot_score);

	my_srand((int)get_ms());
	rand_val = my_rand() % tot_score;
	for (i = 0; i < move_list.nmoves; i++) {
		int score = move_list.score[i];

		if (score != VAL_NONE) {
			cur_score += score;
			if (cur_score > rand_val)
				return move_list.move[i];
		}
	}

	return NULLMOVE;
}

/* Store a position in an AVL tree.
   Returns true if the position is new.  */
bool
save_book_pos(U64 key, int points, AvlNode **tree)
{
	U16 wins = 0;
	AvlNode *node;
	
	ASSERT(1, tree != NULL);
	ASSERT(1, points == 0 || points == 2);

	book_modified = true;
	if (points == 2)
		wins = 1;
	if ((node = (AvlNode*)find_avl(*tree, key)) != NULL) {
		if (node->games < UINT16_MAX) {
			(node->games)++;
			node->wins += wins;
		}
		return false;
	}
	*tree = (AvlNode*)insert_avl(*tree, key, 1, wins);

	return true;
}

/* Write an AVL tree to a file (the opening book), then clear the tree.
   Returns 0 if successfull.  */
int
write_book(const char *filename, AvlNode *tree)
{
	FILE *fp;
	
	ASSERT(1, filename != NULL);
	
	if (tree == NULL)
		return -1;

	if (!book_modified)
		return 0;

	if ((fp = fopen(filename, "wb")) == NULL) {
		my_perror("Can't open file %s", filename);
		return -1;
	}

	write_avl(tree, fp);
	
	my_close(fp, filename);
	update_log("Book file saved: %s", filename);

	return 0;
}

/* Store the positions of a played game (stored in <board>) in an AVL tree.
   <winner> is either WHITE or BLACK.  */
void
book_learn(Board *board, int winner, AvlNode **tree)
{
	int i;
	
	ASSERT(1, board != NULL);
	ASSERT(1, tree != NULL);

	if (!settings.use_learning)
		return;
	if (*tree == NULL)
		printf("Creating a new opening book...\n");
	
	for (i = 1; i < board->nmoves; i++) {
		int points;
		
		if (board->pos[i].key == 0 || i > 26)
			continue;
		if (!(i % 2) == winner)
			points = 2;
		else
			points = 0;
		(void)save_book_pos(board->pos[i].key, points, tree);
	}
}

