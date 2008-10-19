#ifndef PGN_H
#define PGN_H

struct _AvlNode;


/* Read a PGN file (a collection of one or more games in PGN format) and store
   the positions and their win/loss ratios in an AVL tree (**tree).
   The AVL tree can later be written in an opening book file (book.bin).
   
   Returns the number of new positions added to the tree, or -1 on error.  */
extern int pgn_to_tree(const char *filename, struct _AvlNode **tree);

#endif /* PGN_H */

