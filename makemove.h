#ifndef MAKEMOVE_H
#define MAKEMOVE_H

#include "sloppy.h"


extern void make_move(Board *board, U32 move);

extern void make_nullmove(Board *board);

extern void undo_move(Board *board);

extern void undo_nullmove(Board *board);

/* Returns the number of times the current position has been reached
   in the game.  */
extern int get_nrepeats(const Board *board, int max_repeats);

#endif /* MAKEMOVE_H */

