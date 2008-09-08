/* Sloppy - avltree.c
   A balanced binary tree implementation for managing Sloppy's opening book.

   Copyright (C) 2007 Ilari Pihlajisto (ilari.pihlajisto@mbnet.fi)

   Sloppy is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Sloppy is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <stdlib.h>
#include "sloppy.h"
#include "util.h"
#include "avltree.h"


/* Write a node and its subtree to a file.  */
void
write_avl(const AvlNode *node, FILE *fp)
{
	if (node != NULL) {
		/* Make sure the data is in the right endian format.  */
		U64 key = fix_endian_u64(node->key);
		U16 games = fix_endian_u16(node->games);
		U16 wins = fix_endian_u16(node->wins);
		
		write_avl(node->left, fp);
		fwrite(&key, sizeof(U64), 1, fp);
		fwrite(&games, sizeof(U16), 1, fp);
		fwrite(&wins, sizeof(U16), 1, fp);
		write_avl(node->right, fp);
	}
}

/* Unallocate a node and its subtree.  */
void
clear_avl(AvlNode *node)
{
	if (node != NULL) {
		clear_avl(node->left);
		clear_avl(node->right);
		free(node);
		node = NULL;
	}
}

/* Search a tree (node, and its subtree) for a node of a specific key (key).
   If no match is found, return NULL.  */
AvlNode
*find_avl(const AvlNode *node, U64 key)
{
	if (node == NULL)
		return NULL;
	if (key < node->key )
		return find_avl(node->left, key);
	if (key > node->key)
		return find_avl(node->right, key);
	return (AvlNode*)node;
}

static int
avl_height(const AvlNode *node)
{
	if (node == NULL)
		return -1;
	return node->height;
}

static int
max_val(int val1, int val2)
{
	return val1 > val2 ? val1 : val2;
}

/* Perform a rotate between a node (n2) and its left child.
   Update heights, then return new root.
   This function can be called only if n2 has a left child.  */
static AvlNode
*single_rotate_with_left(AvlNode *n2)
{
	AvlNode *n1;

	n1 = n2->left;
	n2->left = n1->right;
	n1->right = n2;

	n2->height = max_val(avl_height(n2->left), avl_height(n2->right)) + 1;
	n1->height = max_val(avl_height(n1->left), n2->height) + 1;

	return n1;  /* The new root node.  */
}

/* Perform a rotate between a node (n1) and its right child.
   Update heights, then return new root.
   This function can be called only if n1 has a right child.  */
static AvlNode
*single_rotate_with_right(AvlNode *n1)
{
	AvlNode *n2;

	n2 = n1->right;
	n1->right = n2->left;
	n2->left = n1;

	n1->height = max_val(avl_height(n1->left), avl_height(n1->right)) + 1;
	n2->height = max_val(avl_height(n2->right), n1->height) + 1;

	return n2;  /* The new root node.  */
}

/* Do the left-right double rotation.
   Update heights, then return new root.
   This function can be called only if n3 has a left
   child and n3's left child has a right child.  */
static AvlNode
*double_rotate_with_left(AvlNode *n3)
{
	/* Rotate between n1 and n2.  */
	n3->left = single_rotate_with_right(n3->left);

	/* Rotate between n3 and n2.  */
	return single_rotate_with_left(n3);
}

/* Do the right-left double rotation.
   Update heights, then return new root.
   This function can be called only if n1 has a right
   child and n1's right child has a left child.  */
static AvlNode
*double_rotate_with_right(AvlNode *n1)
{
	/* Rotate between n3 and n2.  */
	n1->right = single_rotate_with_left(n1->right);
	/* Rotate between n1 and n2.  */
	return single_rotate_with_right(n1);
}

/* Insert a new node into an AVL tree.  */
AvlNode
*insert_avl(AvlNode *node, U64 key, U16 games, U16 wins)
{
	if (node == NULL) {
		/* Create a new node.  */
		node = malloc(sizeof(AvlNode));
		if (node == NULL)
			fatal_error("Couldn't allocate memory");
		else {
			node->key = key;
			node->games = games;
			node->wins = wins;
			node->height = 0;
			node->left = node->right = NULL;
		}
	} else if (key < node->key) {
		node->left = insert_avl(node->left, key, games, wins);
		if (avl_height(node->left) - avl_height(node->right) == 2) {
			if (key < node->left->key)
				node = single_rotate_with_left(node);
			else
				node = double_rotate_with_left(node);
		}
	} else if (key > node->key) {
		node->right = insert_avl(node->right, key, games, wins);
		if(avl_height(node->right) - avl_height(node->left) == 2) {
			if(key > node->right->key)
				node = single_rotate_with_right(node);
			else
				node = double_rotate_with_right(node);
		}
	}

	node->height = max_val(avl_height(node->left), avl_height(node->right)) + 1;

	return node;
}

