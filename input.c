/* Sloppy - input.c
   Functions for parsing and executing Sloppy's commands

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

#ifdef WINDOWS
#include <windows.h>
#include <conio.h>
#else /* not WINDOWS */
#include <unistd.h>
#endif /* not WINDOWS */

#include "chess.h"
#include "debug.h"
#include "util.h"
#include "eval.h"
#include "pgn.h"
#include "perft.h"
#include "bench.h"
#include "xboard.h"


typedef enum _SloppyId
{
	SLID_XBOARD,
	SLID_QUIT,
	SLID_DEBUG,
	SLID_PRINTBOARD,
	SLID_PRINTEVAL,
	SLID_PRINTMAT,
	SLID_PRINTKEY,
	SLID_PRINTMOVES,
	SLID_TESTSEE,
	SLID_PERFT,
	SLID_DIVIDE,
	SLID_READPGNLIST,
	SLID_READPGN,
	SLID_BENCH,
	SLID_TESTPOS,
	SLID_TESTSUITE,
	SLID_HELP,
	SLID_NONE
} SloppyId;

typedef struct _SloppyCmd
{
	SloppyId id;
	const char *cmd;
	CmdType cmd_type;
} SloppyCmd;

static SloppyCmd slcmds[] =
{
	{ SLID_XBOARD, "xboard", CMDT_EXEC_AND_CONTINUE },
	{ SLID_QUIT, "quit", CMDT_CANCEL },
	{ SLID_DEBUG, "debug", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PRINTBOARD, "printboard", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PRINTEVAL, "printeval", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PRINTMAT, "printmat", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PRINTKEY, "printkey", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PRINTMOVES, "printmoves", CMDT_EXEC_AND_CONTINUE },
	{ SLID_TESTSEE, "testsee", CMDT_EXEC_AND_CONTINUE },
	{ SLID_PERFT, "perft", CMDT_CANCEL },
	{ SLID_DIVIDE, "divide", CMDT_CANCEL },
	{ SLID_READPGNLIST, "readpgnlist", CMDT_CANCEL },
	{ SLID_READPGN, "readpgn", CMDT_CANCEL },
	{ SLID_BENCH, "bench", CMDT_CANCEL },
	{ SLID_TESTPOS, "testpos", CMDT_CANCEL },
	{ SLID_TESTSUITE, "testsuite", CMDT_CANCEL },
	{ SLID_HELP, "help", CMDT_EXEC_AND_CONTINUE }
};


static SloppyCmd
*get_slcmd(Chess *chess)
{
	int i;
	char *cmd;
	char *param;
	char line[MAX_BUF];
	
	ASSERT(1, chess != NULL);

	strlcpy(line, last_input, MAX_BUF);
	cmd = strtok_r(line, " ", &param);
	
	for (i = 0; i < SLID_NONE; i++) {
		if (!strcmp(cmd, slcmds[i].cmd))
			return &slcmds[i];
	}

	return NULL;
}

static CmdType
get_sloppy_cmd_type(Chess *chess)
{
	SloppyCmd *slcmd;
	
	ASSERT(1, chess != NULL);
	
	if (chess->analyze)
		return get_xboard_cmd_type(chess);

	slcmd = get_slcmd(chess);
	if (slcmd == NULL)
		return get_xboard_cmd_type(chess);

	return slcmd->cmd_type;
}

/* Displays a list of commands and their descriptions.  */
static void
print_help(void)
{
	printf("Accepted commands:\n\n"
	       "bench - runs Sloppy's own benchmark\n"
	       "debug - toggles debugging mode\n"
	       "divide [depth] - perft with a node count for each root move\n"
	       "help - shows this list\n"
	       "perft [depth] - runs the perft test [depth] plies deep\n"
	       "printboard - prints an ASCII chess board and the FEN string\n"
	       "printeval - prints the static evaluation\n"
	       "printkey - prints the hash key\n"
	       "printmat - prints the material each player has on the board\n"
	       "printmoves - prints a list of legal moves\n"
	       "quit - quits the program\n"
	       "readpgn [file] - imports a pgn file to the book\n"
	       "readpgnlist [file] - imports a list of pgn files to the book\n"
	       "testpos [time] [fen] - runs a test position (eg. WAC, WCSAC)\n"
	       "testsee [fen] [move] - tests the Static Exchange Evaluator\n"
	       "testsuite [time] [file] - runs a list of test positions\n"
	       "xboard - switches to Xboard/Winboard mode\n\n");
}

static void
input_perft(Board *board, const char *param, bool divide)
{
	int depth;
	S64 timer;
	U64 nnodes;
	
	ASSERT(1, board != NULL);
	ASSERT(1, param != NULL);

	if (strlen(param) == 0) {
		printf("A parameter for perft is needed\n");
		return;
	}
	depth = atoi(param);
	if (depth < 1) {
		printf("Depth is too small: %d (minimum 1)\n", depth);
		return;
	}
	timer = get_ms();
	nnodes = perft_root(board, depth, divide);
	timer = get_ms() - timer;
	printf("Perft(%d): %" PRIu64 " nodes.\n", depth, nnodes);
	printf("Time: %.2f seconds.\n", ((double)timer / 1000.0));
	printf("Processing speed: %.0f nodes per second.\n",
	       (double)nnodes / ((double)timer / 1000.0));
}

static void
input_readpgn(Chess *chess, const char *param)
{
	S64 timer;
	int npos;
	double sec;
	
	ASSERT(1, chess != NULL);
	ASSERT(1, param != NULL);

	timer = get_ms();
	npos = pgn_to_tree(param, &chess->book);

	sec = (double)(get_ms() - timer) / 1000.0;
	printf("PGN file read in %.2f seconds.\n", sec);
	printf("%d new positions were stored in the book.\n", npos);
}

static void
input_readpgnlist(Chess *chess, const char *param)
{
	FILE *fp;
	char filename[MAX_BUF];
	S64 timer;
	int npos = 0;
	double sec;
	
	ASSERT(1, chess != NULL);
	ASSERT(1, param != NULL);

	if ((fp = fopen(param, "r")) == NULL) {
		my_perror("Can't open file %s", param);
		return;
	}

	timer = get_ms();

	while (fgetline(filename, MAX_BUF, fp) != EOF) {
		if (strlen(filename) > 2)
			npos += pgn_to_tree(filename, &chess->book);
	}

	my_close(fp, param);
	sec = (double)(get_ms() - timer) / 1000.0;
	printf("PGN file(s) read in %.2f seconds.\n", sec);
	printf("%d new positions were stored in the book.\n", npos);
}

static void
input_testpos(char **param, bool show_pv)
{
	int time_limit;
	S64 timer;
	Chess tmp_chess;
	
	ASSERT(1, param != NULL);
	ASSERT(1, *param != NULL);

	if (strlen(*param) == 0) {
		printf("A time limit (in seconds) and a valid"
		       " test position are needed.\n");
		return;
	}

	time_limit = atoi(strtok_r(NULL, " ", param)) * 1000;
	if (strlen(*param) == 0) {
		printf("A valid test position is needed.\n");
		return;
	}
	if (time_limit <= 0) {
		printf("The time limit has to be greater than 0.\n");
		return;
	}
	timer = get_ms();
	
	init_chess(&tmp_chess);
	tmp_chess.show_pv = show_pv;
	tmp_chess.increment = time_limit;
	switch (test_pos(&tmp_chess, *param)) {
	case -1:
		printf("Invalid test position: %s\n", *param);
		return;
	case 0:
		printf("Couldn't solve test\n");
		break;
	case 1:
		printf("Test solved\n");
		break;
	case 2:
		printf("Test cancelled by user\n");
		return;
	}
	print_search_data(&tmp_chess.sd, (int)(get_ms() - timer));
}

static void
input_testsuite(char **param, bool show_pv)
{
	Chess tmp_chess;
	int time_limit;
	
	ASSERT(1, param != NULL);
	ASSERT(1, *param != NULL);
	
	if (strlen(*param) == 0) {
		printf("A time limit (in seconds) and the filename"
		       " of the test suite are needed.\n");
		return;
	}

	time_limit = atoi(strtok_r(NULL, " ", param)) * 1000;
	if (strlen(*param) == 0) {
		printf("The filename of the test suite is needed.\n");
		return;
	}
	if (time_limit <= 0) {
		printf("The time limit has to be greater than 0.\n");
		return;
	}
	
	init_chess(&tmp_chess);
	tmp_chess.show_pv = show_pv;
	tmp_chess.increment = time_limit;
	test_suite(&tmp_chess, *param);
}

/* Read input from stdin or last_input (if the input was already read) and
   perform the task associated with the command.

   These commands only work in the PROTO_NONE mode. Xboard commands are
   however valid also in the PROTO_NONE mode.  */
int
read_input(Chess *chess)
{
	Board *board;
	char line[MAX_BUF];
	char *cmd;
	char *param = NULL;
	SloppyCmd *slcmd;

	ASSERT(1, chess != NULL);

	board = &chess->board;
	
	if (ninput <= 0 || strlen(last_input) == 0) {
		/* Read input.  */
		if (chess->protocol == PROTO_NONE) {
			if (board->color == WHITE)
				printf("White: ");
			else
				printf("Black: ");
		}		
		if (fgetline(last_input, MAX_BUF, stdin) < 1)
			return 0;
		ninput++;
	}
	ninput--;
	strlcpy(line, last_input, MAX_BUF);

	if (chess->protocol == PROTO_XBOARD || chess->analyze)
		return read_xb_input(chess);

	cmd = strtok_r(line, " ", &param);
	slcmd = get_slcmd(chess);
	/* If the command isn't any of Sloppy's own commands, we'll
	   try it as an Xboard command.  */
	if (slcmd == NULL)
		return read_xb_input(chess);

	switch (slcmd->id) {
	case SLID_XBOARD:
		chess->protocol = PROTO_XBOARD;
		printf("\n");
		break;
	case SLID_QUIT:
		return -1;
	/* Toggle debug mode.
	   In debug mode Sloppy prints additional information like node count,
	   search time, hash signature, etc.  */
	case SLID_DEBUG:
		chess->debug = !chess->debug;
		if (chess->protocol == PROTO_NONE) {
			if (chess->debug)
				printf("Debugging mode ON\n");
			else
				printf("Debugging mode OFF\n");
		}
		break;
	case SLID_PRINTEVAL:
		printf("eval: %d\n", eval(board));
		break;
	/* Print the amount of material (in centipawns) each player has.  */
	case SLID_PRINTMAT:
		printf("eval: White %d, Black %d\n",
		       board->material[WHITE], board->material[BLACK]);
		printf("Max phase: %d\n", max_phase);
		printf("Phase: %d\n", board->phase);
		break;
	case SLID_PRINTKEY:
		printf("Hash key: %" PRIu64 "\n", board->posp->key);
		break;
	/* Test the static exchange evaluator.
	   Usage: testsee move fen
	   Example: testsee Nxd5 4k3/6q1/1n1r4/3b4/3r4/4NB2/3R4/4K2Q w - - 0 1
	   The SEE value is reported in centipawns.  */
	case SLID_TESTSEE:
		test_see(param, strtok_r(NULL, " ", &param));
		break;
	/* Print a list of legal moves for the current position.  */
	case SLID_PRINTMOVES:
		print_moves(board, false);
		break;
	case SLID_PERFT: case SLID_DIVIDE:
		input_perft(board, param, (slcmd->id == SLID_DIVIDE));
		break;
	case SLID_READPGNLIST:
		input_readpgnlist(chess, param);
		break;
	case SLID_READPGN:
		input_readpgn(chess, param);
		break;
	case SLID_BENCH:
		bench();
		break;
	case SLID_TESTPOS:
		input_testpos(&param, chess->show_pv);
		break;
	case SLID_TESTSUITE:
		input_testsuite(&param, chess->show_pv);
		break;
	case SLID_PRINTBOARD:
		print_board(board);
		break;
	case SLID_HELP:
		print_help();
		break;
	default:
		my_error("Invalid command: %s", cmd);
		break;
	}
	
	return 0;
}

/* Returns the type of the last input. If the type is CMDT_EXEC_AND_CONTINUE,
   the command is also executed here.  */
static CmdType
get_cmd_type(Chess *chess)
{
	CmdType return_val;

	ASSERT(1, chess != NULL);

	return_val = 0;
	if (fgetline(last_input, MAX_BUF, stdin) < 1)
		return CMDT_NONE;
	ninput++;

	if (chess->protocol == PROTO_XBOARD)
		return_val = get_xboard_cmd_type(chess);
	else if (chess->protocol == PROTO_NONE)
		return_val = get_sloppy_cmd_type(chess);

	if (return_val == CMDT_EXEC_AND_CONTINUE)
		read_input(chess);
	
	return return_val;
}

/* See if there's any input (with a line break) in stdin. If there is,
   then return the type of the input.  */
#ifdef WINDOWS

CmdType
input_available(Chess *chess)
{
	static bool init_done = false;
	static bool pipe;
	static HANDLE inh;
	DWORD dw;
	
	ASSERT(2, chess != NULL);

	if (!init_done) {
		init_done = true;
		inh = GetStdHandle(STD_INPUT_HANDLE);
		pipe = !GetConsoleMode(inh, &dw);
		if (!pipe) {
			DWORD mode;
			mode = dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
			SetConsoleMode(inh, mode);
			FlushConsoleInputBuffer(inh);
		}
	}
	if (pipe) {
		if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL) || dw > 0)
			return get_cmd_type(chess);
	} else {
		INPUT_RECORD irec[MAX_BUF];
		DWORD records;
		GetNumberOfConsoleInputEvents(inh, &dw);
		if (dw <= 1)
			return CMDT_NONE;
		PeekConsoleInput(inh, irec, dw, &records);
		if (irec[dw - 1].Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
			return get_cmd_type(chess);
	}

	return CMDT_NONE;
}
#else /* not WINDOWS */

CmdType
input_available(Chess *chess)
{
	fd_set set;
	struct timeval timeout;

	ASSERT(2, chess != NULL);

	/* Initialize the file descriptor set.  */
	FD_ZERO (&set);
	FD_SET (STDIN_FILENO, &set);

	/* Initialize the timeout data structure.  */
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
	case -1:
		fatal_perror("Couldn't read input from stdin");
		break;
	case 1:
		return get_cmd_type(chess);
	case 0:
		break;
	default:
		fatal_error("Invalid return value for select()");
		break;
	}
	
	return CMDT_NONE;
}
#endif /* not WINDOWS */

