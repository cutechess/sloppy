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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


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
			set_hash_size(hsize);
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
#ifndef WINDOWS
	char *env;
#endif /* WINDOWS */

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
	init_zobrist();
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
	&&  strlen(settings.egbb_path) > 0
	&&  load_bitbases()) {
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

