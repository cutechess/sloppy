/* Sloppy - bench.c
   Functions for benchmarking Sloppy's performance and strength

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
#include "notation.h"
#include "search.h"
#include "bench.h"


/* Run a test position (eg. WAC, ECM, WCSAC).

   Format of pos: <FEN> <"bm" or "am"> <move>; <position id>;
   - "bm" means <move> is the best move, "am" means <move> should be avoided
   - the move is in SAN notation
   - position id can be omitted
   - example: R7/P4k2/8/8/8/8/r7/6K1 w - - bm Rg8; id "WAC.018";
   
   The time limit must be defined in <chess> before running the test.

   Returns -1 on error
            0 for unsolved test
            1 for solved test
            2 for cancelled test  */
int
test_pos(Chess *chess, const char *pos)
{
	char tmp_pos[MAX_BUF];
	char *pos_item = NULL;
	char *svptr = NULL;
	U32 move;
	bool find_best;

	strlcpy(tmp_pos, pos, MAX_BUF);
	
	/* See if we're looking for the best move or avoiding a move.
	   By doing that we'll also get to the end of the FEN string.  */
	if ((pos_item = strstr(tmp_pos, " bm ")) != NULL)
		find_best = true;
	else if ((pos_item = strstr(tmp_pos, " am ")) != NULL) {
		find_best = false;
		my_error("'Avoid move' positions not currently allowed");
		return -1;
	} else
		return -1;

	*pos_item = 0; /* end of FEN string */
	pos_item += 4; /* best move or move to avoid */
	
	if (fen_to_board(&chess->board, tmp_pos))
		return -1;

	/* Get the move string.  */
	if ((pos_item = strtok_r(pos_item, ";", &svptr)) == NULL)
		return -1;
	/* Some positions have a series of moves (a pv) as the solution. We
	   only care about the first one, so make sure to pick just one.  */
	svptr = strchr(pos_item, ' ');
	if (svptr != NULL)
		*svptr = '\0';
	
	move = san_to_move(&chess->board, pos_item);
	if (move == NULLMOVE) {
		printf("Illegal test solution: %s\n", pos_item);
		return -1;
	}
	id_search(chess, move);
	if (chess->sd.cmd_type != CMDT_CONTINUE)
		return 2;
	if (chess->sd.move == move)
		return 1;

	return 0;
}

/* Run a test suite like:
   - Win at Chess (WAC)
   - Winning Chess Sacrifices and Combinations (WCSAC)
   - Encyclopedia of Chess Middlegames (ECM)
   
   The tests in the suite must be in the format test_pos() uses.  */
void
test_suite(Chess *chess, const char *filename)
{
	S64 timer;
	int npos = 0;
	int nsolved = 0;
	int nfailed = 0;
	char pos[MAX_BUF];
	SearchData *sd;
	SearchData sd_total;
	FILE *fp;

	if ((fp = fopen(filename, "r")) == NULL) {
		my_perror("Can't open file %s", filename);
		return;
	}

	sd = &chess->sd;
	init_search_data(&sd_total);

	printf("Running test suite...\n");
	timer = get_ms();
	while (fgetline(pos, MAX_BUF, fp) != EOF) {
		if (strlen(pos) <= 1)
			continue;
		npos++;
		printf("%d.: ", npos);
		switch (test_pos(chess, pos)) {
		case -1:
			printf("Invalid test position: %s\n", pos);
			continue;
		case 0:
			printf("Couldn't solve test: %s\n", pos);
			nfailed++;
			break;
		case 1:
			printf("Solved test: %s\n", pos);
			nsolved++;
			break;
		case 2:
			printf("Test suite cancelled by user\n");
			my_close(fp, filename);
			return;
		}
		sd_total.nnodes += sd->nnodes;
		sd_total.nqs_nodes += sd->nqs_nodes;
		sd_total.nhash_hits += sd->nhash_hits;
		sd_total.nhash_probes += sd->nhash_probes;
		sd_total.bfactor += sd->bfactor;
	}
	my_close(fp, filename);

	sd_total.bfactor /= nsolved + nfailed;
	timer = get_ms() - timer;
	printf("\n");
	print_search_data(&sd_total, (int)timer);
	printf("\n%d of %d tests were solved.\n", nsolved, nsolved + nfailed);
}

static const char *bench_fen[] =
{
	"1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - 0 1",
	"r1k5/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - - 0 1",
	"q1rr1k2/3bbnnp/p2p1pp1/2pPp3/PpP1P1P1/1P2BNNP/2BQ1PRK/7R b - - 0 1",
	"nbqkb1r1/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQ - 0 1",
	"r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - - 0 1",
	"2r3k1/pppR1pp1/4p3/4P1P1/5P2/1P4K1/P1P5/8 w - - 0 1",
	"1nk1r1r1/pp2n1pp/4p3/q2pPp1N/b1pP1P2/B1P2R2/2P1B1PP/R2Q2K1 w - - 0 1",
	"4b3/p3kp2/6p1/3pP2p/2pP1P2/4K1P1/P3N2P/8 w - - 0 1",
	"2kr1bnr/pbpq4/2n1pp2/3p3p/3P1P1B/2N2N1Q/PPP3PP/2KR1B1R w - - 0 1",
	"3rr1k1/pp3pp1/1qn2np1/8/3p4/PP1R1P2/2P1NQPP/R1B3K1 b - - 0 1",
	"2r1nrk1/p2q1ppp/bp1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - - 0 1",
	"r3r1k1/ppqb1ppp/8/4p1NQ/8/2P5/PP3PPP/R3R1K1 b - - 0 1",
	"r2q1rk1/4bppp/p2p4/2pP4/3pP3/3Q4/PP1B1PPP/R3R1K1 w - - 0 1",
	"rnb2r1k/pp2p2p/2pp2p1/q2P1p2/8/1Pb2NP1/PB2PPBP/R2Q1RK1 w - - 0 1",
	"2r3k1/1p2q1pp/2b1pr2/p1pp4/6Q1/1P1PP1R1/P1PN2PP/5RK1 w - - 0 1",
	"r1bqkb1r/4npp1/p1p4p/1p1pP1B1/8/1B6/PPPN1PPP/R2Q1RK1 w kq - 0 1",
	"r1bq1rk1/pp2ppbp/2np2p1/2n5/P3PP2/N1P2N2/1PB3PP/R1B1QRK1 b - - 0 1",
	"3rr3/2pq2pk/p2p1pnp/8/2QBPP2/1P6/P5PP/4RRK1 b - - 0 1",
	"r4k2/pb2bp1r/1p1qp2p/3pNp2/3P1P2/2N3P1/PPP1Q2P/2KRR3 w - - 0 1",
	"3rn2k/ppb2rpp/2ppqp2/5N2/2P1P3/1P5Q/PB3PPP/3RR1K1 w - - 0 1",
	"2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - - 0 1",
	"r1bqk2r/pp2bppp/2p5/3pP3/P2Q1P2/2N1B3/1PP3PP/R4RK1 b kq - 0 1",
	"r2qnrnk/p2b2b1/1p1p2pp/2pPpp2/1PP1P3/PRNBB3/3QNPPP/5RK1 w - - 0 1",
	"r2q1rk1/1ppnbppp/p2p1nb1/3Pp3/2P1P1P1/2N2N1P/PPB1QP2/R1B2RK1 b - - 0 1"
};

/* Benchmark Sloppy's speed, branching factor, and hash table efficiency.  */
#define BENCH_FILE "bench.txt"
void
bench(void)
{
	Chess chess;
	SearchData sd;
	S64 timer;
	int npos = 0;
	int nfen = (int)(sizeof(bench_fen) / sizeof(char*));
	
	double avg_bfactor;
	double t_elapsed;
	double hhit_rate;
	U64 nnodes_all;	/* num. of all nodes (main + qs) */
	int nps;	/* nodes (all types) per second */
	int i;
	
	init_chess(&chess);
	init_search_data(&sd);
	chess.max_depth = 8;
	chess.increment = 60000;

	printf("Running benchmark at search depth %d...\n", chess.max_depth);
	timer = get_ms();
	progressbar(nfen, 0);
	for (i = 0; i < nfen; i++) {
		const char *fen = bench_fen[i];
		if (strlen(fen) <= 1)
			continue;
		if (!fen_to_board(&chess.board, fen)) {
			id_search(&chess, NULLMOVE);
			if (chess.sd.cmd_type != CMDT_CONTINUE) {
				printf("Benchmark cancelled by user\n");
				return;
			}

			sd.nnodes += chess.sd.nnodes;
			sd.nqs_nodes += chess.sd.nqs_nodes;
			sd.nhash_probes += chess.sd.nhash_probes;
			sd.nhash_hits += chess.sd.nhash_hits;
			sd.bfactor += chess.sd.bfactor;
			npos++;
			progressbar(nfen, npos);
		} else
			printf("\nInvalid FEN string: %s\n", fen);
	}

	timer = get_ms() - timer;
	t_elapsed = timer / 1000.0;
	avg_bfactor = sd.bfactor / npos;
	hhit_rate = (sd.nhash_hits * 100.0) / sd.nhash_probes;
	nnodes_all = sd.nnodes + sd.nqs_nodes;
	nps = (double)nnodes_all / ((double)timer / 1000.0);
	printf("\n\nBenchmark finished in %.2f seconds.\n", t_elapsed);
	printf("Main nodes searched: %" PRIu64 "\n", sd.nnodes);
	printf("Quiescence nodes searched: %" PRIu64 "\n", sd.nqs_nodes);
	printf("Total nodes per second: %d\n", nps);
	printf("Average branching factor: %.2f\n", avg_bfactor);
	printf("Hash table hit rate: %.2f%%\n", hhit_rate);
}

