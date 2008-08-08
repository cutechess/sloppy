#ifndef NOTATION_H
#define NOTATION_H

#include "sloppy.h"


/* Convert a piece in int format to a character (eg. 'N' for knight).  */
extern char get_pc_type_chr(int pc_type_int);

/* Returns true if <word> is a move string (in coordinate notation).
   It doesn't have to be a legal or even a pseudo-legal move though.  */
extern bool is_move_str(const char *word);

/* Convert a move string (in coordinate notation) into a move.  */
extern U32 str_to_move(Board *board, const char *str_move);

/* Convert a move into a move string (in coordinate notation).  */
extern void move_to_str(U32 move, char *str_move);

/* Convert a move string (in SAN notation) into a move.  */
extern U32 san_to_move(Board *board, const char *str_move);

/* Convert a move into a move string (in SAN notation).  */
extern void move_to_san(char *str_move, Board *board, U32 move);

/* Convert a board string (in FEN notation) into a board.  */
extern int fen_to_board(Board *board, const char *fen);

/* Convert a board into a board string (in FEN notation).  */
extern void board_to_fen(Board *board, char *fen);

#endif /* NOTATION_H */

