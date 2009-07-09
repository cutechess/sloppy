/* Sloppy - sloppy.h
   Sloppy's core data structures, macros, and definitions.  */

#ifndef SLOPPY_H
#define SLOPPY_H

#ifndef APP_NAME
#define APP_NAME "Sloppy"
#endif /* APP_NAME */

#ifndef APP_VERSION
#define APP_VERSION "0.2.2"
#endif /* APP_VERSION */


#if defined(_WIN32) || defined(_WIN64) || defined(__NT__)
  #ifndef WINDOWS
    #define WINDOWS
  #endif
#endif /* Microsoft Windows */

#ifdef _MSC_VER

#define snprintf _snprintf
#if _MSC_VER < 1500 /* Earlier than MSVC 2008 */
  #define vsnprintf _vsnprintf
#endif

#define PRIu64 "I64u"
#define PRIx64 "I64x"
#define UINT16_MAX 0xFFFF
#define UINT32_MAX 0xFFFFFFFF
#define INT64_MAX 0x7FFFFFFFFFFFFFFF

typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

typedef int bool;
#define true  1
#define false 0

#else /* not _MSC_VER */

#include <stdbool.h>
#include <inttypes.h>

#endif /* not _MSC_VER */

/* Define fixed-size integer types.  */
typedef int8_t S8;
typedef uint8_t U8;
typedef int16_t S16;
typedef uint16_t U16;
typedef int32_t S32;
typedef uint32_t U32;
typedef int64_t S64;
typedef uint64_t U64;
#ifndef __64_BIT_INTEGER_DEFINED__
#define __64_BIT_INTEGER_DEFINED__
#endif /* not __64_BIT_INTEGER_DEFINED__ */




/* Macros.  */

#define SIGN(color) ((color) == WHITE ? 1 : -1)

#define SQ_FILE(sq) ((sq) & 7)
#define SQ_RANK(sq) ((sq) / 8)

/* Specs for Move:
   from		bits 1 - 6
   to		bits 7 - 12
   piece	bits 13 - 15
   capture	bits 16 - 18
   promotion	bits 19 -21
   castling	bits 22 - 23  */

/* Macros for extracting components from a Move.  */
#define GET_FROM(a)	(a & 0000000077)		/* "from" square */
#define GET_TO(a)	((a & 0000007700) >> 6)		/* "to" square */
#define GET_PC(a)	((a & 0000070000) >> 12)	/* moving piece */
#define GET_CAPT(a)	((a & 0000700000) >> 15)	/* captured piece */
#define GET_PROM(a)	((a & 0007000000) >> 18)	/* promotion piece */
#define GET_EPSQ(a)	((a & 0770000000) >> 21)	/* en passant square */
#define IS_CASTLING(a)	((a & 01000000000) >> 27)
#define GET_CASTLE(a)	((a & 02000000000) >> 28)	/* castling type */
#define IS_CHECK(a)	((a & 04000000000) >> 29)

/* Starting position for a chess game, in FEN format.  */
#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* Get a value with bit x set.  */
#define BIT(x) ((1) << (x))


/* Defined constants.  */

#define NULLMOVE	00		/* an empty move or a null move */
#define MOVE_ERROR	01

#define MAX_BUF 256			/* max. length of a string */
#define MAX_NMOVES_PER_GAME 1024	/* max. num. of halfmoves per game */

#define VAL_NONE -32767
#define VAL_AVOID_NULL (VAL_NONE + 1)
#define VAL_BITBASE 5000
#define VAL_INF 30000
#define VAL_MATE 30000
/* The smallest positive score that's still considered a forced mate.  */
#define VAL_LIM_MATE 29744
#define VAL_DRAW 0
/* If the score gets this bad, Sloppy will resign.  */
#define VAL_RESIGN -VAL_LIM_MATE

/* A mask of files B to H.  */
#define FILE_B_H 0xFEFEFEFEFEFEFEFE
/* A mask of files A to G.  */
#define FILE_A_G 0x7F7F7F7F7F7F7F7F

enum _Color
{
	WHITE,
	BLACK,
	COLOR_NONE,
	COLOR_ERROR
};

enum _Square
{
	A8, B8, C8, D8, E8, F8, G8, H8,
	A7, B7, C7, D7, E7, F7, G7, H7,
	A6, B6, C6, D6, E6, F6, G6, H6,
	A5, B5, C5, D5, E5, F5, G5, H5,
	A4, B4, C4, D4, E4, F4, G4, H4,
	A3, B3, C3, D3, E3, F3, G3, H3,
	A2, B2, C2, D2, E2, F2, G2, H2,
	A1, B1, C1, D1, E1, F1, G1, H1,
	SQUARE_NONE, SQUARE_ERROR
};

enum _Piece
{
	ALL,	/* all pieces, only to be used as an array index */
	PAWN,
	KNIGHT,
	BISHOP,
	ROOK,
	QUEEN,
	KING,
	BQ,	/* bishops and queens, also just an array index */
	RQ,	/* rooks and queens */
	PIECE_NONE
};


typedef struct _PosInfo
{
	unsigned castle_rights;	/* castle rights mask */
	int ep_sq;		/* enpassant square */
	int fifty;		/* num. of reversible moves played in a row */
	bool in_check;		/* if true, the side to move is in check */
	U32 move;		/* move that was played to reach this pos. */
	U64 pawn_key;		/* pawn hash key */
	U64 key;		/* hash key */
} PosInfo;

typedef struct _Board
{
    int nmoves;			/* number of half moves made */
    int color;			/* the side to move */
    int king_sq[2];		/* king's square */
    int mailbox[64];		/* board in mailbox format, side not encoded */
    int material[2];		/* amount of material on board */
    int phase;			/* phase (0 = opening, max_phase = endgame) */
    U64 all_pcs;		/* mask of all pieces on board */
    U64 pcs[2][9];		/* masks of all piece types for both sides */
    PosInfo *posp;		/* pointer to PosInfo of current pos. */
    PosInfo pos[MAX_NMOVES_PER_GAME]; /* PosInfo of each reached pos. */
} Board;

#endif /* SLOPPY_H */

