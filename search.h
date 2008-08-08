#ifndef SEARCH_H
#define SEARCH_H

#include "sloppy.h"
#include "chess.h"


/* Iterative deepening search.
   If test_move != NULLMOVE then it's the solution to a test position.  */
extern int id_search(Chess *chess, U32 test_move);

#endif /* SEARCH_H */

