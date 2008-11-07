/* Sloppy - chess.c
   Functions for initializing some of Sloppy's data structures

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

#include "chess.h"
#include "debug.h"
#include "util.h"


void
init_search_data(SearchData *sd)
{
	int i;
	
	ASSERT(1, sd != NULL);
	
	for (i = 0; i < MAX_PLY; i++)
		sd->pv.moves[i] = NULLMOVE;
	sd->stop_search = false;
	sd->cmd_type = CMDT_CONTINUE;
	sd->ply = 0;
	sd->nmoves = 0;
	sd->nmoves_left = 0;
	sd->nnodes = 0;
	sd->nqs_nodes = 0;
	sd->nhash_hits = 0;
	sd->nhash_probes = 0;
	sd->t_start = 0;
	sd->bfactor = 0.0;
	sd->move = NULLMOVE;
	strlcpy(sd->san_move, "", MAX_BUF);
}

void
init_chess(Chess *chess)
{
	ASSERT(1, chess != NULL);

	init_search_data(&chess->sd);
	chess->book = NULL;
	chess->protocol = PROTO_NONE;
	chess->cpu_color = COLOR_NONE;
	chess->max_depth = 64;
	chess->max_time = 0;
	chess->tc_end = 0;
	chess->increment = 0;
	chess->nmoves_per_tc = 0;
	strlcpy(chess->op_name, "", MAX_BUF);
	chess->in_book = false;
	chess->debug = false;
	chess->game_over = false;
	chess->show_pv = false;
	chess->analyze = false;
}

/* Print some details about the last search.  */
void
print_search_data(const SearchData *sd, int t_elapsed)
{
	double hhit_rate;
	U64 nnodes;
	
	ASSERT(1, sd != NULL);
	
	hhit_rate = (double)sd->nhash_hits / (double)sd->nhash_probes;
	nnodes = sd->nnodes + sd->nqs_nodes;

	if (t_elapsed > 0) {
		double sec_elapsed = (double)t_elapsed / 1000.0;
		int nps = (int)((double)nnodes / sec_elapsed);
		printf("Time elapsed: %.2f seconds.\n", sec_elapsed);
		printf("Total nodes per second: %d\n", nps);
	}
	printf("Main nodes searched: %" PRIu64 "\n", sd->nnodes);
	printf("Quiescence nodes searched: %" PRIu64 "\n", sd->nqs_nodes);
	printf("Hash table hit rate: %.2f%%\n", hhit_rate * 100.0);
	printf("Branching factor: %.2f\n", sd->bfactor);
}

