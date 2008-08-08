#ifndef AVLTREE_H
#define AVLTREE_H

#include <stdio.h>
#include "sloppy.h"


typedef struct _AvlNode
{
	U64 key;		/* hash key */
	U16 games;		/* number of times the pos was reached */
	U16 wins;		/* number of times the pos led to a win */
	struct _AvlNode *left;
	struct _AvlNode *right;
	int height;		/* height of the node's subtree */
} AvlNode;


/* Write a node and its subtree to a file.  */
extern void write_avl(const AvlNode *node, FILE *fp);

/* Unallocate a node and its subtree.  */
extern void clear_avl(AvlNode *node);

/* Search a tree (node, and its subtree) for a node of a specific key (key).
   If no match is found, return NULL.  */
extern AvlNode *find_avl(const AvlNode *node, U64 key);

/* Insert a new node into an AVL tree.  */
extern AvlNode *insert_avl(AvlNode *node, U64 key, U16 games, U16 wins);

#endif /* AVLTREE_H */

