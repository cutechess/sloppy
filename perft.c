/* Sloppy - perft.c
   Perft tests Sloppy's move generation, makemove and hash table implementation,
   and it can also be used as a benchmark.

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


   After making changes to movegen, makemove or undomove, or perft itself,
   run perft in the KiwiPete position:
   r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1

   Perft numbers for the first 6 plies are:

   1: 48
   2: 2039
   3: 97862
   4: 4085603
   5: 193690690
   6: 8031647685  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sloppy.h"
#include "debug.h"
#include "util.h"
#include "movegen.h"
#include "makemove.h"
#include "notation.h"
#include "thread.h"

#define PERFT_HASH_SIZE 0x200000

typedef struct _PerftHash
{
	int depth;
	U64 nnodes;
	U64 key;
} PerftHash;


#ifdef USE_THREADS

typedef struct _PerftJob
{
	Board board;
	U32 move;
} PerftJob;

typedef struct _PerftData
{
	PerftHash *hash;
	PerftJob *jobs;
	bool divide;
	int depth;
	int njobs;
	int job_index;
	U64 nnodes;
	mutex_t job_mutex;
	mutex_t node_count_mutex;
} PerftData;

static mutex_t hash_mutex;

#endif /* USE_THREADS */


static void
init_perft_hash(PerftHash *hash)
{
	int i;

	ASSERT(1, hash != NULL);

	for (i = 0; i < PERFT_HASH_SIZE; i++) {
		hash[i].depth = 0;
		hash[i].nnodes = 0;
		hash[i].key = 0;
	}
}

static U64
probe_perft_hash(U64 key, int depth, const PerftHash *hash)
{
	U64 nnodes = 0;

	ASSERT(2, hash != NULL);

	hash = &hash[key % PERFT_HASH_SIZE];
	if (hash->key == key && hash->depth == depth)
		nnodes = hash->nnodes;

	return nnodes;
}

static void
store_perft_hash(U64 key, U64 nnodes, int depth, PerftHash *hash)
{
	ASSERT(2, hash != NULL);

	hash = &hash[key % PERFT_HASH_SIZE];

#ifdef USE_THREADS
	mutex_lock(&hash_mutex);
#endif /* USE_THREADS */
	if (depth >= hash->depth) {
		hash->depth = depth;
		hash->key = key;
		hash->nnodes = nnodes;
	}
#ifdef USE_THREADS
	mutex_unlock(&hash_mutex);
#endif /* USE_THREADS */
}

static U64
perft(Board *board, int depth, PerftHash *hash)
{
	int i;
	U64 nnodes = 0;
	MoveLst move_list;
	
	ASSERT(2, board != NULL);
	ASSERT(2, hash != NULL);
	ASSERT(2, depth >= 0);

	if (depth == 0)
		return 1;

	if (depth > 1) {
		nnodes = probe_perft_hash(board->posp->key, depth, hash);
		if (nnodes > 0)
			return nnodes;
	}
	
	gen_moves(board, &move_list);
	if (depth == 1 || move_list.nmoves == 0)
		return move_list.nmoves;

	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = move_list.move[i];

		make_move(board, move);
		nnodes += perft(board, depth - 1 , hash);
		undo_move(board);
	}

	if (depth > 1)
		store_perft_hash(board->posp->key, nnodes, depth, hash);
	
	return nnodes;
}


#ifdef USE_THREADS
static THREAD_FUNC
threadfunc(THREAD_ARG data)
{
	PerftData *pd = (PerftData*)data;
	ASSERT(2, pd != NULL);

	while (true) {
		int index;
		Board *board;
		U32 move;
		char str_move[MAX_BUF];
		U64 nnodes;
		
		// look for work
		mutex_lock(&pd->job_mutex);
		if (pd->job_index >= pd->njobs) {
			mutex_unlock(&pd->job_mutex);
			return 0;
		}
		index = pd->job_index++;
		mutex_unlock(&pd->job_mutex);
		
		board = &pd->jobs[index].board;
		move = pd->jobs[index].move;
		move_to_str(move, str_move);
		make_move(board, move);
		
		nnodes = perft(board, pd->depth, pd->hash);
		
		mutex_lock(&pd->node_count_mutex);
		if (pd->divide)
			printf("%s %" PRIu64 "\n", str_move, nnodes);
		pd->nnodes += nnodes;
		mutex_unlock(&pd->node_count_mutex);
	}
	
	return 0;
}
#endif

U64
perft_root(Board *board, int depth, bool divide)
{
#ifdef USE_THREADS
	PerftData pd;
	thread_t *p_thread;
#endif /* USE_THREADS */

	int i;
	U64 nnodes = 0;
	MoveLst move_list;
	PerftHash *hash;
	
	ASSERT(2, board != NULL);
	ASSERT(2, depth >= 0);

	if (depth <= 0)
		return 0;

	gen_moves(board, &move_list);
	if (move_list.nmoves == 0)
		return 0;

	hash = calloc(PERFT_HASH_SIZE, sizeof(PerftHash));
	if (hash == NULL)
		fatal_perror("perft_root: Couldn't allocate memory");
	init_perft_hash(hash);

#ifdef USE_THREADS

	pd.nnodes = 0;
	pd.depth = depth - 1;
	pd.divide = divide;
	mutex_init(&hash_mutex);
	mutex_init(&pd.job_mutex);
	mutex_init(&pd.node_count_mutex);
	pd.job_index = 0;
	pd.njobs = move_list.nmoves;
	pd.jobs = calloc(pd.njobs, sizeof(PerftJob));
	
	for (i = 0; i < move_list.nmoves; i++) {
		PerftJob *job = pd.jobs + i;
		
		copy_board(&job->board, board);
		job->move = move_list.move[i];
	}
	
	p_thread = calloc(settings.nthreads, sizeof(thread_t));
	pd.hash = hash;

	for (i = 0; i < settings.nthreads; i++)
		t_create(threadfunc, (THREAD_ARG)&pd, &p_thread[i]);
	join_threads(p_thread, settings.nthreads);


	nnodes = pd.nnodes;
	free(p_thread);
	free(pd.jobs);
	mutex_destroy(&pd.node_count_mutex);
	mutex_destroy(&pd.job_mutex);
	mutex_destroy(&hash_mutex);

#else /* not USE_THREADS */

	for (i = 0; i < move_list.nmoves; i++) {
		U64 tmp_nnodes;
		char str_move[MAX_BUF];
		U32 move = move_list.move[i];

		move_to_str(move, str_move);
		make_move(board, move);
		
		/* Single threaded perft */
		tmp_nnodes = perft(board, depth - 1, hash);
		nnodes += tmp_nnodes;
		if (divide)
			printf("%s %" PRIu64 "\n", str_move, tmp_nnodes);

		undo_move(board);
	}

#endif /* not USE_THREADS */

	free(hash);
	return nnodes;
}
