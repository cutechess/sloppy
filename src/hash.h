#ifndef HASH_H
#define HASH_H

#include "sloppy.h"


/* The type of a hash node.  */
typedef enum _Hashf
{
	H_NONE,		/* no hash entry */
	H_EXACT,	/* exact value (alpha < value < beta) */
	H_ALPHA,	/* fail low (value <= alpha) */
	H_BETA		/* fail high (value >= beta) */
} Hashf;

/* Random 64-bit values for generating hash keys.  */
typedef struct _Zobrist
{
	U64 color;
	U64 enpassant[64];
	U64 castle[2][2];
	U64 pc[2][8][64];
} Zobrist;

/* If we're not using GNU C, elide __attribute__  */
#ifndef __GNUC__
#define  __attribute__(x)
//#   pragma pack(1)
#endif /* not __GNUC__ */

/* Data structure for a hash table entry.  */
typedef struct _Hash
{
	S8 depth;
	S16 priority;	/* replace treshold */
	S8 flag;	/* hash entry type */
	S16 val;
	U32 best;
	U64 key;
} __attribute__ ((__packed__)) Hash;


/* Random values for everything that's needed in a hash key (side to move,
   enpassant square, castling rights, and piece positions).  */
extern Zobrist zobrist;


/* Set a new hash table size (in megabytes),
   and deallocate the old table (if any).  */
extern void set_hash_size(int hsize);

/* Initialize the hash table.  */
extern void init_hash(void);

/* Initialize the zobrist values.  */
extern void init_zobrist(void);

/* Free the memory allocated for the hash table.  */
extern void destroy_hash(void);

/* Convert a value from the hash table into a value that
   can be used in the search.  */
extern int val_from_hash(int val, int ply);

/* Convert a search value into a hash value.  */
extern int val_to_hash(int val, int ply);

/* Get the best move from the hash table.
   If not successfull, return NULLMOVE.  */
extern U32 get_hash_move(U64 key);

/* Probe the hash table for a score and the best move.
   If not successfull, return VAL_NONE.  */
extern int probe_hash(int depth, int alpha, int beta, U64 key, U32 *best_move, int ply);

/* Store a hash key and its score and best move in the hash table.  */
extern void store_hash(int depth, int val, Hashf flag, U64 key, U32 best_move, int root_ply);

/* Generate the hash key for a board position.  */
extern void comp_hash_key(Board *board);

#endif /* HASH_H */

