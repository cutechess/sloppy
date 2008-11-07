#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include "sloppy.h"


/* If this is defined, Sloppy can use one or more threads (pthreads).  */
// #define USE_THREADS

typedef enum _BookType
{
	BOOK_MEM,	/* book is in memory */
	BOOK_DISK,	/* book is accessed externally */
	BOOK_OFF	/* book is disabled */
} BookType;

typedef enum _EgbbLoadType
{
	LOAD_NONE,	/* load nothing to RAM */
	LOAD_4MEN,	/* load 3-men and 4-men egbbs to RAM */
	SMART_LOAD,	/* load a smart selection of egbbs to RAM */
	LOAD_5MEN,	/* load 3-men, 4-men and 5-men egbbs to RAM */
	EGBB_OFF	/* disable egbbs */
} EgbbLoadType;

/* Sloppy's global settings.  */
typedef struct _Settings
{
	size_t hash_size;		/* hash size (num. of entries) */
	int egbb_max_men;		/* 4 or 5 */
	EgbbLoadType egbb_load_type;
	size_t egbb_cache_size;		/* egbb cache size in bytes */
	char book_file[MAX_BUF];	/* path to opening book */
	char egbb_path[MAX_BUF];	/* path to egbb folder */
	int nthreads;			/* max. num of threads to run */
	BookType book_type;
	bool use_learning;
	bool use_log;
} Settings;

/* Castle masks and squares for move generation, castle rights, etc.
   Defined and declared in util because these are used in so many places.  */
typedef struct _Castling
{
	int king_sq[2][2][2];
	int rook_sq[2][2][2];
	unsigned rights[2][2];
	unsigned all_rights[2];
} Castling;

/* Castling array indeces.  */
#define C_KSIDE 0	/* kingside castle */
#define C_QSIDE 1	/* queenside castle */
#define C_FROM 0	/* "from" square for king or rook */
#define C_TO 1		/* "to" square for king or rook */

extern char last_input[];	/* last input in stdin */
extern int ninput;		/* num. of commands in queue */
extern Settings settings;
extern const Castling castling;

/* An array of bitmasks where each mask has one bit set.  */
extern const U64 bit64[64];


/* Clears a stream until the next line break or EOF.  */
extern void clear_buf(FILE *fp);

/* A safe way to read a line from a FILE.
   <lim> is the maximum number of characters to read.
   Returns the number of characters read, or EOF if end of file is reached.  */
extern int fgetline(char *line, int lim, FILE *fp);

extern void my_error(const char format[], ...);

/* Print a custom error message and the description of the errno val.  */
extern void my_perror(const char format[], ...);

/* Print a custom error message, log the error, and terminate.  */
extern void fatal_error(const char format[], ...);

/* Print a custom error message and errno description, log the error,
   and terminate.  */
extern void fatal_perror(const char *format, ...);

/* Try to close a file, and if it can't be done, exit and print the error.  */
extern void my_close(FILE *fp, const char *filename);

/* Write a custom error message to Sloppy's log.  */
extern void update_log(const char *format, ...);

/* Write a custom message and the current time and date in the log.  */
extern void log_date(const char *format, ...);

/* Get the number of configured processors.  */
extern int get_nproc(void);

/* Make <dest> a copy of <src>.  */
extern void copy_board(Board *dest, const Board *src);

/* Display an ASCII version of the board.  */
extern void print_board(const Board *board);

/* Returns true if <sq> is a valid square index.  */
extern bool is_on_board(int sq);

/* Returns true if <val> is a mate score, which means there's a forced
   mate somewhere in the principal variation.  */
extern bool is_mate_score(int val);

/* Returns a pseudo-random integer between 1 and 2147483646.  */
extern int my_rand(void);

/* Initialize the random number generator with a new seed.  */
extern void my_srand(int new_seed);

/* Returns the time in milliseconds since Jan 01, 1970.  */
extern S64 get_ms(void);

/* Display an ASCII progressbar in position <i> with <nsteps> steps.  */
extern void progressbar(int nsteps, int i);

/* Init the endianess of the cpu architecture.  */
extern void init_endian(void);

/* Fix the endianess of an unsigned integer (16, 32, or 64 bits).
   On a little-endian platform nothing is changed.
   On a big-endian platform the bytes are swapped from big to little endian,
   or vice versa.  */
extern U16 fix_endian_u16(U16 val);
extern U32 fix_endian_u32(U32 val);
extern U64 fix_endian_u64(U64 val);

/* Locates the first (least significant) "one" bit in a bitboard.  */
extern int get_lsb(U64 b);

/* Same as get_lsb(), but also clears the first bit in *b.  */
extern int pop_lsb(U64 *b);

/* Returns the number of "one" bits in a 64-bit word.  */
extern int popcount(U64 b);

#if defined(WINDOWS) || defined(__GNUC__)
/* A replacement for strncpy().
   Uses NUL termination even when the string has to be truncated.  */
extern size_t strlcpy(char *dst, const char *src, size_t size);

/* A replacement for strncat().
   <size> is the size of <dst>, not space left.  */
extern size_t strlcat(char *dst, const char *src, size_t size);
#endif /* defined(WINDOWS) || defined(__GNUC__) */

#ifdef WINDOWS
/* A reentrant version of <strtok>.  */
char *strtok_r(char *s1, const char *s2, char **lasts);
#endif /* WINDOWS */

#endif /* UTIL_H */

