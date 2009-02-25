/* Sloppy - egbb.c
   Wrapper functions for loading and probing Daniel Shawul's Scorpio bitbases.
   Based on probe.cpp from Scorpio 2.0, Copyright (c) 2007, Daniel Shawul.

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

#include "egbb.h"
#include "util.h"
#include "debug.h"

#ifdef WINDOWS
  #include <windows.h>
  #include <io.h>
  #include <conio.h>
  
  #undef CDECL
  #define CDECL __cdecl
  #define LIB_HND HMODULE
  #define LOAD_LIB(x) LoadLibrary(x)
  #define LOAD_LIB_SYM GetProcAddress
  #define UNLOAD_LIB FreeLibrary
  #define EGBB_NAME "egbbdll.dll"
#else /* not WINDOWS */
  #include <dlfcn.h>
  
  #define CDECL
  #define LIB_HND void*
  #define LOAD_LIB(x) dlopen((x), RTLD_LAZY | RTLD_LOCAL)
  #define LOAD_LIB_SYM dlsym
  #define UNLOAD_LIB dlclose
  #define EGBB_NAME "egbbso.so"
#endif /* not WINDOWS */


enum { _WHITE, _BLACK };
enum { _EMPTY, _WKING, _WQUEEN, _WROOK, _WBISHOP, _WKNIGHT, _WPAWN,
       _BKING, _BQUEEN, _BROOK, _BBISHOP, _BKNIGHT, _BPAWN };


/* The bitbase library's function types.  */
typedef int (CDECL *PPROBE_EGBB) (int color, int wking, int bking, int pc1,
                                  int sq1, int pc2, int sq2, int pc3, int sq3);

typedef void (CDECL *PLOAD_EGBB) (const char *path, int cache_size,
                                  int load_options);
static PPROBE_EGBB probe_egbb = NULL;

static LIB_HND egbb_hnd = NULL;
static bool bitbases_loaded = false;


/* Load the dll and get the address of the load and probe functions.  */
bool
load_bitbases(void)
{
	PLOAD_EGBB load_egbb;
	char path[MAX_BUF];
	
	const char *main_path = settings.egbb_path;
	int cache_size = (int)settings.egbb_cache_size;
	EgbbLoadType load_type = settings.egbb_load_type;

	ASSERT(1, main_path != NULL);
	ASSERT(1, cache_size >= 0);
	ASSERT(1, load_type != EGBB_OFF);
	
	if (bitbases_loaded) {
		my_error("Bitbases are already loaded");
		return false;
	}

	strlcpy(path, main_path, MAX_BUF);
	strlcat(path, EGBB_NAME, MAX_BUF);

	egbb_hnd = LOAD_LIB(path);
	if (!egbb_hnd) {
		my_error("Can't load egbb library %s", path);
		return false;
	}

	/* ISO C forbids these conversions of object pointer to function
	   pointer, but it should work fine on any system Sloppy runs on.  */
	load_egbb = (PLOAD_EGBB)LOAD_LIB_SYM(egbb_hnd, "load_egbb_5men");
	if (load_egbb == NULL) {
		unload_bitbases();
		my_error("Can't find bitbase load function");
		return false;
	}
	probe_egbb = (PPROBE_EGBB)LOAD_LIB_SYM(egbb_hnd, "probe_egbb_5men");
	if (probe_egbb == NULL) {
		unload_bitbases();
		my_error("Can't find bitbase probe function");
		return false;
	}

	load_egbb(main_path, cache_size, load_type);
	bitbases_loaded = true;
	return true;
}

/* Unload the endgame bitbase object/library.  */
void
unload_bitbases(void)
{
	if (egbb_hnd == NULL)
		return;

	UNLOAD_LIB(egbb_hnd);
	egbb_hnd = NULL;
	probe_egbb = NULL;
	bitbases_loaded = false;
}

/* Probe the endgame bitbases for a result.
   Returns VAL_NONE if the position is not found.  */
#define _NOTFOUND 99999
int
probe_bitbases(const Board *board, int ply, int depth)
{
	int val;
	int sq;
	int n = 0;
	int wking;
	int bking;
	int npcs;
	U32 move;
	U64 whites;
	U64 blacks;
	int pcs[3] = { 0, 0, 0 };
	int sqs[3] = { 0, 0, 0 };
	static const int pc_table[2][7] = {
		{ _EMPTY, _WPAWN, _WKNIGHT, _WBISHOP, _WROOK, _WQUEEN, _WKING },
		{ _EMPTY, _BPAWN, _BKNIGHT, _BBISHOP, _BROOK, _BQUEEN, _BKING }
	};
	static const int flip[64] =
	{
		56,  57,  58,  59,  60,  61,  62,  63,
		48,  49,  50,  51,  52,  53,  54,  55,
		40,  41,  42,  43,  44,  45,  46,  47,
		32,  33,  34,  35,  36,  37,  38,  39,
		24,  25,  26,  27,  28,  29,  30,  31,
		16,  17,  18,  19,  20,  21,  22,  23,
		 8,   9,  10,  11,  12,  13,  14,  15,
		 0,   1,   2,   3,   4,   5,   6,   7
	};

	ASSERT(2, board != NULL);
	ASSERT(2, ply > 0);

	if (!bitbases_loaded)
		return VAL_NONE;

	npcs = popcount(board->all_pcs);
	ASSERT(2, npcs > 2);
	ASSERT(2, settings.egbb_max_men <= 5);
	if (npcs > settings.egbb_max_men)
		return VAL_NONE;

	if (depth <= 0) {
		switch (settings.egbb_load_type) {
		case LOAD_NONE:
			return VAL_NONE;
		case LOAD_4MEN: case SMART_LOAD:
			if (npcs > 4)
				return VAL_NONE;
			break;
		case LOAD_5MEN:
			break;
		default:
			fatal_error("Invalid egbb load type");
		}
	}

	move = board->posp->move;
	if (ply < ((2 * (depth + ply)) / 3)
	&&  GET_CAPT(move) == 0 && GET_PC(move) != PAWN)
		return VAL_NONE;

	whites = board->pcs[WHITE][ALL] ^ board->pcs[WHITE][KING];
	blacks = board->pcs[BLACK][ALL] ^ board->pcs[BLACK][KING];
	wking = flip[board->king_sq[WHITE]];
	bking = flip[board->king_sq[BLACK]];

	while (whites) {
		sq = pop_lsb(&whites);
		sqs[n] = flip[sq];
		pcs[n++] = pc_table[WHITE][board->mailbox[sq]];
	}
	while (blacks) {
		sq = pop_lsb(&blacks);
		sqs[n] = flip[sq];
		pcs[n++] = pc_table[BLACK][board->mailbox[sq]];
	}

	val = probe_egbb(board->color, wking, bking,
	                 pcs[0], sqs[0], pcs[1], sqs[1], pcs[2], sqs[2]);
	if (val == _NOTFOUND)
		return VAL_NONE;

	if (val > 0)
		val += VAL_BITBASE - ply;
	else if (val < 0)
		val += ply - VAL_BITBASE;

	return val;
}

