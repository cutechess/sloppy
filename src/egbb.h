#ifndef EGBB_H
#define EGBB_H

#include "sloppy.h"

/* Load the endgame bitbase library.
   Returns true if successfull.  */
extern bool load_bitbases(void);

/* Unload the endgame bitbase object/library.  */
extern void unload_bitbases(void);

/* Probe the bitbases for a result.
   Returns VAL_NONE if the position is not found.  */
extern int probe_bitbases(const Board *board, int ply, int depth);

#endif /* EGBB_H */
