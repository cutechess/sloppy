/* Sloppy - search.c
   Search functions like alpha-beta, quiescence, root search, etc.

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
#include "chess.h"
#include "debug.h"
#include "util.h"
#include "movegen.h"
#include "makemove.h"
#include "eval.h"
#include "hash.h"
#include "notation.h"
#include "input.h"
#include "search.h"
#include "egbb.h"


#define PAWN_THREAT(move) (GET_PC(move) == PAWN && prom_threat[GET_TO(move)])
#define MATE(ply) (-VAL_MATE+(ply))

#define FUT_MARGIN 100
#define NULL_R 3

#define BEST_SCORE 2000
#define KILLER_SCORE 4
#define TACTICAL_SCORE -150
#define CHECK_SCORE -1500
#define PASSED_PAWN_SCORE -1600
#define BAD_SCORE -24000


static const int prom_threat[64] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0
};

static U32 killer[MAX_PLY][2];


/* Static function prototypes.  */
static int search(Chess *chess, int alpha, int beta, int depth, bool in_pv, PvLine *pv);


/* Get the next best (highest move ordering score) move from a list.  */
static U32
get_next_move(MoveLst *move_list, int index)
{
	int i;
	int best_i = -1;
	int best_score = -VAL_INF;
	U32 best_move;
	
	ASSERT(2, move_list != NULL);
	ASSERT(2, index >= 0 && index < move_list->nmoves);

	for (i = index; i < move_list->nmoves; i++) {
		if (move_list->score[i] > best_score) {
			best_score = move_list->score[i];
			best_i = i;
		}
	}
	best_move = move_list->move[best_i];
	if (best_i > index) {
		move_list->move[best_i] = move_list->move[index];
		move_list->move[index] = best_move;
		move_list->score[best_i] = move_list->score[index];
		move_list->score[index] = best_score;
	}

	return best_move;
}

/* Give move ordering scores to the moves in a list.  */
static void
score_moves(const Board *board, U32 hash_move, int ply, MoveLst *move_list)
{
	int i;
	int *scorep;
	U32 move;
	
	ASSERT(2, board != NULL);
	ASSERT(2, move_list != NULL);

	for (i = 0; i < move_list->nmoves; i++) {
		move = move_list->move[i];
		scorep = &move_list->score[i];
		if (move == hash_move)
			*scorep = BEST_SCORE;
		else if (GET_CAPT(move) || GET_PROM(move))
			*scorep = see(board, move, board->color);
		//else if (GET_PROM(move))
		//	*scorep = pc_val[GET_PROM(move)] - VAL_PAWN;
		else if (IS_CHECK(move))
			*scorep = TACTICAL_SCORE;
		else if (move == killer[ply][0])
			*scorep = KILLER_SCORE;
		else if (move == killer[ply][1])
			*scorep = KILLER_SCORE - 1;
		else
			*scorep = BAD_SCORE;
	}
}

/* Give quiescence search move ordering scores to the moves in a list.  */
static void
score_qs_moves(const Board *board, MoveLst *move_list)
{
	int i;
	int *scorep;
	U32 move;
	
	ASSERT(2, board != NULL);
	ASSERT(2, move_list != NULL);

	for (i = 0; i < move_list->nmoves; i++) {
		move = move_list->move[i];
		scorep = &move_list->score[i];
		
		if (GET_CAPT(move) || GET_PROM(move)) {
			*scorep = see(board, move, board->color);
			if (*scorep <= -VAL_PAWN)
				*scorep = BAD_SCORE;
		} else if (IS_CHECK(move))
			*scorep = CHECK_SCORE;
		else
			*scorep = BAD_SCORE;
	}
}

/* Returns true if the side to move is in checkmate.  */
static bool
board_is_mate(const Board *board)
{
	MoveLst move_list;
	
	ASSERT(2, board != NULL);

	if (!board->posp->in_check)
		return false;
	gen_moves(board, &move_list);
	if (move_list.nmoves == 0)
		return true;
	return false;
}

/* Do mate distance pruning. The idea is that a mate in less than
   <ply> plies (from the root) must have been found in one of the previous
   iterations of the search, so we mustn't return a mate shorter than that.  */
static int
mate_distance_pruning(const Board *board, int *alpha, int *beta, int ply)
{
	int val;
	
	ASSERT(2, board != NULL);
	ASSERT(2, alpha != NULL);
	ASSERT(2, beta != NULL);

	/* Lower bound.  */
	val = MATE(ply + 2);
	if (val > *alpha && board_is_mate(board))
		val = MATE(ply);
	if (val > *alpha) {
		*alpha = val;
		if (val >= *beta)
			return val;
	}
	/* Upper bound.  */
	val = -MATE(ply + 1);
	if (val < *beta) {
		*beta = val;
		if (val <= *alpha)
			return val;
	}
	return 0;
}

/* Returns true if the side to move has enough material to win.  */
static bool
can_win(const Board *board)
{
	int color;
	
	ASSERT(2, board != NULL);
	
	color = board->color;
	if (!board->pcs[color][PAWN] && board->material[color] < VAL_ROOK)
		return false;

	return true;
}

/* Quiescence search. It's an alpha-beta search which only searches
   positions that aren't quiet, like captures. The goal is to return
   a quiescent evaluation, which is more reliable.  */
static int
qs_search(Chess *chess, int alpha, int beta, int depth)
{
	bool in_check;
	int val;
	int i;
	int ply;
	Board *board;
	MoveLst move_list;
	
	
	ASSERT(2, chess != NULL);
	ASSERT(2, alpha >= -VAL_INF);
	ASSERT(2, beta <= VAL_INF);
	ASSERT(2, alpha < beta);
	
	board = &chess->sboard;
	(chess->sd.nqs_nodes)++;

	if (beta > VAL_DRAW && !can_win(board)) {
		if (alpha >= VAL_DRAW)
			return VAL_DRAW;
		beta = VAL_DRAW;
	}

	ply = board->nmoves - chess->sd.root_ply;
	ASSERT(2, ply >= 1);

	val = mate_distance_pruning(board, &alpha, &beta, ply);
	if (val)
		return val;

	if (alpha < VAL_LIM_MATE && beta > -VAL_LIM_MATE) {
		val = probe_bitbases(board, ply, depth);
		if (val != VAL_NONE)
			return val;
	}

	if (ply >= (MAX_PLY - 1))
		return eval(board);

	in_check = board->posp->in_check;
	ASSERT(2, !in_check || depth < 0);

	/* Trust the static evaluation only when not in check.  */
	if (!in_check) {
		val = eval(board);
		if (val > alpha) {
			if (val >= beta)
				return beta;
			alpha = val;
		}

		if (depth >= 0) {
			gen_moves(board, &move_list);
			if (move_list.nmoves == 0)
				return VAL_DRAW;
		} else {
			gen_qs_moves(board, &move_list);
			/* Now we have a possible stalemate, but since this
		   	   is the quiescence search we just don't care.  */
			if (move_list.nmoves == 0)
				return alpha;
		}
	}
	/* If we're in check we should search all moves.  */
	else {
		gen_moves(board, &move_list);
		if (move_list.nmoves == 0)
			return MATE(ply);
		//depth++;
	}
	
	score_qs_moves(board, &move_list);
	for (i = 0; i < move_list.nmoves; i++) {
		U32 move = get_next_move(&move_list, i);
		if (!in_check && move_list.score[i] == BAD_SCORE)
			return alpha;

		make_move(board, move);
		val = -qs_search(chess, -beta, -alpha, depth - 1);
		undo_move(board);
		
		if (val > alpha) {
			if (val >= beta)
				return beta;
			alpha = val;
		}
	}

	return alpha;
}

/* Returns true if the new move is a forced retaliation (piece exchange) to the
   opponent's previous move.  */
static bool
is_recapture(const Board *board, U32 move, int score)
{
	U32 prev_move;
	int capt;
	
	ASSERT(2, board != NULL);
	ASSERT(2, move != NULLMOVE);
	
	prev_move = board->posp->move;
	if (score <= 0 || GET_TO(move) != GET_TO(prev_move))
		return false;

	capt = GET_CAPT(move);
	switch (GET_CAPT(prev_move)) {
	case PAWN:
		return (capt == PAWN);
	case KNIGHT: case BISHOP:
		return (capt == KNIGHT || capt == BISHOP);
	case ROOK:
		return (capt == ROOK);
	case QUEEN:
		return (capt == QUEEN);
	default:
		return false;
	}
}

/* Update the principal variation.  */
static void
update_pv(PvLine *pv, PvLine *new_pv, U32 move)
{
	if (pv == NULL)
		return;
	
	pv->moves[0] = move;
	memcpy(pv->moves + 1, new_pv->moves, new_pv->nmoves * sizeof(U32));
	pv->nmoves = new_pv->nmoves + 1;
}

static bool
null_move_pruning(Chess *chess, int beta, int *depth, bool in_pv)
{
	int val;
	Board *board;
	SearchData *sd;
	
	ASSERT(2, chess != NULL);

	board = &chess->sboard;
	if (in_pv
	||  board->posp->move == NULLMOVE
	||  board->posp->in_check
	||  *depth < 3
	||  is_mate_score(beta)
	||  board->material[board->color] <= VAL_KNIGHT
	||  eval(board) < beta)
		return false;

	make_nullmove(board);
	val = -search(chess, -beta, -beta + 1, *depth - NULL_R, false, NULL);
	undo_nullmove(board);
	
	sd = &chess->sd;
	if (sd->stop_search)
		return false;

	if (val >= beta) {
		int rply = sd->root_ply;
		int ply = board->nmoves - rply;
		int hval = val_to_hash(beta, ply);
		U64 key = board->posp->key;
		store_hash(*depth, hval, H_BETA, key, NULLMOVE, rply);
		return true;
	} else if (val < -VAL_LIM_MATE)
		(*depth)++;

	return false;
}

/* Internal Iterative Deepening (IID).  */
static U32
iid(Chess *chess, int alpha, int beta, int depth)
{
	int val;
	
	ASSERT(2, chess != NULL);
	ASSERT(2, depth > 0);

	val = search(chess, alpha, beta, depth, true, NULL);
	if (val <= alpha)
		val = search(chess, -VAL_INF, beta, depth, true, NULL);

	return get_hash_move(chess->sboard.posp->key);
}

/* Check for new input or timeup.  */
static bool
cancel_or_timeout(Chess *chess)
{
	S64 now;
	SearchData *sd;
	
	ASSERT(2, chess != NULL);
	
	sd = &chess->sd;

	now = get_ms();
	/* If we're past the first root move it's probably not going to take
	   long to complete the iteration. And if it does, we'll likely be
	   rewarded with a better move and score.  */
	if (now > sd->strict_deadline
	||  (now > sd->deadline && sd->nmoves_left == sd->nmoves)) {
		sd->stop_search = true;
		return true;
	}
	
	switch (input_available(chess)) {
	case CMDT_FINISH:
		sd->stop_search = true;
		sd->cmd_type = CMDT_FINISH;
		return true;
	case CMDT_CANCEL:
		sd->stop_search = true;
		sd->cmd_type = CMDT_CANCEL;
		return true;
	default:
		break;
	}
	
	return false;
}

static int
search(Chess *chess, int alpha, int beta, int depth, bool in_pv, PvLine *pv)
{
	Board *board;
	SearchData *sd;
	U64 key;
	int val;
	U32 best_move = NULLMOVE;
	bool in_check;
	bool avoid_null = false;
	MoveLst move_list;
	PvLine tmp_pv;
	PvLine *new_pv = NULL;
	int i;
	int orig_alpha;
	int ply;
	int best_val = -VAL_INF;
	int fut_score = VAL_INF;
	U32 hash_move;

	ASSERT(2, chess != NULL);
	ASSERT(2, alpha >= -VAL_INF);
	ASSERT(2, beta <= VAL_INF);
	ASSERT(2, alpha < beta);

	board = &chess->sboard;
	sd = &chess->sd;
	if (sd->stop_search)
		return VAL_NONE;
	if (pv != NULL) {
		pv->nmoves = 0;
		new_pv = &tmp_pv;
	}

	(sd->nnodes)++;
	if (sd->nnodes % 0x400 == 0 && cancel_or_timeout(chess))
		return VAL_NONE;

	key = board->posp->key;
	if (board->posp->fifty >= 100 || get_nrepeats(board, 1) > 0)
		return VAL_DRAW;

	if (beta > VAL_DRAW && !can_win(board)) {
		if (alpha >= VAL_DRAW)
			return VAL_DRAW;
		beta = VAL_DRAW;
	}

	ply = board->nmoves - sd->root_ply;
	ASSERT(2, ply >= 1);

	val = mate_distance_pruning(board, &alpha, &beta, ply);
	if (val)
		return val;

	/* Transposition table lookup.  */
	val = probe_hash(depth, alpha, beta, key, &best_move, ply);
	(sd->nhash_probes)++;
	switch (val) {
	case VAL_NONE:
		if (best_move != NULLMOVE)
			(sd->nhash_hits)++;
		break;
	case VAL_AVOID_NULL:
		(sd->nhash_hits)++;
		avoid_null = true;
		break;
	default:
		(sd->nhash_hits)++;
		if (!in_pv)
			return val;
		break;
	}

	if (alpha < VAL_LIM_MATE && beta > -VAL_LIM_MATE) {
		val = probe_bitbases(board, ply, depth);
		if (val != VAL_NONE)
			return val;
	}

	/* Quiescence search at leaf nodes.  */
	if (depth <= 0 || ply >= (MAX_PLY - 1))
		return qs_search(chess, alpha, beta, 0);

	/* Null-move pruning.  */
	if (!avoid_null && null_move_pruning(chess, beta, &depth, in_pv))
		return beta;
	else if (sd->stop_search)
		return VAL_NONE;

	ASSERT(2, depth > 0);
	in_check = board->posp->in_check;

	gen_moves(board, &move_list);
	if (move_list.nmoves == 0) {
		if (in_check)
			return MATE(ply);
		return VAL_DRAW;
	} else if (move_list.nmoves == 1) {
		depth++;
		best_move = move_list.move[0];
	}

	/* Internal Iterative Deepening (IID).  */
	if (depth >= 3 && in_pv && best_move == NULLMOVE) {
		best_move = iid(chess, alpha, beta, depth - 2);
		if (sd->stop_search)
			return VAL_NONE;
	}

	score_moves(board, best_move, ply, &move_list);

	orig_alpha = alpha;
	hash_move = best_move;

	for (i = 0; i < move_list.nmoves; i++) {
		bool reduced;
		int new_depth;
		U32 move = get_next_move(&move_list, i);
		bool extend = IS_CHECK(move) || PAWN_THREAT(move) ||
		              is_recapture(board, move, move_list.score[i]);
		bool tactical = extend || GET_CAPT(move) ||
		                is_passer_move(board, move);
		bool bad_score = (move_list.score[i] == BAD_SCORE);

		/* Futility pruning.  */
		if (depth < 3 && !in_check && !tactical && !in_pv
		&&  i > 0 && alpha < VAL_LIM_MATE && bad_score) {
			/* Optimistic evaluation.  */
			if (fut_score == VAL_INF) {
				fut_score = eval(board) + FUT_MARGIN * depth;
				ASSERT(2, val_is_ok(fut_score));
			}
			/* Prune the move if it seems to be bad enough.  */
			if (fut_score <= alpha)
				continue;
		}

		reduced = false;
		new_depth = depth - 1;

		make_move(board, move);

		if (extend)
			new_depth++;
		/* Late move reduction.  */
		else if (i > 2 && depth > 2 && !in_check
		     &&  !in_pv && !tactical && bad_score) {
			new_depth--;
			reduced = true;
		}

		if (!in_pv || best_val == -VAL_INF)
			val = -search(chess, -beta, -alpha, new_depth, in_pv, new_pv);
		else {
			val = -search(chess, -alpha - 1, -alpha, new_depth, false, NULL);
			if (val > alpha && val < beta)
				val = -search(chess, -beta, -alpha, new_depth, true, new_pv);
		}
		
		/* Late move reduction re-search.  */
		if (reduced && val >= beta) {
			new_depth++;
			val = -search(chess, -beta, -alpha, new_depth, in_pv, new_pv);
		}
		undo_move(board);

		if (sd->stop_search)
			return VAL_NONE;

		/* Fail high.  */
		if (val >= beta) {
			/* Update killer moves.  */
			if (!in_check && !tactical && move != killer[ply][0]) {
				killer[ply][1] = killer[ply][0];
				killer[ply][0] = move;
			}

			store_hash(depth, val_to_hash(beta, ply), H_BETA,
			           key, move, sd->root_ply);
			return beta;
		}
		if (val > best_val) {
			best_val = val;
			best_move = move;
			if (val > alpha) {
				alpha = val;
				update_pv(pv, new_pv, move);
			}
		}
	}

	/* Fail low.  */
	if (alpha <= orig_alpha)
		store_hash(depth, val_to_hash(alpha, ply), H_ALPHA,
		           key, best_move, sd->root_ply);
	else	/* Exact score.  */
		store_hash(depth, val_to_hash(alpha, ply), H_EXACT,
		           key, best_move, sd->root_ply);

	return alpha;
}

static int
search_root(Chess *chess, int depth, U32 *movep)
{
	int val;
	int i;
	int alpha = -VAL_INF;
	int beta = VAL_INF;
	U32 best_move;
	U64 key;
	Board *board;
	SearchData *sd;
	MoveLst move_list;
	PvLine new_pv;

	ASSERT(1, chess != NULL);
	ASSERT(1, movep != NULL);
	ASSERT(1, depth > 0);

	best_move = NULLMOVE;
	board = &chess->sboard;
	sd = &chess->sd;

	sd->nnodes = 1;
	sd->nqs_nodes = 0;
	sd->nhash_probes = 0;
	sd->nhash_hits = 0;
	sd->nmoves = 0;
	sd->nmoves_left = 0;
	key = board->posp->key;

	/* get the best move from the hash table */
	if (*movep != NULLMOVE)
		best_move = *movep;
	else {
		best_move = get_hash_move(key);
		(sd->nhash_probes)++;
		if (best_move != NULLMOVE)
			(sd->nhash_hits)++;
	}

	gen_moves(board, &move_list);
	sd->nmoves = move_list.nmoves;
	score_moves(board, best_move, 0, &move_list);

	for (i = 0; i < move_list.nmoves; i++) {
		bool extend;
		int new_depth;
		U32 move = get_next_move(&move_list, i);

		sd->nmoves_left = move_list.nmoves - i;
		move_to_san(sd->san_move, board, move);
		make_move(board, move);
		
		extend = IS_CHECK(move) || PAWN_THREAT(move);
		new_depth = depth - 1;
		
		if (extend)
			new_depth++;

		if (i == 0)
			val = -search(chess, -beta, -alpha, new_depth, true, &new_pv);
		else {
			val = -search(chess, -alpha - 1, -alpha, new_depth, false, NULL);
			if (val > alpha && val < beta)
				val = -search(chess, -beta, -alpha, new_depth, true, &new_pv);
		}
		undo_move(board);

		if (sd->stop_search && *movep != NULLMOVE && i > 0) {
			*movep = best_move;
			store_hash(depth, val_to_hash(alpha, 0), H_BETA,
			           key, best_move, sd->root_ply);
		}
		if (sd->stop_search)
			return VAL_NONE;

		ASSERT(1, val < beta);
		if (val > alpha) {
			alpha = val;
			best_move = move;
			update_pv(&sd->pv, &new_pv, move);
		}
	}

	*movep = best_move;
	store_hash(depth, val_to_hash(alpha, 0), H_EXACT,
	           key, best_move, sd->root_ply);

	if (get_ms() > sd->deadline)
		sd->strict_deadline = sd->deadline;

	return alpha;
}

static void
init_killers(void)
{
	int ply;

	for (ply = 0; ply < MAX_PLY; ply++) {
		killer[ply][0] = NULLMOVE;
		killer[ply][1] = NULLMOVE;
	}
}

/* Print the principal variation.  */
static void
print_pv(const Chess *chess, int depth, int score, U64 nnodes)
{
	int i;
	int t_elapsed;
	Board tmp_board;
	const PvLine *pv;

	ASSERT(1, chess != NULL);

	pv = &chess->sd.pv;
	t_elapsed = (int)(get_ms() - chess->sd.t_start);
	if (chess->protocol == PROTO_NONE) {
		int minutes = t_elapsed / 60000;
		int seconds = (t_elapsed % 60000) / 1000;
		printf("%2d  ", depth);
		if (score >= 0)
			printf("+");
		printf("%.2f  ", (double)score / 100.0);
		printf("%.2d:%.2d  ", minutes, seconds);
		printf("%10" PRIu64 " ", nnodes);
	} else if (chess->protocol == PROTO_XBOARD) {
		int csec = t_elapsed / 10;
		printf("%d %d %d %" PRIu64, depth, score, csec, nnodes);
	}
	
	copy_board(&tmp_board, &chess->board);
	for (i = 0; i < depth; i++) {
		U32 move;
		char san_move[MAX_BUF];
		
		if (i < pv->nmoves)
			move = pv->moves[i];
		else
			/* If the pv isn't long enough (i.e. because a forced
			   mate was found) we try to find the moves by looking
			   inside the hash table. */
			move = get_hash_move(tmp_board.posp->key);

		/* It's very rare, but possible, that both the PvLine and the
		   hash table fail to provide us a complete pv.  */
		if (!move)
			break;

		move_to_san(san_move, &tmp_board, move);
		printf(" %s", san_move);

		make_move(&tmp_board, move);
	}
	printf("\n");
}

/* Decide how long Sloppy is allowed to think of his next move.  */
static void
allocate_time(Chess *chess)
{
	int limit = 0;
	int time_left = 0;
	S64 deadline = 0;
	S64 strict_deadline = 0;
	S64 tc_end;
	SearchData *sd;
	
	ASSERT(1, chess != NULL);
	
	sd = &chess->sd;
	sd->t_start = get_ms();
	sd->stop_search = false;
	tc_end = chess->tc_end - 800;
	if (tc_end < 0)
		tc_end = 0;

	/* In analyze mode there is no time limit, so we'll just
	   use an insanely big value to fake it.  */
	if (chess->analyze) {
		sd->deadline = INT64_MAX;
		sd->strict_deadline = INT64_MAX;
		return;
	}
	
	if (tc_end > 0)
		time_left = (int)(tc_end - sd->t_start);
	if (chess->nmoves_per_tc > 0) {
		int nmoves;
		nmoves = (chess->board.nmoves / 2) % chess->nmoves_per_tc;
		nmoves = chess->nmoves_per_tc - nmoves;
		ASSERT(1, nmoves > 0);
		limit = time_left / nmoves;
	} else
		limit = time_left / 45;

	/* If the last move was a book move Sloppy may not immediately
	   understand the position, so more time is needed.  */
	if (chess->in_book)
		limit *= 2;

	deadline = sd->t_start + limit + chess->increment;
	strict_deadline = sd->t_start + (limit * 6) + chess->increment;
	
	if (tc_end > 0 && strict_deadline > tc_end)
		strict_deadline = tc_end;

	sd->deadline = deadline;
	sd->strict_deadline = strict_deadline;
}

/* Iterative deepening search.
   If test_move != NULLMOVE then it's the solution to a test position.  */
int
id_search(Chess *chess, U32 test_move)
{
	U32 move = NULLMOVE;
	int val = 0;
	int depth;
	int last_depth = 0;
	int last_score = 0;
	U64 last_nnodes = 0;
	U64 total_nnodes = 0;
	U64 total_nqs_nodes = 0;
	U64 nhash_probes = 0;
	U64 nhash_hits = 0;
	Board *board;
	SearchData *sd;

	ASSERT(1, chess != NULL);

	allocate_time(chess);
	
	board = &chess->board;
	copy_board(&chess->sboard, board);
	sd = &chess->sd;
	sd->cmd_type = CMDT_CONTINUE;
	sd->root_ply = board->nmoves;
	sd->move = NULLMOVE;

	init_killers();
	for (depth = 1; depth <= chess->max_depth; depth++) {
		sd->ply = depth;
		val = search_root(chess, depth, &move);
		total_nqs_nodes += sd->nqs_nodes;
		nhash_probes += sd->nhash_probes;
		nhash_hits += sd->nhash_hits;
		if (sd->stop_search)
			break;
		last_nnodes = total_nnodes;
		last_score = val;
		last_depth = depth;
		total_nnodes += sd->nnodes;
		if (chess->show_pv && depth > 1) {
			U64 nall_nodes = total_nnodes + total_nqs_nodes;
			print_pv(chess, depth, val, nall_nodes);
		}
		if (move != NULLMOVE && move == test_move)
			break;
	}

	if (last_nnodes > 0)
		sd->bfactor = (double)total_nnodes / (double)last_nnodes;
	else
		sd->bfactor = (double)total_nnodes;
	if (sd->stop_search)
		total_nnodes += sd->nnodes;
	sd->nnodes = total_nnodes;
	sd->nqs_nodes = total_nqs_nodes;
	sd->nhash_probes = nhash_probes;
	sd->nhash_hits = nhash_hits;
	sd->move = move;

	return SIGN(board->color)*last_score;
}

