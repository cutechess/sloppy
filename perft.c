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

#define PERFT_HASH_SIZE 0x200000

typedef struct _PerftHash
{
	int depth;
	U64 nnodes;
	U64 key;
} PerftHash;


#ifdef USE_THREADS

#include <pthread.h>

typedef struct _PerftData
{
	Board *board;
	PerftHash *hash;
	char *str_move;
	U64 nnodes;
	int depth;
	int nidle;		/* num. of idle threads */
	int last_id;		/* id of the thread that was created last */
	bool all_done;		/* perft is complete */
	bool divide;		/* print each root move's node count */
} PerftData;

static PerftData pd;

/* Condition variables.  */
static pthread_cond_t new_job_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t got_job_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t ready_cv = PTHREAD_COND_INITIALIZER;

/* Mutexes.  */
static pthread_mutex_t pd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t new_job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t got_job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t hash_mutex = PTHREAD_MUTEX_INITIALIZER;

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

#ifdef USE_THREADS
	pthread_mutex_lock(&hash_mutex);
#endif /* USE_THREADS */
	hash = &hash[key % PERFT_HASH_SIZE];

	if (depth >= hash->depth) {
		hash->depth = depth;
		hash->key = key;
		hash->nnodes = nnodes;
	}
#ifdef USE_THREADS
	pthread_mutex_unlock(&hash_mutex);
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
static void *
smp_perft(void *data)
{
	int id;
	int depth;
	U64 nnodes;
	Board board;
	char str_move[MAX_BUF];

	/* Pthreads for Windows don't work as they're supposed to here, so we
	   can't make any assumptions on what (if anything) is in *data.  */
	//ASSERT(1, data == NULL);

	id = pd.last_id;

	pthread_mutex_lock(&pd_mutex);
	pd.last_id++;
	pthread_mutex_unlock(&pd_mutex);

	while (!pd.all_done) {
		/* Tell that you're idle and wait for a job.  */
		pthread_mutex_lock(&new_job_mutex);
		pd.nidle++;
		pthread_mutex_lock(&ready_mutex);
		pthread_cond_signal(&ready_cv);
		pthread_mutex_unlock(&ready_mutex);
		pthread_cond_wait(&new_job_cv, &new_job_mutex);
		pthread_mutex_unlock(&new_job_mutex);

		/* Getting the new_job signal could also mean that all work
		   is done. Check <all_done> to see if that's the case.  */
		if (pd.all_done)
			pthread_exit(NULL);
		
		/* Get the job.  */
		copy_board(&board, pd.board);
		strlcpy(str_move, pd.str_move, MAX_BUF);
		depth = pd.depth;

		/* Give new_request() some time.  */
		sched_yield();
		
		/* Tell new_request() that you got the job.  */
		pthread_mutex_lock(&got_job_mutex);
		pthread_cond_signal(&got_job_cv);
		pthread_mutex_unlock(&got_job_mutex);

		/* Get the perft score.  */
		nnodes = perft(&board, depth, pd.hash);

		pthread_mutex_lock(&pd_mutex);
		if (pd.divide)
			printf("%d: %s %" PRIu64 "\n", id, str_move, nnodes);
		pd.nnodes += nnodes;
		pthread_mutex_unlock(&pd_mutex);
	}

	pthread_exit(NULL);
	return NULL;
}

static void
init_threads(pthread_t *pthread)
{
	int i;

	ASSERT(1, pthread != NULL);
	
	pd.depth = 0;
	pd.nnodes = 0;
	pd.last_id = 0;
	pd.nidle = 0;
	pd.divide = false;
	pd.all_done = false;

	for (i = 0; i < settings.nthreads; i++) {
		int rc = pthread_create(&pthread[i], NULL, smp_perft, NULL);
		if (rc != 0)
			fatal_error("Can't create thread: %s", strerror(rc));
	}
}

static void
new_request(const Board *board, int depth, const char *str_move)
{
	ASSERT(1, board != NULL);
	ASSERT(1, str_move != NULL);
	ASSERT(1, depth > 0);

	/* Prepare a job.  */
	pthread_mutex_lock(&pd_mutex);
	pd.board = board;
	pd.depth = depth;
	pd.str_move = str_move;
	pthread_mutex_unlock(&pd_mutex);

	/* Check if there are any idle threads. If not, wait for one
	   to become ready.  */
	if (pd.nidle <= 0) {
		pthread_mutex_lock(&ready_mutex);
		pthread_cond_wait(&ready_cv, &ready_mutex);
		pthread_mutex_unlock(&ready_mutex);
	}
	
	/* Offer a job.  */
	pthread_mutex_lock(&new_job_mutex);
	pd.nidle--;
	pthread_cond_signal(&new_job_cv);
	pthread_mutex_unlock(&new_job_mutex);

	/* Wait until a thread gets the job.  */
	pthread_mutex_lock(&got_job_mutex);
	pthread_cond_wait(&got_job_cv, &got_job_mutex);
	pthread_mutex_unlock(&got_job_mutex);
}

void
thread_cleanup(void)
{
	/* Destroy mutexes.  */
	pthread_mutex_destroy(&pd_mutex);
	pthread_mutex_destroy(&new_job_mutex);
	pthread_mutex_destroy(&got_job_mutex);
	pthread_mutex_destroy(&ready_mutex);
	pthread_mutex_destroy(&hash_mutex);

	/* Destroy condition variables.  */
	pthread_cond_destroy(&new_job_cv);
	pthread_cond_destroy(&got_job_cv);
	pthread_cond_destroy(&ready_cv);
}

#endif

U64
perft_root(Board *board, int depth, bool divide)
{
	int i;
	char str_move[MAX_BUF];
	U64 nnodes = 0;
#ifdef USE_THREADS
	pthread_t *p_thread;
#else /* not USE_THREADS */
	U64 tmp_nnodes;
#endif /* not USE_THREADS */
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
	p_thread = calloc(settings.nthreads, sizeof(pthread_t));
	pd.hash = hash;
	init_threads(p_thread);
	pd.divide = divide;
#endif /* USE_THREADS */

	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = move_list.move[i];

		move_to_san(str_move, board, move);
		make_move(board, move);
		
#ifdef USE_THREADS
		/* Parallel perft */
		new_request(board, depth - 1, str_move);
#else /* not USE_THREADS */
		/* Single threaded perft */
		tmp_nnodes = perft(board, depth - 1, hash);
		nnodes += tmp_nnodes;
		if (divide)
			printf("%s %" PRIu64 "\n", str_move, tmp_nnodes);
#endif /* not USE_THREADS */
		undo_move(board);
	}
#ifdef USE_THREADS

	pthread_mutex_lock(&pd_mutex);
	pd.all_done = true;
	pthread_mutex_unlock(&pd_mutex);

	/* Make sure no one's waiting for a job.  */
	pthread_mutex_lock(&new_job_mutex);
	pthread_cond_broadcast(&new_job_cv);
	pthread_mutex_unlock(&new_job_mutex);

	for (i = 0; i < settings.nthreads; i++) {
		int rc = pthread_join(p_thread[i], NULL);
		if (rc != 0)
			fatal_error("Can't join thread: %s", strerror(rc));
	}

	nnodes = pd.nnodes;
	free(p_thread);
#endif /* USE_THREADS */
	free(hash);
	return nnodes;
}

