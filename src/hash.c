/* Sloppy - hash.c
   Hash table and hash key code.
   The code for updating the hash key during a game is in
   make_move() and undo_move().

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


   Notes:

   After making changes to the hash table, the program should be tested
   with Fine #70: 8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1
  
   The best move is Kb1 (a1b1), which should be found with a good
   positive score in reasonable time.  */

#include <stdio.h>
#include <stdlib.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "hash.h"


/* A priority bonus for pv nodes in the hash. It makes pv nodes
   less likely to be replaced by other nodes.  */
#define PV_PRIORITY 3

/* Random values for everything that's needed in a hash key (side to move,
   enpassant square, castling rights, and piece positions).  */
Zobrist zobrist;

/* Sloppy's main hash table.  */
static Hash *hash_table = NULL;

/* Returns a pseudo-random unsigned 64-bit number.  */
static U64
rand64(void)
{
	U64 rand1 = (U64)my_rand();
	U64 rand2 = (U64)my_rand();
	U64 rand3 = (U64)my_rand();

	return rand1 ^ (rand2 << 31) ^ (rand3 << 62);
}

/* Clear the whole hash table.  */
static void
clear_hash_table(void)
{
	int i;
	Hash *hash;
	for (i = 0; i < settings.hash_size; i++) {
		hash = &hash_table[i];
		hash->key = 0;
		hash->val = 0;
		hash->flag = H_NONE;
		hash->best = NULLMOVE;
		hash->depth = 0;
		hash->priority = 0;
	}
}

/* Set a new hash table size (in megabytes),
   and deallocate the old table (if any).  */
void
set_hash_size(int hsize)
{
	ASSERT(1, hsize > 0);
	
	if (hash_table != NULL) {
		free(hash_table);
		hash_table = NULL;
	}
	settings.hash_size = (hsize * 0x100000) / sizeof(Hash);
}

/* Initialize the hash table.  */
void
init_hash(void)
{
	ASSERT(1, settings.hash_size > 0);
	
	hash_table = calloc(settings.hash_size, sizeof(Hash));
	clear_hash_table();
}

/* Initialize the zobrist values.  */
void
init_zobrist(void)
{
	int color;
	int sq;

	zobrist.color = rand64();
	for (color = WHITE; color <= BLACK; color++) {
		int pc;
		zobrist.castle[color][C_KSIDE] = rand64();
		zobrist.castle[color][C_QSIDE] = rand64();
		for (pc = PAWN; pc <= KING; pc++) {
			for (sq = 0; sq < 64; sq++) {
				zobrist.pc[color][pc][sq] = rand64();
			}
		}
	}
	for (sq = 0; sq < 64; sq++)
		zobrist.enpassant[sq] = rand64();
}

/* Free the memory allocated for the hash table.  */
void
destroy_hash(void)
{
	if (hash_table != NULL) {
		free(hash_table);
		hash_table = NULL;
	}
}

/* Convert a value from the hash table into a value that
   can be used in the search.  */
int
val_from_hash(int val, int ply)
{
	ASSERT(2, val_is_ok(val));

	if (val < -VAL_BITBASE)
		val += ply;
	else if (val > VAL_BITBASE)
		val -= ply;

	ASSERT(2, val_is_ok(val));

	return val;
}

/* Convert a search value into a hash value.  */
int
val_to_hash(int val, int ply)
{
	ASSERT(2, val_is_ok(val));

	if (val < -VAL_BITBASE)
		val -= ply;
	else if (val > VAL_BITBASE)
		val += ply;

	ASSERT(2, val_is_ok(val));

	return val;
}

/* Get the best move from the hash table.
   If not successfull, return NULLMOVE.  */
U32
get_hash_move(U64 key)
{
	Hash *hash = &hash_table[key % settings.hash_size];

	if (hash->key == key)
		return hash->best;
	return NULLMOVE;
}

/* Probe the hash table for a score and the best move.
   If not successfull, return VAL_NONE.  */
int
probe_hash(int depth, int alpha, int beta, U64 key, U32 *best_move, int ply)
{
	Hash *hash;

	ASSERT(2, best_move != NULL);

	hash = &hash_table[key % settings.hash_size];
	if (hash->key == key) {
		*best_move = hash->best;
		if ((int)hash->depth >= depth) {
			int val = val_from_hash(hash->val, ply);

			if (hash->flag == H_EXACT)
				return val;
			if (hash->flag == H_ALPHA) {
				if (val <= alpha)
					return alpha;
				if (val < beta)
					return VAL_AVOID_NULL;
			} else if (hash->flag == H_BETA && val >= beta)
				return beta;
		}
	}

	return VAL_NONE;
}

/* Store a hash key and its score and best move in the hash table.  */
void
store_hash(int depth, int val, Hashf flag, U64 key, U32 best_move, int root_ply)
{
	int priority;
	Hash *hash = &hash_table[key % settings.hash_size];

	priority = root_ply + depth;
	if (flag == H_EXACT)
		priority += PV_PRIORITY;

	/* If the existing hash entry is "newer" than the new one, it must be
	   from a previous game and thus its priority should be 0. */
	if (hash->priority - hash->depth > root_ply + PV_PRIORITY)
		hash->priority = 0;

	if (priority >= hash->priority) {
		if ((key != hash->key || hash->best == NULLMOVE)
		||  (best_move != NULLMOVE && flag != H_ALPHA))
			hash->best = best_move;
		hash->key = key;
		hash->val = val;
		hash->flag = (S8)flag;
		hash->depth = depth;
		hash->priority = priority;
	}
}

/* Generate the hash key for a board position.  */
void
comp_hash_key(Board *board)
{
	int color;
	int ep_sq;
	unsigned castle_rights;
	U64 key = 0;
	U64 pawn_key = 0;

	ASSERT(1, board != NULL);

	castle_rights = board->posp->castle_rights;
	ep_sq = board->posp->ep_sq;
	for (color = WHITE; color <= BLACK; color++) {
		int pc;
		U64 mask;

		if ((castle_rights & castling.rights[color][C_KSIDE]) != 0)
			key ^= zobrist.castle[color][C_KSIDE];
		if ((castle_rights & castling.rights[color][C_QSIDE]) != 0)
			key ^= zobrist.castle[color][C_QSIDE];

		mask = board->pcs[color][PAWN];
		while (mask)
			pawn_key ^= zobrist.pc[color][PAWN][pop_lsb(&mask)];
		for (pc = KNIGHT; pc <= KING; pc++) {
			mask = board->pcs[color][pc];
			while (mask)
				key ^= zobrist.pc[color][pc][pop_lsb(&mask)];
		}
	}
	key ^= pawn_key;
	if (ep_sq)
		key ^= zobrist.enpassant[ep_sq];
	if (board->color == BLACK)
		key ^= zobrist.color;

	board->posp->key = key;
	board->posp->pawn_key = pawn_key;
}

