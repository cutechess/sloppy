#ifndef CHESS_H
#define CHESS_H

#include "sloppy.h"
#include "avltree.h"

/* Absolute maximum search depth. The user/protocol can also set another max.
   depth, which can't of course be greater than this limit.  */
#define MAX_PLY 128


typedef enum _Protocol
{
	PROTO_NONE,		/* Sloppy's own protocol */
	PROTO_XBOARD,
	PROTO_ERROR
} Protocol;

typedef enum _CmdType
{
	CMDT_CONTINUE,		/* continue search, handle input later */
	CMDT_EXEC_AND_CONTINUE,	/* handle input, continue search */
	CMDT_FINISH,		/* stop search, play best move, handle input */
	CMDT_CANCEL,		/* cancel search, handle input */
	CMDT_NONE
} CmdType;

/* A struct for the principal variation (the expected sequence of moves).  */
typedef struct _PvLine
{
	int nmoves;
	U32 moves[MAX_PLY];
} PvLine;

/* Variables that are needed in the search, and often also after the search.  */
typedef struct _SearchData
{
	bool stop_search;	/* time's up or search was cancelled */
	CmdType cmd_type;	/* type of pending command (if any) */
	int ply;		/* how deep are we in the search tree? */
	int nmoves;		/* num. of root moves */
	int nmoves_left;	/* num. of root moves not yet searched */
	int root_ply;		/* num. of moves played before the search */
	U64 nnodes;		/* num. of main nodes searched */
	U64 nqs_nodes;		/* num. of quiescence nodes searched */
	U64 nhash_hits;		/* num. of hash hits */
	U64 nhash_probes;	/* num. of hash probes */
	S64 t_start;		/* time at the beginning of search */
	S64 deadline;		/* flexible deadline for the search */
	S64 strict_deadline;	/* strict deadline for the search */
	double bfactor;		/* branching factor */
	char san_move[MAX_BUF];	/* root move being searched, in SAN format */
	PvLine pv;		/* principal variation */
	U32 move;		/* best root move, assigned after search */
} SearchData;

/* Almost all the data of a chess game.  */
typedef struct _Chess
{
	Board board;		/* board for the game Sloppy is playing */
	Board sboard;		/* board for the search */
	SearchData sd;		/* search statistics */
	AvlNode *book;		/* opening book */
	Protocol protocol;	/* chess protocol */
	int cpu_color;		/* Sloppy's side (WHITE or BLACK) */
	int max_depth;		/* maximum search depth */
	int max_time;		/* total time (ms) per time control */
	S64 tc_end;		/* timestamp for when time per tc is up */
	int increment;		/* time increment (ms) for each move */
	int nmoves_per_tc;	/* no. of moves per time control */
	char op_name[MAX_BUF];	/* Sloppy's opponent's name */
	bool in_book;		/* Sloppy's last move was from the book */
	bool debug;		/* we're in debug mode */
	bool game_over;		/* game is over, no more moves are accepted */
	bool show_pv;		/* show pv after each search iteration */
	bool analyze;		/* we're in analyze mode */
} Chess;


extern void init_search_data(SearchData *sd);

extern void init_chess(Chess *chess);

/* Print some details about the last search.  */
extern void print_search_data(SearchData *sd, int t_elapsed);

#endif /* CHESS_H */

