/* Sloppy - util.c
   Miscellaneous utilities for Sloppy.

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
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "sloppy.h"
#ifdef WINDOWS
#include <windows.h>
#include <sys/timeb.h>
#else /* not WINDOWS */
#include <unistd.h>
#include <sys/time.h>
#endif /* not WINDOWS */
#include "debug.h"
#include "notation.h"
#include "util.h"


#define B64(a) ((U64)1 << (a))	/* returns a 64-bit bitmask with <a> bit set */
#define ERROR_LOG "errlog.txt"

const Castling castling = {
	{ {{E1, G1}, {E1, C1}}, {{E8, G8}, {E8, C8}} },
	{ {{H1, F1}, {A1, D1}}, {{H8, F8}, {A8, D8}} },
	{ {BIT(0U), BIT(1U)}, {BIT(2U), BIT(3U)} },
	{ BIT(0U) | BIT(1U), BIT(2U) | BIT(3U) }
};

Settings settings = {
	0x200000,	/* hash size (num. of entries) */
	4,		/* egbb_max_men */
	EGBB_OFF,	/* egbb load type */
	0x400000,	/* egbb cache size (bytes) */
	"",		/* book file */
	"",		/* egbb path */
	-1,		/* num. of threads */
	BOOK_MEM,	/* book mode */
	true,		/* book learning */
	false		/* logfile */
};

static int rand_seed = 1;	/* seed for the random number generator */

char last_input[MAX_BUF] = "";	/* last input in stdin */
int ninput = 0;			/* num. of commands in queue */

/* An array of bitmasks where each mask has one bit set.  */
const U64 bit64[64] =
{
	B64(0),  B64(1),  B64(2),  B64(3),  B64(4),  B64(5),  B64(6),  B64(7),
	B64(8),  B64(9),  B64(10), B64(11), B64(12), B64(13), B64(14), B64(15),
	B64(16), B64(17), B64(18), B64(19), B64(20), B64(21), B64(22), B64(23),
	B64(24), B64(25), B64(26), B64(27), B64(28), B64(29), B64(30), B64(31),
	B64(32), B64(33), B64(34), B64(35), B64(36), B64(37), B64(38), B64(39),
	B64(40), B64(41), B64(42), B64(43), B64(44), B64(45), B64(46), B64(47),
	B64(48), B64(49), B64(50), B64(51), B64(52), B64(53), B64(54), B64(55),
	B64(56), B64(57), B64(58), B64(59), B64(60), B64(61), B64(62), B64(63)
};

/* Clears a stream until the next line break or EOF.  */
void
clear_buf(FILE *fp)
{
	int c;
	
	ASSERT(1, fp != NULL);
	
	do {
		c = fgetc(fp);
	} while (c != EOF && c != '\n' && c != '\r');
}

/* A safe way to read a line from a stream.
   <lim> is the maximum number of characters to read.
   Returns the number of characters read, or EOF if end of file is reached.  */
int
fgetline(char *line, int lim, FILE *fp)
{
	int c;
	int i;

	ASSERT(1, fp != NULL);
	ASSERT(1, line != NULL);
	ASSERT(1, lim > 1);

	for (i = 0; i < lim - 1; i++) {
		c = fgetc(fp);
		if (c == '\n' || c == '\r') {
			line[i] = '\0';
			return i;
		} else if (c == EOF) {
			line[i] = '\0';
			return EOF;
		}
		line[i] = (char)c;
	}

	line[++i] = '\0';
	fprintf(stderr, "fgetline: Input too long (max %d characters)\n", lim);
	clear_buf(fp);

	return EOF;
}

/* Print a custom error message.  */
void
my_error(const char *format, ...)
{
	va_list ap;
	char error_msg[MAX_BUF];
	
	ASSERT(1, format != NULL);
	
	va_start(ap, format);
	vsnprintf(error_msg, MAX_BUF, format, ap);
	va_end(ap);
	
	fprintf(stderr, "%s\n", error_msg);
	update_log("%s\n", error_msg);
}

/* Print a custom error message and the description of the errno val.  */
void
my_perror(const char *format, ...)
{
	va_list ap;
	char error_msg[MAX_BUF];

	ASSERT(1, format != NULL);

	va_start(ap, format);
	vsnprintf(error_msg, MAX_BUF, format, ap);
	va_end(ap);

	my_error("%s: %s", error_msg, strerror(errno));
}

/* Print a custom error message, log the error, and terminate.  */
void
fatal_error(const char *format, ...)
{
	va_list ap;
	char error_msg[MAX_BUF];

	ASSERT(1, format != NULL);

	va_start(ap, format);
	vsnprintf(error_msg, MAX_BUF, format, ap);
	va_end(ap);

	my_error(error_msg);
	fprintf(stderr, "Aborted.\n");
	log_date("Aborted at ");

	exit(EXIT_FAILURE);
}

/* Print a custom error message and errno description, log the error,
   and terminate.  */
void
fatal_perror(const char *format, ...)
{
	va_list ap;
	char error_msg[MAX_BUF];

	ASSERT(1, format != NULL);

	va_start(ap, format);
	vsnprintf(error_msg, MAX_BUF, format, ap);
	va_end(ap);

	fatal_error("%s: %s", error_msg, strerror(errno));
}

/* Try to close a file, and if it can't be done, exit and print the error.  */
void
my_close(FILE *fp, const char *filename)
{
	ASSERT(1, fp != NULL);
	ASSERT(1, filename != NULL);

	if (fclose(fp) == EOF) {
		my_perror("Can't close file %s", filename);
		exit(EXIT_FAILURE);
	}
}

/* Write a custom error message to Sloppy's log.  */
void
update_log(const char *format, ...)
{
	FILE *fp;
	va_list ap;
	
	if (!settings.use_log)
		return;
	
	if ((fp = fopen(ERROR_LOG, "a")) == NULL) {
		my_perror("Can't open file %s", ERROR_LOG);
		return;
	}
	
	va_start(ap, format);
	vfprintf(fp, format, ap);
	va_end(ap);
	
	my_close(fp, ERROR_LOG);
}

/* Write a custom message and the current time and date in the log.  */
void
log_date(const char *format, ...)
{
	time_t td;
	struct tm *dcp;
	FILE *fp;
	va_list ap;
	char date[MAX_BUF];
	
	if (!settings.use_log)
		return;
	
	if ((fp = fopen(ERROR_LOG, "a")) == NULL) {
		my_perror("Can't open file %s", ERROR_LOG);
		return;
	}

	va_start(ap, format);
	vfprintf(fp, format, ap);
	va_end(ap);

	time(&td);
	dcp = localtime(&td);
	if (dcp != NULL)
		strftime(date, MAX_BUF, "%H:%M:%S %m/%d/%Y", dcp);
	else
		strlcpy(date, "<no date>", MAX_BUF);

	fprintf(fp, "%s\n", date);

	my_close(fp, ERROR_LOG);
}

/* Get the number of configured processors.
   Returns -1 if unsuccessfull.  */
int
get_nproc(void)
{
	#if defined(_SC_NPROCESSORS_ONLN)	/* Sun Solaris, Digital UNIX */
	  return sysconf(_SC_NPROCESSORS_ONLN);
	#elif defined(_SC_NPROC_ONLN)		/* Silicon Graphics IRIX */
	  return sysconf(_SC_NPROC_ONLN);
	#elif defined(_SC_NPROCESSORS_CONF)	/* favor _ONLN over _CONF */
	  return sysconf(_SC_NPROCESSORS_CONF);
	#elif defined(WINDOWS)			/* Microsoft Windows */
	  SYSTEM_INFO info;
	  GetSystemInfo(&info);
	  return info.dwNumberOfProcessors;
	#else
	  return -1;
	#endif
}

/* Make <dest> a copy of <src>.  */
void
copy_board(Board *dest, const Board *src)
{
	*dest = *src;
	dest->posp = &dest->pos[dest->nmoves];
}

/* Display an ASCII version of the board.  */
void
print_board(const Board *board)
{
	char c;
	char fen[MAX_BUF];
	int i;
	
	ASSERT(1, board != NULL);

	for (i = 0; i < 64; i++) {
		if (board->mailbox[i]) {
			if (board->pcs[WHITE][ALL] & bit64[i])
				c = get_pc_type_chr(board->mailbox[i]);
			else
				c = tolower(get_pc_type_chr(board->mailbox[i]));
		} else
			c = '.';
		if (!SQ_FILE(i))
			printf("\n");
		if (SQ_FILE(i + 1))
			printf("%c ", c);
		else
			printf("%c", c);
	}
	printf("\n\n");

	/* Print the position in FEN notation.  */
	board_to_fen(board, fen);
	printf("Fen: %s\n", fen);
}

/* Returns true if <sq> is a valid square.  */
bool
is_on_board(int sq)
{
	if (sq >= 0 && sq <= 63)
		return true;
	return false;
}

/* Returns true if <val> is a mate score, which means there's a forced
   mate somewhere in the principal variation.  */
bool
is_mate_score(int val)
{
	ASSERT(2, val_is_ok(val));
	return (val < -VAL_LIM_MATE || val > VAL_LIM_MATE);
}

/* The "minimal standard" random number generator by Park and Miller.
   Returns a pseudo-random integer between 1 and 2147483646.  */
int
my_rand(void)
{
	const int a = 16807;
	const int m = 2147483647;
	const int q = (m / a);
	const int r = (m % a);

	int hi = rand_seed / q;
	int lo = rand_seed % q;
	int test = a * lo - r * hi;

	if (test > 0)
		rand_seed = test;
	else
		rand_seed = test + m;

	return rand_seed;
}

/* Initialize the random number generator with a new seed.  */
void
my_srand(int new_seed)
{
	rand_seed = new_seed;
}

/* Returns the time in milliseconds since Jan 01, 1970.  */
S64
get_ms(void)
{
#ifdef WINDOWS
	struct timeb time_buf;
	ftime(&time_buf);

	return ((S64)time_buf.time * 1000) + time_buf.millitm;
#else /* not WINDOWS */
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return ((S64)tv.tv_sec * 1000) + tv.tv_usec / 1000;
#endif /* not WINDOWS */
}

/* Display an ASCII progressbar in position <i> with <nsteps> steps.  */
void
progressbar(int nsteps, int i)
{
	int j;
	int percent = (i * 100) / nsteps;

	printf("\r%3d%% [", percent);
	for (j = 0; j < nsteps; j++) {
		if (j < i)
			printf("=");
		else if (j == i)
			printf(">");
		else
			printf(" ");
	}
	printf("]");
}

/* 16-bit endian swap.  */
#define ENDIAN_SWAP_U16(val) ((U16) ( \
    (((U16) (val) & (U16) 0x00ffU) << 8) | \
    (((U16) (val) & (U16) 0xff00U) >> 8)))

/* 32-bit endian swap.  */
#define ENDIAN_SWAP_U32(val) ((U32) ( \
    (((U32) (val) & (U32) 0x000000ffU) << 24) | \
    (((U32) (val) & (U32) 0x0000ff00U) <<  8) | \
    (((U32) (val) & (U32) 0x00ff0000U) >>  8) | \
    (((U32) (val) & (U32) 0xff000000U) >> 24)))

/* 64-bit endian swap.  */
#define ENDIAN_SWAP_U64(val) ((U64) ( \
    (((U64) (val) & (U64) 0x00000000000000ff) << 56) | \
    (((U64) (val) & (U64) 0x000000000000ff00) << 40) | \
    (((U64) (val) & (U64) 0x0000000000ff0000) << 24) | \
    (((U64) (val) & (U64) 0x00000000ff000000) <<  8) | \
    (((U64) (val) & (U64) 0x000000ff00000000) >>  8) | \
    (((U64) (val) & (U64) 0x0000ff0000000000) >> 24) | \
    (((U64) (val) & (U64) 0x00ff000000000000) >> 40) | \
    (((U64) (val) & (U64) 0xff00000000000000) >> 56)))


typedef enum _Endian
{
	SL_LITTLE_ENDIAN,
	SL_BIG_ENDIAN,
	SL_NO_ENDIAN
} Endian;

static Endian endian = SL_NO_ENDIAN;

/* Init the endianess of the CPU architecture.  */
void
init_endian(void)
{
	union {
		U32 l;
		U8 c[4];
	} u;
	u.l = 0xFF000000;
	/* Big-endian architectures will have the MSB at the lowest address.  */
	if (u.c[0] == 0xFF)
		endian = SL_BIG_ENDIAN;
	else
		endian = SL_LITTLE_ENDIAN;
}

/* Fix the endianess of an unsigned integer (16, 32, or 64 bits).
   On a little-endian platform nothing is changed.
   On a big-endian platform the bytes are swapped from big to little endian,
   or vice versa.  */
U16
fix_endian_u16(U16 val)
{
	if (endian == SL_LITTLE_ENDIAN)
		return val;
	return ENDIAN_SWAP_U16(val);
}

U32
fix_endian_u32(U32 val)
{
	if (endian == SL_LITTLE_ENDIAN)
		return val;
	return ENDIAN_SWAP_U32(val);
}

U64
fix_endian_u64(U64 val)
{
	if (endian == SL_LITTLE_ENDIAN)
		return val;
	return ENDIAN_SWAP_U64(val);
}


#if defined(__GNUC__) && (defined(__LP64__) || defined(__powerpc64__))

/* Locates the first (least significant) "one" bit in a bitboard.
   Optimized for x86-64.  */
int
get_lsb(U64 b)
{
	U64 ret;
	__asm__
	(
        "bsfq %[b], %[ret]"
        :[ret] "=r" (ret)
        :[b] "mr" (b)
	);
	return (int)ret;
}

#else /* not 64-bit GNUC */

/* Locates the first (least significant) "one" bit in a bitboard.
   Optimized for x86.  */
int
get_lsb(U64 b)
{
	unsigned a;
	/* Based on code by Lasse Hansen.  */
	static const int lsb_table[32] = {
		31,  0,  9,  1, 10, 20, 13,  2,
		 7, 11, 21, 23, 17, 14,  3, 25,
		30,  8, 19, 12,  6, 22, 16, 24,
		29, 18,  5, 15, 28,  4, 27, 26
	};

	ASSERT(2, b != 0);

	a = (unsigned)b;
	if (a != 0)
		return lsb_table[((a & -(int)a) * 0xe89b2be) >> 27];
	a  =  (unsigned)(b >> 32);

	return lsb_table[((a & -(int)a) * 0xe89b2be) >> 27]  +  32;
}

#endif /* not 64-bit GNUC */


#if defined(__LP64__) || defined(__powerpc64__) || defined(_WIN64)

/* Returns the number of "one" bits in a 64-bit word.
   Optimized for x86-64.  */
int
popcount(U64 b)
{
	b = (b & 0x5555555555555555) + ((b >> 1) & 0x5555555555555555);
	b = (b & 0x3333333333333333) + ((b >> 2) & 0x3333333333333333);
	b = (b + (b >> 4)) & 0x0F0F0F0F0F0F0F0F;
	b = b + (b >> 8);
	b = b + (b >> 16);
	b = (b + (b >> 32)) & 0x0000007F;

	return (int)b;
}
#else /* not 64-bit */

/* From R. Scharnagl

   Returns the number of "one" bits in a 64-bit word.
   Endian independent form.
   Optimized for 32-bit processors.  */
int
popcount(U64 b)
{
	unsigned buf;
	unsigned acc;

	if (b == 0)
		return 0;

	buf = (unsigned)b;
	acc = buf;
	acc -= ((buf &= 0xEEEEEEEE) >> 1);
	acc -= ((buf &= 0xCCCCCCCC) >> 2);
	acc -= ((buf &= 0x88888888) >> 3);
	buf = (unsigned)(b >> 32);
	acc += buf;
	acc -= ((buf &= 0xEEEEEEEE) >> 1);
	acc -= ((buf &= 0xCCCCCCCC) >> 2);
	acc -= ((buf &= 0x88888888) >> 3);
	acc = (acc & 0x0F0F0F0F) + ((acc >> 4) & 0x0F0F0F0F);
	acc = (acc & 0xFFFF) + (acc >> 16);

	return (acc & 0xFF) + (acc >> 8);
}
#endif /* not 64-bit */

/* Same as get_lsb(), but also clears the first bit in *b.  */
int
pop_lsb(U64 *b)
{
	int lsb;

	ASSERT(2, b != NULL);
	
	lsb = get_lsb(*b);
	*b &= (*b - 1);

	return lsb;
}

#if defined(WINDOWS) || defined(__GNUC__)

/* A replacement for strncpy().
   Uses NUL termination even when the string has to be truncated.  */
size_t
strlcpy(char *dst, const char *src, size_t size)
{
	char *dstptr = dst;
	size_t tocopy = size;
	const char *srcptr = src;

	if (tocopy && --tocopy) {
		do {
			if (!(*dstptr++ = *srcptr++))
				break;
		} while (--tocopy);
	}
	if (!tocopy) {
		if (size) *dstptr = 0;
		while (*srcptr++);
	}

	return (srcptr - src - 1);
}

/* A replacement for strncat().
   <size> is the size of <dst>, not space left.  */
size_t
strlcat(char *dst, const char *src, size_t size) {
	char *dstptr = dst;
	size_t dstlen;
	size_t tocopy = size;
	const char *srcptr = src;

	while (tocopy-- && *dstptr)
		dstptr++;
	dstlen = dstptr - dst;
	if (!(tocopy = size - dstlen))
		return (dstlen + strlen(src));
	while (*srcptr) {
		if (tocopy != 1) {
			*dstptr++ = *srcptr;
			tocopy--;
		}
		srcptr++;
	}
	*dstptr = 0;

	return (dstlen + (srcptr - src));
}

#endif /* defined(WINDOWS) || defined(__GNUC__) */


#ifdef WINDOWS

/*
 * Copyright (c) 1995, 1996, 1997 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Thread-safe version of strtok().  */
char *
strtok_r(char *s1, const char *s2, char **lasts)
{
  char *ret;

  if (s1 == NULL)
    s1 = *lasts;
  while(*s1 && strchr(s2, *s1))
    ++s1;
  if(*s1 == '\0')
    return NULL;
  ret = s1;
  while(*s1 && !strchr(s2, *s1))
    ++s1;
  if(*s1)
    *s1++ = '\0';
  *lasts = s1;
  return ret;
}
#endif /* WINDOWS */

