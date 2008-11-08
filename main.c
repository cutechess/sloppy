/* Sloppy - main.c
   The main() function and initialization.

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


   Version 0.2.0 (02/05/2008):
    - Pawn hash table
    - Support for Scorpio's endgame bitbases
    - Slightly improved time allocation
    - Improved search
    - Minor bugfixes
    - Minor code cleanup

   Version 0.1.1 (10/21/2007):
    - Improved search
    - Replaced command line arguments with a config file
    - The source now compiles with the MSVC++ compiler
    - Cleaned up some of the code
    - Complete rewrite of king-attack evaluation
    - Minor bugfixes
    - Quiescense nodes are also counted in print_pv()


   Completed tasks:
   
    - Passed pawn pushes mustn't be reduced
    - Don't update history tables (not even on fail low) with captures
    - Implement recapture extensions
    - Ditch longjmp() for stopping the search
    - search_book() shouldn't open and close the book file. Instead it
      should have a FILE pointer and the position count passed to it.
    - Drop null move if all the officers have fallen
    - Don't use a custom struct to read/write book positions
    - Add king square to Board structure
    - Encode enpassant square in Move
    - Write an undo_move() function
    - Use SEE for move ordering
    - No null move pruning at PV nodes or when beta is a mate score
    - Replace all occurences of strtok() with strtok_r()
    - Create another version of gen_moves() with <pc> and <to> arguments
      so that we can read a SAN string and create only the moves that we need.
    - Replace age_hash() with a more robust solution. Even with a 35MB hash
      table it takes 5% of the CPU cycles.
    - Improvements to fen_to_board():
      - Does not crash on invalid input
      - Recognizes invalid FEN strings fairly accurately
    - Use a square table for passed pawns instead of a rank table
    - Replace rotated bitboards with antirotated magic bitboards
    - Don't accept repetitions from the opening book
    - Add the ability to convert a board position to FEN
    - When reading PGN files, store book positions in an AVL tree before
      writing to book.
    - Make sure illegal castling is detected correctly
    - Make sure mate scores are accurate
    - Generate only legal moves. This will solve a lot of problems and won't
      hopefully slow the program down too much.
    - Add passed pawn moves to quiescence search
    - Add <U64 check_mask> and <bool double_check> to Board struct.
      The values are updated in make_move().
    - Smooth transition from middlegame to endgame evaluation.
    - Implement futility pruning:
      http://members.home.nl/matador/chess840.htm#FUTILITY
    - Store all necessary information in hash keys, including castling and
      en-passant data.
    - Implement a help command which displays all the available commands
    - Change opening book data structure to:
      - U64 key
      - U16 games
      - U16 wins
    - No promotion argument in get_move(...). The promotion should be included
      in the <U32 move> argument.
    - Use the same hash keys in the Linux and Windows version to make sure that
      the same opening book can be used.
    - pcsq should be defined as pcsq[color][pc][sq], not pcsq[pc][color][sq]
    - Use a function called progressbar() to display the progress while
      running benchmark or reading PGN files.
    - Use the bool type in <stdbool.h> instead of a typedef
    - Moves should be of type U32, not int
    - Eval should never favor a player who has insufficient mating material.
    - Add 50 move rule
    - Get totally rid of path dependent castling evaluation and replace it with
      a better king safety eval.
    - Implement quiescence search
    - Make sure that the "U64 target" argument is taken into consideration
      in each of the gen_x_moves() (i.e. pawn moves) functions.
    - Implement book learning
    - Encode check (one bit) in <U32 move>
    - Order moves with high scores first and not the other way around
    - Replace the global <nnodes> mess with a cleaner solution so that <nnodes>
      won't have to be manually cleared all the time.
    - Penalize backward pawns in eval()
    - Do Internal Iterative Deepening in PV nodes with no hash hit
    - In main(), group the variables of a game in one struct (Chess)
    - Tactical moves shouldn't get a score from the history table
    - Drop null move in extended positions
    - Accept Xboard commands even in the PROTO_NONE mode
    - Sloppy shouldn't go crazy in analyze mode when it reaches the maximum
      search depth (eg. after finding a forced mate).
    - Calculate branching factor somewhat correctly
    - Try checks after captures on the first ply of QS
    - input_available() should call a get_cmd_type() function which
      has the following return values:
        CMDT_EXEC_AND_CONTINUE
        CMDT_FINISH
        CMDT_CANCEL
        CMDT_CONTINUE
    - Separate Xboard commands from Sloppy's own commands.
    - Use the types in <inttypes.h> (C99) for fixed-size integers.
    - The age of a hashnode should be replaced with a birthdate (i.e. move #2).
      The birthdate is the number of moves played in the game, not the move
      number of the move in the search tree.
    - Replace file_mask[square] etc. with file_mask[file] to save memory. It
      won't really be slower because we get the file with just "sq & 7".
    - Use an endianess-independent opening book format. The book is now
      always in little-endian format.
    - Board.nmove should be the number of moves made, and also an index
      for the last move.
    - Replace strcpy() and strncpy() with the safer strlcpy()
    - Replace strcat() and strncat() with the safer strlcat()
    - Improvements to book management:
      - Add a command line parameter to use or not use the book.
      - Add a command line parameter to load or not load it in RAM.
      - Add a command line parameter to enable or disable learning.
      - If not using the "book in memory" mode, disable learning.
    - Rename pop_count() to popcount() so that "pop" wouldn't be confused with
      popping a stack.
    - Rename first_one() to lsb()
    - Add a pop_lsb() function for getting the lsb AND clearing that bit.
      It will make code shorter, not faster.
    - Commands like "hint" and "bk" should use the CMDT_EXEC_AND_CONTINUE mode.
      To do that we need separate boards for the game and the search.
    - Analyze mode should have its own get_input_type() function so that
      normal xboard commands wouldn't get accepted.
    - Switch from mixed case to lower case
    - Quiescence search should have its own simple score_moves() function
    - Make san_to_move() more flexible but still intolerant of illegal moves
    - Get rid of "ULL" in long long literals, we'll have to use C99 anyway
    - Use some getopt() clone + a switch statement to parse input.
      The Xboard and UCI protocols could actually have their own modules.
    - Get rid of verified null move pruning. Just use NULL_R=3.
    - Cancelling the search while running a benchmark or test should also
      cancel the whole benchmark/test.
    - Merge print_debug_info() and print_search_info()
    - Get rid of circular includes when possible. Can't be done for headers
      like chess.h or util.h.
    - Chess.cmd[] and Chess.ncmd should be global variables in util.c.
      The "quit" command must work in the middle of running a testsuite.
    - These things need a better name:
      - int cancel (in struct _SearchData) - DONE (now "CmdType")
      - GameData (struct) and game_data (the variables) - DONE (now "PosInfo")
      - PreMoves (struct) - DONE (now "MoveMasks")
      - pc_type[64] (in struct _Board) - DONE (now "mailbox")
    - Compile with the pedantic flag
    - Fix CastleMask (in util.h). Only half of its contents are masks.
    - Use a sign (int) variable instead of SELF(color)
    - Cleanup or rename some of the ridiculous stuff in sloppy.h
    - Combine define.h, chess.h and chess.c into sloppy.h and sloppy.c, OR
      rename define.h to sloppy.h
    - Use PeekNamedPipe() (in winbase.h) or GetNumberOfConsoleInputEvents()
      (in wincon.h) to poll stdin in Windows.
    - Totally get rid of global.c
    - Implement version 2 of the Xboard protocol, and don't use any
      protover 1 commands (like "white" and "black")
    - Make sure that hash keys in the pv line aren't easily overwritten
    - Rename CAPTURE() to GET_CAPT(), SQ_FROM() to GET_FROM(), etc.
    - Cleanup eval.c and search.c
    - Rewrite the position parser in test_pos()
    - Do something to push_history() and push_killers(). Pushing them takes
      take and doesn't guarantee that the tables are in sync with the game.
    - print_moves() shouldn't be a debug mode function/command
    - The config file is enough for setting options. Get rid of cmdline args.
    - Don't use _WIN32 to mean WINDOWS
    - Test input_available() in Windows pipe mode
    - Fix MSVC++ threading issues in perft
    - Get rid of struct _Masks (in movegen.c)
    - Count also QS nodes in print_pv()
    - Don't overwrite the book file if the book wasn't modified
    - Don't reduce killer moves
    - Add a "logfile on/off" setting
    - Change NO_MOVE to NULLMOVE
    - Always deallocate the book when exiting
    - Add a pawn structure hash table
    - Add support for Scorpio's endgame bitbases


   Tasks in progress:
   
    - Implement the complete Winboard/Xboard protocol:
      http://www.tim-mann.org/xboard/engine-intf.html
    - Better evaluation function:
      - more mobility evaluation
      - Detect draws by material and likely draws by material (i.e. KR vs. KR).
        This detection should happen in search() if possible.
    - Full support for the PGN standard, including FEN strings


   Deferred tasks:
   
    - Try returning alpha or beta even with mate scores
    - Use a bitfield for move instead of an integer
    - Use the BigTwo1 replacement scheme in hash table:
      http://www.xs4all.nl/~breukerd/thesis/summary.html
    - In critical parts use <unsigned> instead of <int> if possible
    - Scale the 7th rank eval bonus with the opponent's pawn material (the more
      pawns it has the bigger the bonus)
    - make_move() should return a boolean value <is_check>.
    - Add a command stack to struct Chess so that all the waiting non-urgent
      commands can be executed when we're done searching.
      UPDATE: turned out that no input ever needs to wait.
    - Evaluation masks (passed pawns etc.) should be grouped in a struct
    - Include ECO code and round number in the PGN log
    - Use the "struct" keyword instead of the typedef name when declaring
      variables in header files (eg. AvlNode *book in Chess).
    - Use a lot more enums like Color, Square, Piece, etc. On the other hand,
      enums aren't enforced so let's drop this plan.
    - Add a global <notation> setting. It should accept values:
      - NTN_SAN   - use SAN notation for moves everywhere
      - NTN_COORD - use coordinate notation everywhere
      - NTN_MIX   - use a mix of SAN and COORD, ie. Sloppy's current defaults
    - Don't read the game log when writing to game.pgn. Instead backtrack to the
      beginning and play the moves again.
    - Add (static) function prototypes at the beginning of each source file.
    - Don't try a null move if the side to move doesn't have any sliders.
      UPDATE: this took away a lot of strength, can't use it.
    - In Linux/Unix the default data folder is ~/.sloppy/ and in Windows it's
      the same folder where the executable is. User should also be able to
      choose the folder by running sloppy with -f <path> parameter.
    - Always completing the search iteration after the first root move takes
      about 5 - 8 % of the total search time, which is 5.3 - 8.7 % of the
      allocated time. So cut about 7 % out of the allocated time.
    - Use Verified Null-Move Pruning
    - Extend 3 plies when the search transitions into a pawn endgame
    - Generate (QUEEN) promotions in gen_qs_moves()
    - Try storing 2 best moves in transposition table
    - Order losing captures last
    - Store 2 positions in each hash entry: always replace and depth preferred
    - Try late move reductions in pv nodes


   TODO:

    - Don't search the book if we're past X plies
    - Don't respond to ping while searching, unless in analyze/pondering mode
    - Support "avoid move" test positions
    - Implement pondering mode
    - If there's only one book move available and it has a score of 0, try to
      avoid choosing that move in search.
    - Use some more advanced source control system
    - Try the hash move boldly without any validation (move generation).
    - King safety extensions: extend all moves that significantly increase
      pressure against the enemy king.
    - Write a is_stalemate() function that can be called in sq_search() when
      gen_qs_moves() creates 0 moves.
    - Dismiss moves with a bad SEE value already in gen_qs_moves()
    - Carefully test all the commands in input.c and xboard.c
    - En passant captures should be handled in a cleaner way in move generation.
      The use of ep_sq should be consistent. In <Board> it's the <to> square,
      and in <Move> it's the square of the enemy pawn.
    - Optimize or get rid of get_threat_mask(), at least for generating legal
      king moves.
    - Search all captures in QS (ignore SEE) if a double check or
      discovered check is possible.
    - Add support for the UCI protocol
    - Special evals for endgames like KBNK
    - Detect blockades in eval
    - Get pointer arguments as constants if they're for reading only
    - In make_move(): if the move captures a rook, make sure the opponent
      loses castling rights in that corner. It's very unlikely that the other
      rook would later move there without the king moving first, but
      it's possible.  */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sloppy.h"
#include "chess.h"
#include "debug.h"
#include "util.h"
#include "game.h"
#include "movegen.h"
#include "avltree.h"
#include "book.h"
#include "eval.h"
#include "hash.h"
#include "egbb.h"

#define CONFIG_FILE "sloppy.conf"
#define BOOK_FILE "book.bin"


static void
set_config_option(const char *opt_name, const char *opt_val)
{
	if (strcmp(opt_name, "hash") == 0) {
		int hsize = atoi(opt_val);
		if (hsize > 0)
			settings.hash_size = (hsize * 0x100000) / sizeof(Hash);
		else
			my_error("config: invalid hash size: %s", opt_val);
	} else if (strcmp(opt_name, "egbb_5men") == 0) {
		if (strcmp(opt_val, "on") == 0)
			settings.egbb_max_men = 5;
		else if (strcmp(opt_val, "off") == 0)
			settings.egbb_max_men = 4;
		else
			my_error("config: invalid egbb_5men type: %s", opt_val);
	} else if (strcmp(opt_name, "egbb_load_type") == 0) {
		if (strcmp(opt_val, "4men") == 0)
			settings.egbb_load_type = LOAD_4MEN;
		else if (strcmp(opt_val, "5men") == 0)
			settings.egbb_load_type = LOAD_5MEN;
		else if (strcmp(opt_val, "smart") == 0)
			settings.egbb_load_type = SMART_LOAD;
		else if (strcmp(opt_val, "none") == 0)
			settings.egbb_load_type = LOAD_NONE;
		else if (strcmp(opt_val, "off") == 0)
			settings.egbb_load_type = EGBB_OFF;
		else
			my_error("config: invalid egbb load type: %s", opt_val);
	} else if (strcmp(opt_name, "egbb_cache") == 0) {
		int egbb_size = atoi(opt_val);
		if (egbb_size > 0)
			settings.egbb_cache_size = egbb_size * 0x100000;
		else
			my_error("config: invalid egbb size: %s", opt_val);
	} else if (strcmp(opt_name, "bookmode") == 0) {
		if (strcmp(opt_val, "off") == 0)
			settings.book_type = BOOK_OFF;
		else if (strcmp(opt_val, "mem") == 0)
			settings.book_type = BOOK_MEM;
		else if (strcmp(opt_val, "disk") == 0)
			settings.book_type = BOOK_DISK;
		else
			my_error("config: invalid book mode: %s", opt_val);
	} else if (strcmp(opt_name, "egbb_path") == 0) {
		int len = strlen(opt_val);
		if (len > 0) {
			strlcpy(settings.egbb_path, opt_val, MAX_BUF);
			if (opt_val[len - 1] != '/')
				strlcat(settings.egbb_path, "/", MAX_BUF);
		}
	} else if (strcmp(opt_name, "learn") == 0) {
		if (strcmp(opt_val, "on") == 0)
			settings.use_learning = true;
		else if (strcmp(opt_val, "off") == 0)
			settings.use_learning = false;
		else
			my_error("config: invalid learning mode: %s", opt_val);
	} else if (strcmp(opt_name, "logfile") == 0) {
		if (strcmp(opt_val, "on") == 0)
			settings.use_log = true;
		else if (strcmp(opt_val, "off") == 0)
			settings.use_log = false;
		else
			my_error("config: invalid logfile mode: %s", opt_val);
	} else if (strcmp(opt_name, "threads") == 0) {
		int nthreads = atoi(opt_val);
		if (nthreads > 0)
			settings.nthreads = nthreads;
		else
			my_error("config: invalid thread count: %s", opt_val);
	} else
		my_error("config: invalid option: %s", opt_name);
}

static void
parse_config_file(const char *filename)
{
	int c;
	int len = 0;
	char opt_name[MAX_BUF];
	char opt_val[MAX_BUF];
	char *strptr;
	bool in_quotes = false;
	FILE *fp;

	if ((fp = fopen(filename, "r")) == NULL) {
		my_perror("Can't open file %s", filename);
		return;
	}

	strptr = opt_name;
	while ((c = fgetc(fp)) != EOF) {
		if (!in_quotes) {
			if (c == '#') { /* comment line */
				clear_buf(fp);
				continue;
			} else if (c == ' ' || c == '\t' || c == '\r') {
				continue;
			} else if (c == '=') {
				if (len > 0)
					*strptr = '\0';
				else {
					my_error("Error in config file");
					my_close(fp, filename);
					return;
				}
				len = 0;
				strptr = opt_val;
				continue;
			}
		}
		if (c == '\"') {
			in_quotes = !in_quotes;
		} else if (c == '\n') { /* separate options by line breaks */
			in_quotes = false;
			if (len > 0) {
				*strptr = '\0';
				set_config_option(opt_name, opt_val);
			}
			len = 0;
			strptr = opt_name;
		} else {
			if (len >= MAX_BUF)
				break;
			len++;
			if (len < MAX_BUF)
				*strptr++ = c;
			else {
				*strptr = '\0';
				my_error("Config string too long");
			}
		}
	}

	my_close(fp, filename);
}

/* Initialize everything.  */
static void
initialize(Chess *chess)
{
	unsigned long hsize;
	char *env;

	printf("%s %s by Ilari Pihlajisto\n\n", APP_NAME, APP_VERSION);
#ifdef GIT_REV
	if (strlen(GIT_REV) > 0)
		printf("Git revision: %s\n", GIT_REV);
#endif /* GIT_VERSION */
	printf("Build date: %s\n", __DATE__);
	printf("Debugging level: %d\n", DEBUG_LEVEL);
#if defined(__LP64__) || defined(__powerpc64__) || defined(_WIN64)
	printf("Optimized for 64-bit\n");
#else /* not 64-bit */
	printf("Optimized for 32-bit\n");
#endif /* not 64-bit */
	printf("\nInitializing...\n");

	init_chess(chess);
	chess->increment = 2000;

	init_endian();
	init_movegen();
	init_eval();
	init_hash();

	if (settings.nthreads < 1) {
		int nproc = get_nproc();
		if (nproc > 0) {
			printf("Found %d CPUs\n", nproc);
			settings.nthreads = nproc;
		} else {
			my_error("Can't detect CPU count, assuming 1\n");
			settings.nthreads = 1;
		}
	} else
		printf("Using %d threads (for perft)\n", settings.nthreads);

#ifdef WINDOWS
	strlcpy(settings.book_file, BOOK_FILE, MAX_BUF);
#else /* not WINDOWS */
	if ((env = getenv("XDG_DATA_HOME"))) {
		strlcpy(settings.book_file, env, MAX_BUF);
		strlcat(settings.book_file, "/sloppy/", MAX_BUF);
		strlcat(settings.book_file, BOOK_FILE, MAX_BUF);
	} else if ((env = getenv("HOME"))) {
		strlcpy(settings.book_file, env, MAX_BUF);
		strlcat(settings.book_file, "/.local/share/sloppy/", MAX_BUF);
		strlcat(settings.book_file, BOOK_FILE, MAX_BUF);
	}
	if (!env
	||  (!file_exists(settings.book_file) && file_exists(BOOK_FILE)))
		strlcpy(settings.book_file, BOOK_FILE, MAX_BUF);
#endif /* not WINDOWS */

	switch (settings.book_type) {
	case BOOK_MEM:
		printf("Using \"book in memory\" book mode\n");
		if (file_exists(settings.book_file)) {
			printf("Loading opening book to memory...\n");
			book_to_tree(settings.book_file, &chess->book);
		} else
			printf("No opening book was found\n");
		break;
	case BOOK_DISK:
		printf("Using \"book on disk\" book mode\n");
		if (!file_exists(settings.book_file)) {
			printf("No opening book was found\n");
			settings.book_type = BOOK_OFF;
		}
		break;
	case BOOK_OFF:
		printf("Opening book is disabled\n");
		break;
	}
	if (settings.use_learning && settings.book_type != BOOK_MEM) {
		my_error("Can't use learning in this book mode");
		settings.use_learning = false;
	}

	if (settings.use_learning)
		printf("Book learning ON\n");
	else
		printf("Book learning OFF\n");

	if (settings.egbb_load_type != EGBB_OFF
	&&  load_bitbases(settings.egbb_path, settings.egbb_cache_size,
	                  settings.egbb_load_type)) {
		if (settings.egbb_max_men >= 5)
			printf("5-men egbbs enabled (if available)\n");
		else
			printf("5-men egbbs disabled\n");
		switch (settings.egbb_load_type) {
		case LOAD_4MEN:
			printf("Egbb load type: 4-men\n");
			break;
		case LOAD_5MEN:
			printf("Egbb load type: 5-men\n");
			break;
		case SMART_LOAD:
			printf("Egbb load type: smart\n");
			break;
		case LOAD_NONE:
			printf("Egbb load type: none\n");
			break;
		default:
			my_error("Invalid egbb load type");
			break;
		}
	} else
		printf("Endgame bitbases disabled\n");

	hsize = (sizeof(Hash) * settings.hash_size) / 0x100000;
	printf("Hash table size: %lu MB\n", hsize);

	printf("...Done\n\n");
	printf("Type \"help\" to display a list of commands\n");
}

int
main(void)
{
	Chess chess;

#ifdef WINDOWS
	parse_config_file(CONFIG_FILE);
#else /* not WINDOWS */

	char home_config[MAX_BUF];
	char *env;
	if ((env = getenv("XDG_CONFIG_HOME"))) {
		strlcpy(home_config, env, MAX_BUF);
		strlcat(home_config, "/sloppy/", MAX_BUF);
		strlcat(home_config, CONFIG_FILE, MAX_BUF);
	} else if ((env = getenv("HOME"))) {
		strlcpy(home_config, env, MAX_BUF);
		strlcat(home_config, "/.config/sloppy/", MAX_BUF);
		strlcat(home_config, CONFIG_FILE, MAX_BUF);
	}
	
	if (env && (file_exists(home_config) || !file_exists(CONFIG_FILE)))
		parse_config_file(home_config);
	else
		parse_config_file(CONFIG_FILE);
#endif /* not WINDOWS */

	setbuf(stdout, NULL);
	log_date("Sloppy started at ");
	initialize(&chess);

	new_game(&chess, START_FEN, BLACK);
	main_loop(&chess);

	if (settings.book_type == BOOK_MEM)
		write_book(settings.book_file, chess.book);
	clear_avl(chess.book);
	unload_bitbases();
	destroy_hash();
	destroy_pawn_hash();
	log_date("Sloppy exited at ");

	return EXIT_SUCCESS;
}

