#ifndef EVAL_H
#define EVAL_H

#include "sloppy.h"


/* Piece values.  */
#define VAL_PAWN 70
#define VAL_KNIGHT 325
#define VAL_BISHOP 326
#define VAL_ROOK 500
#define VAL_QUEEN 975
/* the king's value may only be used for move ordering */
#define VAL_KING 10000

extern const int max_phase;
extern const int pc_val[];	/* chess piece values */
extern const int phase_val[];	/* piece values for determining the phase */

extern void destroy_pawn_hash(void);

/* Initialize evaluation bitmasks.  */
extern void init_eval(void);

/* The Static Exchange Evaluator.
   It returns the likely outcome (or evaluation) of a piece exchange
   (or a series of them) cause by <move> (not necessarily a capture).  */
extern int see(Board *board, U32 move, int color);

/* If true, the moving piece is a passed pawn.  */
extern bool is_passer_move(Board *board, U32 move);

/* Compute the game phase.  */
extern int get_phase(Board *board);

/* Returns the static evaluation of the board.  */
extern int eval(Board *board);

#endif /* EVAL_H */

