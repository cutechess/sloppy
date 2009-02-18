/* Sloppy - xboard.c
   Functions for parsing and executing Xboard commands

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
#include "book.h"
#include "util.h"
#include "debug.h"
#include "notation.h"
#include "hash.h"
#include "makemove.h"
#include "game.h"
#include "xboard.h"


typedef enum _XbId
{
	XBID_XBOARD,
	XBID_PROTOVER,
	XBID_ACCEPTED,
	XBID_REJECTED,
	XBID_NEW,
	XBID_QUIT,
	XBID_FORCE,
	XBID_GO,
	XBID_PLAYOTHER,
	XBID_LEVEL,
	XBID_ST,
	XBID_SD,
	XBID_TIME,
	XBID_OTIM,
	XBID_MOVE_NOW, /* "?" */
	XBID_PING,
	XBID_RESULT,
	XBID_SETBOARD,
	XBID_HINT,
	XBID_BK,
	XBID_UNDO,
	XBID_REMOVE,
	XBID_POST,
	XBID_NOPOST,
	XBID_ANALYZE,
	XBID_NAME,
	XBID_COMPUTER,
	XBID_MEMORY,
	XBID_EXIT,
	XBID_ANALYZE_UPDATE, /* "." */
	XBID_MOVESTR, /* any chess move */
	XBID_NONE
} XbId;

typedef enum _XbMode
{
	XBMODE_BASIC,
	XBMODE_ANALYZE,
	XBMODE_ALL,
	XBMODE_NONE
} XbMode;

typedef struct _XbCmd
{
	XbId id;
	const char *cmd;
	CmdType cmd_type;
	XbMode mode;
} XbCmd;

static XbCmd xbcmds[] =
{
	{ XBID_XBOARD, "xboard", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_PROTOVER, "protover", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_ACCEPTED, "accepted", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_REJECTED, "rejected", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_NEW, "new", CMDT_CANCEL, XBMODE_ALL },
	{ XBID_QUIT, "quit", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_FORCE, "force", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_GO, "go", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_PLAYOTHER, "playother", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_LEVEL, "level", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_ST, "st", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_SD, "sd", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_TIME, "time", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_OTIM, "otim", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_MOVE_NOW, "?", CMDT_FINISH, XBMODE_BASIC },
	{ XBID_PING, "ping", CMDT_EXEC_AND_CONTINUE, XBMODE_ALL },
	{ XBID_RESULT, "result", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_SETBOARD, "setboard", CMDT_CANCEL, XBMODE_ALL },
	{ XBID_HINT, "hint", CMDT_EXEC_AND_CONTINUE, XBMODE_ALL },
	{ XBID_BK, "bk", CMDT_EXEC_AND_CONTINUE, XBMODE_ALL },
	{ XBID_UNDO, "undo", CMDT_CANCEL, XBMODE_ALL },
	{ XBID_REMOVE, "remove", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_POST, "post", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_NOPOST, "nopost", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_ANALYZE, "analyze", CMDT_CANCEL, XBMODE_BASIC },
	{ XBID_NAME, "name", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_COMPUTER, "computer", CMDT_EXEC_AND_CONTINUE, XBMODE_BASIC },
	{ XBID_MEMORY, "memory", CMDT_CANCEL, XBMODE_ALL },
	{ XBID_EXIT, "exit", CMDT_CANCEL, XBMODE_ANALYZE },
	{ XBID_ANALYZE_UPDATE, ".", CMDT_EXEC_AND_CONTINUE, XBMODE_ANALYZE },
	{ XBID_MOVESTR, "", CMDT_CANCEL, XBMODE_ALL }
};

static XbCmd
*get_xbcmd(void)
{
	int i;
	char *cmd;
	char *param;
	char line[MAX_BUF];
	
	strlcpy(line, last_input, MAX_BUF);
	cmd = strtok_r(line, " ", &param);
	
	for (i = 0; i <= XBID_ANALYZE_UPDATE; i++) {
		if (!strcmp(cmd, xbcmds[i].cmd))
			return &xbcmds[i];
	}
	if (is_move_str(cmd))
		return &xbcmds[i];

	return NULL;
}

CmdType
get_xboard_cmd_type(const Chess *chess)
{
	XbCmd *xbcmd;
	
	ASSERT(1, chess != NULL);
	
	xbcmd = get_xbcmd();
	if (xbcmd != NULL) {
		if ((chess->analyze && xbcmd->mode != XBMODE_BASIC)
		||  (!chess->analyze && xbcmd->mode != XBMODE_ANALYZE))
			return xbcmd->cmd_type;
	}

	return CMDT_EXEC_AND_CONTINUE;
}

/* Tell the result of the game to Sloppy, even if he already knows it.  */
static void
xb_result(Chess *chess, const char *result)
{
	int winner = -1;

	ASSERT(1, chess != NULL);
	ASSERT(1, result != NULL);

	if (chess->cpu_color == WHITE)
		log_game(result, APP_NAME, chess->op_name);
	else
		log_game(result, chess->op_name, APP_NAME);
	chess->game_over = true;
	
	if (!strcmp(result, "1-0"))
		winner = WHITE;
	else if (!strcmp(result, "0-1"))
		winner = BLACK;
	/* To keep the opening book reliable, book learning is used
	   only when Sloppy loses a game. */
	if (winner == !chess->cpu_color)
		book_learn(&chess->board, winner, &chess->book);
}

/* Execute an Xboard analyze mode command.  */
static int
exec_xb_analyze_cmd(Chess *chess, const XbCmd *xbcmd)
{
	ASSERT(1, chess != NULL);
	ASSERT(1, xbcmd != NULL);
	ASSERT(1, xbcmd->mode == XBMODE_ANALYZE);
	
	switch (xbcmd->id) {
		int t_elapsed;
		SearchData *sd;
	case XBID_EXIT:
		chess->analyze = false;
		break;
	case XBID_ANALYZE_UPDATE:
		sd = &chess->sd;
		t_elapsed = (get_ms() - sd->t_start) / 10;
		printf("stat01: %d %" PRIu64 " %d %d %d %s\n",
		       t_elapsed, sd->nnodes + sd->nqs_nodes, sd->ply,
		       sd->nmoves_left, sd->nmoves, sd->san_move);
		break;
	default:
		my_error("Invalid Xboard analyze command: %s", last_input);
		break;
	}

	return 0;
}

/* Read an Xboard command from last_input and execute it.

   The specifications of the XBoard/Winboard protocol can be found here:
   http://www.research.digital.com/SRC/personal/mann/xboard/engine-intf.html  */
int
read_xb_input(Chess *chess)
{
	Board *board;
	char line[MAX_BUF];
	char *cmd;
	char *param = NULL;
	XbCmd *xbcmd;

	ASSERT(1, chess != NULL);

	board = &chess->board;
	
	strlcpy(line, last_input, MAX_BUF);
	cmd = strtok_r(line, " ", &param);

	xbcmd = get_xbcmd();
	if (xbcmd == NULL
	||  (chess->analyze && xbcmd->mode == XBMODE_BASIC)
	||  (!chess->analyze && xbcmd->mode == XBMODE_ANALYZE)) {
		printf("Error (unknown command): %s\n", cmd);
		return 0;
	}

	if (chess->analyze && xbcmd->mode == XBMODE_ANALYZE)
		return exec_xb_analyze_cmd(chess, xbcmd);

	switch (xbcmd->id) {
		int st;
		int time_left;
		int depth;
		int memory;
		char *tok;
		U32 move;
	case XBID_XBOARD:
		chess->protocol = PROTO_NONE;
		printf("Xboard mode disabled.\n");
		break;
	case XBID_PROTOVER:
		if (atoi(param) < 2) {
			chess->protocol = PROTO_NONE;
			printf("Xboard protocol 2 or newer is needed.\n");
			break;
		}
		printf("feature myname=\"%s-%s\""
		       " ping=1"
		       " setboard=1"
		       " playother=1"
		       " san=0"
		       " usermove=0"
		       " time=1"
		       " draw=0"
		       " variants=\"normal\""
		       " colors=0"
		       " sigint=0"
		       " sigterm=0"
		       " reuse=1"
		       " analyze=1"
		       " ics=0"
		       " name=1"
		       " pause=0"
		       " nps=0"
		       " debug=0"
		       " memory=1"
		       " smp=0"
		       " egt=scorpio"
		       " done=1\n", APP_NAME, APP_VERSION);
		break;
	case XBID_ACCEPTED:
		break;
	case XBID_REJECTED:
		break;
	case XBID_NEW:
		new_game(chess, START_FEN, BLACK);
		break;
	case XBID_QUIT:
		return -1;
	case XBID_FORCE:
		chess->cpu_color = COLOR_NONE;
		break;
	case XBID_GO:
		chess->cpu_color = board->color;
		break;
	case XBID_PLAYOTHER:
		chess->cpu_color = !board->color;
		break;
	/* Usage: level MOVES_PER_TC TIME_PER_TC TIME_INCREMENT
	   - MOVES_PER_TC is the number of moves in a time control. If it's 0,
	     then the whole game must be played in TIME_PER_TC time.
	   - TIME_PER_TC is in format MINUTES:SECONDS, though the :SECONDS part
	     can be omitted.
	   - TIME_INCREMENT is the amount of time (in seconds) that's added to
	     the clock on each move. A time increment of 0 means that the game
	     is played in conventional clock mode. */
	case XBID_LEVEL:
		tok = strtok_r(NULL, " ", &param);
		chess->nmoves_per_tc = atoi(tok);
		tok = strtok_r(NULL, " ", &param);
		if (strchr(tok, ':') == NULL)
			chess->max_time = (atoi(tok) * 60) * 1000;
		else {
			char *tok2 = NULL;
			tok = strtok_r(tok, ":", &tok2);
			chess->max_time = (atoi(tok) * 60) * 1000;
			tok = strtok_r(NULL, " ", &tok2);
			chess->max_time += atoi(tok) * 1000;
		}
		tok = strtok_r(NULL, " ", &param);
		chess->increment = atoi(tok) * 1000;
		break;
	/* Usage: st N
	   Each move must be made in at most N seconds. Time not used on one
	   move does not accumulate for use on later moves. */
	case XBID_ST:
		st = (atoi(param) * 1000) - 200;
		chess->nmoves_per_tc = 0;
		chess->max_time = st;
		chess->tc_end = 0;
		chess->increment = st;
		break;
	/* Usage: sd DEPTH
	   The engine should limit its thinking to DEPTH ply. */
	case XBID_SD:
		depth = atoi(param);
		if (depth > 0)
			chess->max_depth = depth;
		break;
	/* Usage: time N
	   Set a clock that always belongs to the engine. N is a number in
	   centiseconds (units of 1/100 second). Even if the engine changes to
	   playing the opposite color, this clock remains with the engine. */
	case XBID_TIME:
		time_left = atoi(param) * 10;
		if (time_left > 0)
			chess->tc_end = get_ms() + time_left;
		else
			chess->tc_end = 0;
		break;
	/* Usage: otim N
	   Set a clock that always belongs to the opponent. N is a number in
	   centiseconds (units of 1/100 second). Even if the opponent changes to
	   playing the opposite color, this clock remains with the opponent. */
	case XBID_OTIM:
		break;
	case XBID_MOVE_NOW:
		break;
	case XBID_PING:
		tok = strtok_r(NULL, " ", &param);
		printf("pong %s\n", tok);
		break;
	case XBID_RESULT:
		tok = strtok_r(NULL, " ", &param);
		xb_result(chess, tok);
		break;
	case XBID_SETBOARD:
		if (strlen(param) == 0)
			printf("A valid FEN string is needed.\n");
		else
			new_game(chess, param, -1);
		break;
	/* Tries to find the best move for the current position from the
	   hash table. */
	case XBID_HINT:
		move = NULLMOVE;
		if (settings.book_type != BOOK_OFF)
			move = get_book_move(board, false, chess->book);
		if (move == NULLMOVE)
			move = get_hash_move(board->posp->key);
		if (move != NULLMOVE) {
			char san_move[MAX_BUF];
			move_to_san(san_move, board, move);
			printf("Hint: %s\n", san_move);
		}
		break;
	case XBID_BK:
		if (settings.book_type != BOOK_OFF)
			print_book(board, chess->book);
		else
			printf("Opening book is disabled\n");
		break;
	case XBID_UNDO:
		if (board->nmoves > 0) {
			undo_move(board);
			chess->game_over = false;
		}
		break;
	case XBID_REMOVE:
		if (board->nmoves > 1) {
			undo_move(board);
			undo_move(board);
			chess->game_over = false;
		}
		break;
	case XBID_POST:
		chess->show_pv = true;
		break;
	case XBID_NOPOST:
		chess->show_pv = false;
		break;
	case XBID_ANALYZE:
		chess->analyze = true;
		analyze_mode(chess);
		break;
	case XBID_NAME:
		strlcpy(chess->op_name, param, MAX_BUF);
		break;
	case XBID_COMPUTER:
		break;
	case XBID_MEMORY:
		memory = atoi(param);
		if (memory < 8 || memory > 1024)
			printf("Hash size must be between 8 and 1024 MB.\n");
		else {
			set_hash_size(memory);
			init_hash();
		}
		break;
	case XBID_MOVESTR:
		move = str_to_move(board, cmd);
		if (move == MOVE_ERROR)
			printf("Error (unknown command): %s\n", cmd);
		else if (chess->game_over)
			printf("Error (the game is over, move rejected)\n");
		else if (move == NULLMOVE)
			printf("Illegal move: %s\n", cmd);
		else {
			char san_move[MAX_BUF];

			move_to_san(san_move, board, move);
			update_game_log(board, san_move, VAL_NONE, false);
			update_game(chess, move);
		}
		break;
	default:
		my_error("Invalid Xboard command: %s", cmd);
		break;
	}

	return 0;
}

