#ifndef GAME_H
#define GAME_H

#include "sloppy.h"
#include "chess.h"


/* Make a move, update the log, display board, etc.  */
extern void update_game(Chess *chess, U32 move);

/* Analyze mode for any supported chess protocol.  */
extern void analyze_mode(Chess *chess);

extern void main_loop(Chess *chess);

/* Log a played game in PGN format.  */
extern void log_game(const char *result, const char *wname, const char *bname);

/* Updates the log file with details of the last move.  */
extern void update_game_log(Board *board, const char *str_move, int score, bool book_used);

/* Starts a new game in position <fen>.  */
extern void new_game(Chess *chess, const char *fen, int new_cpu_color);

#endif /* GAME_H */

