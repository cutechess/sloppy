#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "sloppy.h"


/* The maximum number of legal moves per position.  */
#define MAX_NMOVES 128

/* Pre-calculated move bitmasks.  */
typedef struct _MoveMasks
{
    U64 knight[64];
    U64 king[64];
    U64 pawn_capt[2][64];
} MoveMasks;

/* A struct for storing all legal moves in any chess position.  */
typedef struct _MoveLst
{
    U32 move[MAX_NMOVES];	/* list of moves */
    int score[MAX_NMOVES];	/* move scores, used for move ordering */
    int nmoves;			/* num. of moves in the list */
} MoveLst;

extern MoveMasks move_masks;
extern U64 rook_xray[64];
extern U64 bishop_xray[64];
extern const U64 seventh_rank[2];

/* Initialize the move generators.  */
extern void init_movegen(void);

/* Returns true if the side to move is in check.  */
extern bool board_is_check(Board *board);

/* Form a simple (incomplete) move from the piece type, from and to
   squares, and the promotion piece.  */
extern U32 simple_move(int pc, int from, int to, int prom);

/* Generate moves that are tried in the quiescence search.  */
extern void gen_qs_moves(Board *board, MoveLst *move_list);

/* Generate all legal moves.  */
extern void gen_moves(Board *board, MoveLst *move_list);

/* Generate moves for a specific piece type with a specific <to> square.  */
extern void gen_pc_moves(Board *board, MoveLst *move_list, int pc, int to);

#endif /* MOVEGEN_H */

