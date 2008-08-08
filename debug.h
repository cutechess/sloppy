#ifndef DEBUG_H
#define DEBUG_H

#include "sloppy.h"


#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 1
#endif /* DEBUG_LEVEL */


#if DEBUG_LEVEL > 0

/* Sloppy's assert macro, which unlike assert(), takes a debug level arg.  */
#undef ASSERT
#define ASSERT(level, exp) \
  if ((level) <= DEBUG_LEVEL && !(exp)) \
    fatal_error("%s:%u: Assertion '" #exp "' failed.", __FILE__, __LINE__);

/* Returns true if <val> is a valid score or evaluation.  */
extern bool val_is_ok(int val);

/* Returns true if <board> isn't corrupted.  */
extern bool board_is_ok(Board *board);

/* Compares two chess boards to each other.
   Returns 0 if they have the same data.  */
extern int board_cmp(Board *board1, Board *board2);

/* Print a 64-bit unsigned integer in binary (bitmask) form.  */
extern void print_bitmask_64(U64 mask);

/* Print an 8-bit unsigned integer in binary (bitmask) form.  */
extern void print_bitmask_8(U8 mask);

#else /* not DEBUG_LEVEL > 0 */

#undef ASSERT
#define ASSERT(level, exp)

#endif /* not DEBUG_LEVEL > 0 */

/* Print detailed information about a move.  */
extern void print_move_details(U32 move);

/* Print a list of legal moves.  */
extern void print_moves(Board *board, bool san_notation);

/* Test the SEE to make sure it works correctly.  */
extern void test_see(const char *fen, const char *san_move);

#endif /* DEBUG_H */

