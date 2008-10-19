#ifndef BENCH_H
#define BENCH_H

//typedef struct _Chess Chess;


/* Run a test position (eg. from WAC, ECM or WCSAC).

   Format of pos: <FEN> <"bm" or "am"> <move>; <position id>;
   - "bm" means <move> is the best move, "am" means <move> should be avoided
   - the move is in SAN notation
   - position id can be omitted
   - example: R7/P4k2/8/8/8/8/r7/6K1 w - - bm Rg8; id "WAC.018";
   
   The time limit must be defined in <chess> before running the test.

   Returns -1 on error
            0 for unsolved test
            1 for solved test
            2 for cancelled test  */
extern int test_pos(Chess *chess, const char *pos);

/* Run a test suite like:
   - Win at Chess (WAC)
   - Winning Chess Sacrifices and Combinations (WCSAC)
   - Encyclopedia of Chess Middlegames (ECM)
   
   The tests in the suite must be in the format test_pos() uses.  */
extern void test_suite(Chess *chess, const char *filename);

/* Benchmark Sloppy's speed, branching factor, and hash table efficiency.  */
extern void bench(void);

#endif /* BENCH_H */

