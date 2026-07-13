/* Sloppy - uci.c
   Functions for parsing and executing UCI commands

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
#include <ctype.h>
#include <limits.h>
#include "sloppy.h"
#include "chess.h"
#include "util.h"
#include "debug.h"
#include "notation.h"
#include "hash.h"
#include "makemove.h"
#include "movegen.h"
#include "game.h"
#include "egbb.h"
#include "uci.h"


/* Egbb cache size limits (in megabytes) for the "EgbbCache" option.  */
#define UCI_EGBB_CACHE_MIN 1
#define UCI_EGBB_CACHE_MAX 1024

/* Time (in milliseconds) reserved for communication overhead
   when a fixed time per move ("go movetime") is used.  */
#define UCI_MOVE_OVERHEAD 200

/* Cap for the clock readings taken from a "go" command. Anything this
   size is effectively an infinite clock for a game of chess, and the
   cap keeps the time allocator's arithmetic, which multiplies the
   budget it derives from the clock, from overflowing an int.  */
#define UCI_MAX_CLOCK_MS (INT_MAX / 16)

typedef enum _UciId
{
	UCIID_UCI,
	UCIID_DEBUG,
	UCIID_ISREADY,
	UCIID_SETOPTION,
	UCIID_REGISTER,
	UCIID_UCINEWGAME,
	UCIID_POSITION,
	UCIID_GO,
	UCIID_STOP,
	UCIID_PONDERHIT,
	UCIID_QUIT,
	UCIID_NONE
} UciId;

typedef struct _UciCmd
{
	UciId id;
	const char *cmd;
	CmdType cmd_type;
} UciCmd;

/* Commands that arrive during a search are classified by <cmd_type>:
   - Every "go" must be answered with exactly one "bestmove", so all the
     commands that end a search do it with CMDT_FINISH, which stops the
     search, sends the best move, and only then executes the command.
   - "quit" is the exception: the engine should exit as soon as possible,
     and the GUI no longer cares about a best move.
   - The rest are executed immediately and the search continues.  */
static UciCmd uci_cmds[] =
{
	{ UCIID_UCI, "uci", CMDT_EXEC_AND_CONTINUE },
	{ UCIID_DEBUG, "debug", CMDT_EXEC_AND_CONTINUE },
	{ UCIID_ISREADY, "isready", CMDT_EXEC_AND_CONTINUE },
	{ UCIID_SETOPTION, "setoption", CMDT_FINISH },
	{ UCIID_REGISTER, "register", CMDT_EXEC_AND_CONTINUE },
	{ UCIID_UCINEWGAME, "ucinewgame", CMDT_FINISH },
	{ UCIID_POSITION, "position", CMDT_FINISH },
	{ UCIID_GO, "go", CMDT_FINISH },
	{ UCIID_STOP, "stop", CMDT_FINISH },
	{ UCIID_PONDERHIT, "ponderhit", CMDT_FINISH },
	{ UCIID_QUIT, "quit", CMDT_CANCEL }
};

/* The effective book mode from initialization, saved when UCI mode is
   first entered so that the "OwnBook" option can restore it after the
   book has been switched off.  */
static BookType configured_book_type = BOOK_OFF;


static UciCmd
*get_ucicmd(void)
{
	int i;
	char *cmd;
	char *param;
	char line[MAX_INPUT_BUF];

	strlcpy(line, last_input, MAX_INPUT_BUF);
	cmd = strtok_r(line, " ", &param);
	if (cmd == NULL)
		return NULL;

	for (i = 0; i < UCIID_NONE; i++) {
		if (!strcmp(cmd, uci_cmds[i].cmd))
			return &uci_cmds[i];
	}

	return NULL;
}

CmdType
get_uci_cmd_type(const Chess *chess)
{
	UciCmd *ucicmd;

	ASSERT(1, chess != NULL);

	ucicmd = get_ucicmd();
	if (ucicmd != NULL)
		return ucicmd->cmd_type;

	/* Per the UCI specification unknown commands are ignored, so they
	   mustn't disturb the search.  */
	return CMDT_EXEC_AND_CONTINUE;
}

void
enter_uci_mode(Chess *chess)
{
	ASSERT(1, chess != NULL);

	if (chess->protocol != PROTO_UCI) {
		configured_book_type = settings.book_type;
		/* A line break, because the command prompt of Sloppy's own
		   protocol may still be open on this line.  */
		printf("\n");
	}

	chess->protocol = PROTO_UCI;
	/* UCI GUIs expect an "info" line after each search iteration.  */
	chess->show_pv = true;

	printf("id name %s %s\n", APP_NAME, APP_VERSION);
	printf("id author Ilari Pihlajisto\n");
	printf("option name Hash type spin default %lu min %d max %d\n",
	       get_hash_size_mb(), HASH_SIZE_MIN_MB, HASH_SIZE_MAX_MB);
	printf("option name Clear Hash type button\n");
	printf("option name OwnBook type check default %s\n",
	       (settings.book_type != BOOK_OFF) ? "true" : "false");
	printf("option name EgbbPath type string default %s\n",
	       (strlen(settings.egbb_path) > 0) ? settings.egbb_path
	                                        : "<empty>");
	printf("option name EgbbLoadType type combo default %s"
	       " var 4men var 5men var smart var none var off\n",
	       egbb_load_type_str(settings.egbb_load_type));
	printf("option name EgbbCache type spin default %lu min %d max %d\n",
	       (unsigned long)(settings.egbb_cache_size / 0x100000),
	       UCI_EGBB_CACHE_MIN, UCI_EGBB_CACHE_MAX);
	printf("option name Egbb5Men type check default %s\n",
	       (settings.egbb_max_men >= 5) ? "true" : "false");
	printf("uciok\n");
}

static void
uci_set_hash(const char *value)
{
	if (value == NULL) {
		printf("info string A hash size in megabytes is needed\n");
		return;
	}
	if (!resize_hash_table_mb(atoi(value)))
		printf("info string Hash size must be between %d and %d MB\n",
		       HASH_SIZE_MIN_MB, HASH_SIZE_MAX_MB);
}

static void
uci_set_own_book(const Chess *chess, const char *value)
{
	ASSERT(1, chess != NULL);

	if (value == NULL
	|| (strcmp_nocase(value, "true") && strcmp_nocase(value, "false"))) {
		printf("info string OwnBook must be true or false\n");
		return;
	}
	if (!strcmp_nocase(value, "false")) {
		settings.book_type = BOOK_OFF;
		return;
	}
	if (configured_book_type == BOOK_OFF
	|| (configured_book_type == BOOK_MEM && chess->book == NULL)) {
		printf("info string No opening book is available\n");
		return;
	}
	settings.book_type = configured_book_type;
}

static void
uci_set_egbb_path(const char *value)
{
	if (value == NULL || strlen(value) == 0
	||  !strcmp_nocase(value, "<empty>")) {
		set_egbb_path("");
		return;
	}
	activate_egbb_path(value);
}

static void
uci_set_egbb_load_type(const char *value)
{
	int load_type;

	if (value == NULL) {
		printf("info string An egbb load type is needed\n");
		return;
	}
	load_type = parse_egbb_load_type(value);
	if (load_type == -1) {
		printf("info string Invalid egbb load type: %s\n", value);
		return;
	}
	settings.egbb_load_type = (EgbbLoadType)load_type;
	if (settings.egbb_load_type == EGBB_OFF) {
		unload_bitbases();
		return;
	}
	if (strlen(settings.egbb_path) > 0)
		load_bitbases();
}

static void
uci_set_egbb_5men(const char *value)
{
	if (value == NULL
	|| (strcmp_nocase(value, "true") && strcmp_nocase(value, "false"))) {
		printf("info string Egbb5Men must be true or false\n");
		return;
	}
	/* The limit is checked when the bitbases are probed, so the change
	   takes effect without reloading them.  */
	if (!strcmp_nocase(value, "true"))
		settings.egbb_max_men = 5;
	else
		settings.egbb_max_men = 4;
}

static void
uci_set_egbb_cache(const char *value)
{
	int cache_mb;

	if (value == NULL) {
		printf("info string A cache size in megabytes is needed\n");
		return;
	}
	cache_mb = atoi(value);
	if (cache_mb < UCI_EGBB_CACHE_MIN || cache_mb > UCI_EGBB_CACHE_MAX) {
		printf("info string Egbb cache size must be between"
		       " %d and %d MB\n",
		       UCI_EGBB_CACHE_MIN, UCI_EGBB_CACHE_MAX);
		return;
	}
	settings.egbb_cache_size = (size_t)cache_mb * 0x100000;
	if (settings.egbb_load_type != EGBB_OFF
	&&  strlen(settings.egbb_path) > 0)
		load_bitbases();
}

/* Usage: setoption name OPTION_NAME [value OPTION_VALUE]
   The option name may contain spaces (eg. "Clear Hash"), and so may
   the value (eg. a filesystem path).  */
static void
uci_setoption(Chess *chess, char **param)
{
	char name[MAX_BUF];
	char *tok;
	char *value = NULL;

	ASSERT(1, chess != NULL);
	ASSERT(1, param != NULL);

	tok = strtok_r(NULL, " ", param);
	if (tok == NULL || strcmp(tok, "name") != 0) {
		printf("info string The \"name\" keyword is needed\n");
		return;
	}

	name[0] = '\0';
	while ((tok = strtok_r(NULL, " ", param)) != NULL
	&&     strcmp(tok, "value") != 0) {
		if (name[0] != '\0')
			strlcat(name, " ", MAX_BUF);
		strlcat(name, tok, MAX_BUF);
	}
	if (tok != NULL) {
		/* The "value" keyword was found, so the rest of the line,
		   spaces included, is the value.  */
		value = *param;
		while (*value == ' ')
			value++;
	}

	if (!strcmp_nocase(name, "Hash"))
		uci_set_hash(value);
	else if (!strcmp_nocase(name, "Clear Hash"))
		init_hash();
	else if (!strcmp_nocase(name, "OwnBook"))
		uci_set_own_book(chess, value);
	else if (!strcmp_nocase(name, "EgbbPath"))
		uci_set_egbb_path(value);
	else if (!strcmp_nocase(name, "EgbbLoadType"))
		uci_set_egbb_load_type(value);
	else if (!strcmp_nocase(name, "EgbbCache"))
		uci_set_egbb_cache(value);
	else if (!strcmp_nocase(name, "Egbb5Men"))
		uci_set_egbb_5men(value);
	else
		printf("info string Unknown option: %s\n", name);
}

/* Usage: position [startpos | fen FEN_STRING] [moves MOVE1 MOVE2 ...]
   Set up the position described by the FEN string (or the starting
   position), then play the listed moves on it.  */
static void
uci_position(Chess *chess, char **param)
{
	char fen[MAX_BUF];
	char *tok;
	U32 move;
	Board tmp_board;
	Board *board;

	ASSERT(1, chess != NULL);
	ASSERT(1, param != NULL);

	board = &chess->board;

	tok = strtok_r(NULL, " ", param);
	if (tok == NULL) {
		printf("info string A position type"
		       " (startpos or fen) is needed\n");
		return;
	}

	if (!strcmp(tok, "startpos")) {
		strlcpy(fen, START_FEN, MAX_BUF);
		tok = strtok_r(NULL, " ", param);
	} else if (!strcmp(tok, "fen")) {
		fen[0] = '\0';
		while ((tok = strtok_r(NULL, " ", param)) != NULL
		&&     strcmp(tok, "moves") != 0) {
			if (fen[0] != '\0')
				strlcat(fen, " ", MAX_BUF);
			strlcat(fen, tok, MAX_BUF);
		}
	} else {
		printf("info string Invalid position type: %s\n", tok);
		return;
	}

	/* Parse the FEN and replay the moves on a temporary board first, so
	   that a broken FEN string or an illegal move in the list can't
	   corrupt or half-update the game board.  */
	if (fen_to_board(&tmp_board, fen)) {
		printf("info string Invalid FEN string: %s\n", fen);
		return;
	}

	if (tok != NULL) {
		if (strcmp(tok, "moves") != 0) {
			printf("info string Unexpected token: %s\n", tok);
			return;
		}
		while ((tok = strtok_r(NULL, " ", param)) != NULL) {
			if (tmp_board.nmoves >= MAX_NMOVES_PER_GAME - 1) {
				printf("info string Too many moves\n");
				return;
			}
			move = str_to_move(&tmp_board, tok);
			if (move == NULLMOVE || move == MOVE_ERROR) {
				printf("info string Illegal move: %s\n", tok);
				return;
			}
			make_move(&tmp_board, move);
		}
	}

	copy_board(board, &tmp_board);
	chess->game_over = false;
	chess->in_book = false;
	chess->cpu_color = COLOR_NONE;
}

/* Read the integer argument of a "go" parameter.
   Returns true if a valid value was found.  */
static bool
go_int_arg(char **param, const char *name, int *value)
{
	char *end;
	char *tok;
	long val;

	ASSERT(1, param != NULL);
	ASSERT(1, name != NULL);
	ASSERT(1, value != NULL);

	tok = strtok_r(NULL, " ", param);
	if (tok == NULL) {
		printf("info string The %s parameter needs a value\n", name);
		return false;
	}
	val = strtol(tok, &end, 10);
	if (end == tok || *end != '\0') {
		printf("info string Invalid %s value: %s\n", name, tok);
		return false;
	}
	/* strtol itself saturates to LONG_MIN/LONG_MAX, so where long is as
	   wide as int these clamps can't trigger; where long is wider they
	   keep the conversion to int well defined. Out-of-range negatives
	   end up below the callers' "> 0" guards, and huge "infinite" clocks
	   simply become a very long time budget.  */
	if (val > INT_MAX)
		val = INT_MAX;
	else if (val < INT_MIN)
		val = INT_MIN;
	*value = (int)val;
	return true;
}

/* Read the node count argument of "go nodes". A dedicated reader,
   because node counts don't fit in an int.
   Returns true if a valid value was found.  */
static bool
go_nodes_arg(char **param, U64 *value)
{
	char *tok;
	int len = 0;

	ASSERT(1, param != NULL);
	ASSERT(1, value != NULL);

	tok = strtok_r(NULL, " ", param);
	if (tok == NULL) {
		printf("info string The nodes parameter needs a value\n");
		return false;
	}
	if (!isdigit((unsigned char)tok[0])
	||  sscanf(tok, "%" SCNu64 "%n", value, &len) != 1
	||  tok[len] != '\0') {
		printf("info string Invalid nodes value: %s\n", tok);
		return false;
	}
	return true;
}

/* Usage: go [wtime N] [btime N] [winc N] [binc N] [movestogo N]
             [depth N] [nodes N] [movetime N] [infinite] [ponder]
             [searchmoves MOVE1 ...] [mate N]
   Set up the search limits and hand the search over to the main loop
   by making Sloppy the player whose turn it is to move.  */
static void
uci_go(Chess *chess, char **param)
{
	char *tok;
	int wtime = 0;
	int btime = 0;
	int winc = 0;
	int binc = 0;
	int movestogo = 0;
	int depth = 0;
	U64 nodes = 0;
	int movetime = 0;
	int mate_in = 0;
	bool infinite = false;
	bool time_budget_given = false;
	bool parse_error = false;
	int time_left;
	int increment;
	Board *board;
	MoveLst move_list;
	MoveLst searchmoves;

	ASSERT(1, chess != NULL);
	ASSERT(1, param != NULL);

	board = &chess->board;
	searchmoves.nmoves = 0;

	tok = strtok_r(NULL, " ", param);
	while (tok != NULL) {
		bool advance = true;

		if (!strcmp(tok, "wtime")) {
			parse_error |= !go_int_arg(param, "wtime", &wtime);
			time_budget_given = true;
		} else if (!strcmp(tok, "btime")) {
			parse_error |= !go_int_arg(param, "btime", &btime);
			time_budget_given = true;
		} else if (!strcmp(tok, "winc")) {
			parse_error |= !go_int_arg(param, "winc", &winc);
			time_budget_given = true;
		} else if (!strcmp(tok, "binc")) {
			parse_error |= !go_int_arg(param, "binc", &binc);
			time_budget_given = true;
		} else if (!strcmp(tok, "movestogo")) {
			parse_error |= !go_int_arg(param, "movestogo",
			                           &movestogo);
		} else if (!strcmp(tok, "depth")) {
			parse_error |= !go_int_arg(param, "depth", &depth);
		} else if (!strcmp(tok, "nodes")) {
			parse_error |= !go_nodes_arg(param, &nodes);
		} else if (!strcmp(tok, "movetime")) {
			parse_error |= !go_int_arg(param, "movetime",
			                           &movetime);
			time_budget_given = true;
		} else if (!strcmp(tok, "infinite")) {
			infinite = true;
		} else if (!strcmp(tok, "ponder")) {
			/* Sloppy can't actually ponder, but the "bestmove"
			   reply must still wait for "ponderhit" or "stop",
			   just like in an infinite search.  */
			printf("info string Pondering is not supported,"
			       " the best move waits for ponderhit"
			       " or stop\n");
			infinite = true;
		} else if (!strcmp(tok, "mate")) {
			/* Not supported: a normal search is done instead,
			   but the argument is still validated so that a
			   malformed command can't slip through silently.  */
			if (go_int_arg(param, "mate", &mate_in))
				printf("info string The mate parameter"
				       " is not supported\n");
			else
				parse_error = true;
		} else if (!strcmp(tok, "searchmoves")) {
			while ((tok = strtok_r(NULL, " ", param)) != NULL
			&&     is_move_str(tok)) {
				U32 move = str_to_move(board, tok);
				if (move == NULLMOVE || move == MOVE_ERROR)
					printf("info string Illegal"
					       " searchmoves move: %s\n",
					       tok);
				else if (searchmoves.nmoves < MAX_NMOVES)
					searchmoves.move[searchmoves.nmoves++]
					        = move;
			}
			/* <tok> already holds the next parameter.  */
			advance = false;
		} else {
			printf("info string Unknown go parameter: %s\n", tok);
		}

		if (advance)
			tok = strtok_r(NULL, " ", param);
	}

	/* Reset the limits left over from any previous search.  */
	chess->max_depth = DEFAULT_MAX_DEPTH;
	chess->max_nodes = 0;
	chess->max_time = 0;
	chess->tc_end = 0;
	chess->increment = 0;
	chess->nmoves_per_tc = 0;
	chess->nmoves_left_in_tc = 0;
	chess->infinite_search = false;
	chess->no_time_limit = false;
	chess->searchmoves.nmoves = 0;

	if (infinite)
		chess->infinite_search = true;
	/* A parameter that failed to parse mustn't lead to an unbounded
	   search: with a zeroed time budget the engine instead plays its
	   move as fast as it can.  */
	if (parse_error)
		time_budget_given = true;
	/* Without any time parameter the search is limited only by depth or
	   node count. When a clock was sent but it's empty or negative, the
	   limits are left zeroed, which makes the time allocator play the
	   move as fast as it can.  */
	if (!time_budget_given)
		chess->no_time_limit = true;
	if (board->color == WHITE) {
		time_left = wtime;
		increment = winc;
	} else {
		time_left = btime;
		increment = binc;
	}
	if (time_left > UCI_MAX_CLOCK_MS)
		time_left = UCI_MAX_CLOCK_MS;
	if (time_left > 0)
		chess->tc_end = get_ms() + time_left;
	if (increment > 0)
		chess->increment = increment;
	if (movestogo > 0)
		chess->nmoves_left_in_tc = movestogo;
	if (depth > 0) {
		if (depth > DEFAULT_MAX_DEPTH)
			depth = DEFAULT_MAX_DEPTH;
		chess->max_depth = depth;
	}
	if (nodes > 0)
		chess->max_nodes = nodes;
	if (searchmoves.nmoves > 0)
		chess->searchmoves = searchmoves;
	if (movetime > 0) {
		int budget = movetime - UCI_MOVE_OVERHEAD;
		if (budget < movetime / 2)
			budget = movetime / 2;
		if (budget < 1)
			budget = 1;
		/* A fixed time per move is handled the same way as the
		   Xboard "st" command: there's no clock to divide, so the
		   increment is the whole time budget of the search.  */
		chess->tc_end = 0;
		chess->increment = budget;
	}

	gen_moves(board, &move_list);
	if (move_list.nmoves == 0) {
		/* Checkmate or stalemate: there is nothing to search, but in
		   an infinite search even the null move reply must wait for
		   the GUI to ask for it.  */
		if (chess->infinite_search && !uci_wait_for_stop(chess))
			return;
		printf("bestmove 0000\n");
		return;
	}

	chess->game_over = false;
	chess->cpu_color = board->color;
}

bool
uci_queued_cmd_wants_bestmove(void)
{
	UciCmd *ucicmd;

	ucicmd = get_ucicmd();
	return ucicmd != NULL
	       && (ucicmd->id == UCIID_STOP || ucicmd->id == UCIID_PONDERHIT);
}

bool
uci_wait_for_stop(Chess *chess)
{
	UciCmd *ucicmd;

	ASSERT(1, chess != NULL);

	while (fgetline(last_input, MAX_INPUT_BUF, stdin) != EOF) {
		ucicmd = get_ucicmd();
		/* Per the UCI specification unknown commands are ignored.  */
		if (ucicmd == NULL)
			continue;
		if (ucicmd->id == UCIID_STOP || ucicmd->id == UCIID_PONDERHIT)
			return true;
		if (ucicmd->cmd_type == CMDT_EXEC_AND_CONTINUE)
			read_uci_input(chess);
		else {
			/* The GUI is no longer interested in this search:
			   leave the command in the input queue for the main
			   loop, and don't send a best move for a search the
			   GUI has abandoned.  */
			ninput++;
			return false;
		}
	}

	return true;
}

/* Read a UCI command from last_input and execute it.  */
int
read_uci_input(Chess *chess)
{
	char line[MAX_INPUT_BUF];
	char *cmd;
	char *param = NULL;
	UciCmd *ucicmd;

	ASSERT(1, chess != NULL);

	strlcpy(line, last_input, MAX_INPUT_BUF);
	cmd = strtok_r(line, " ", &param);
	if (cmd == NULL)
		return 0;

	ucicmd = get_ucicmd();
	if (ucicmd == NULL) {
		/* Per the UCI specification unknown commands are ignored.  */
		printf("info string Unknown command: %s\n", cmd);
		return 0;
	}

	switch (ucicmd->id) {
	case UCIID_UCI:
		enter_uci_mode(chess);
		break;
	case UCIID_DEBUG:
		if (!strcmp(param, "on"))
			chess->debug = true;
		else if (!strcmp(param, "off"))
			chess->debug = false;
		else
			printf("info string Invalid debug mode: %s\n", param);
		break;
	case UCIID_ISREADY:
		printf("readyok\n");
		break;
	case UCIID_SETOPTION:
		uci_setoption(chess, &param);
		break;
	case UCIID_REGISTER:
		/* Sloppy requires no registration, so any attempt at it
		   is immediately successful.  */
		printf("registration checking\n");
		printf("registration ok\n");
		break;
	case UCIID_UCINEWGAME:
		/* Deliberately keeps the hash table: Sloppy has never cleared
		   it between games (the Xboard "new" command doesn't either),
		   and UCI mode preserves the engine's original behavior. The
		   GUI can ask for a cold start with the Clear Hash button.  */
		new_game(chess, START_FEN, COLOR_NONE);
		break;
	case UCIID_POSITION:
		uci_position(chess, &param);
		break;
	case UCIID_GO:
		uci_go(chess, &param);
		break;
	case UCIID_STOP:
		/* No search is running, so there is nothing to stop.  */
		break;
	case UCIID_PONDERHIT:
		/* Pondering is not supported, so there's nothing to do.  */
		break;
	case UCIID_QUIT:
		return -1;
	default:
		my_error("Invalid UCI command: %s", cmd);
		break;
	}

	return 0;
}
