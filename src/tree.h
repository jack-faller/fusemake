#ifndef TREE_H
#define TREE_H

#include "list-char.h"
#include "pool-Tree_Node.h"

// Expose a tree where each node can have a pair (name, value) added to its
// child set.
// Lookup is optimised through splitting node names into half-bytes (4-bit
// values) then storing a tree of these which forks wherever there is a
// difference between two nodes' paths.
// The indices for nodes in the tree will not change when the tree is updated.

typedef struct Tree {
	Pool_Tree_Node pool;
	List_char paths;
	unsigned root;
} Tree;

Tree Tree_new();
void method(free, Tree, *tree);

// Returns ~0 if value not found.
node_ref method(lookup, Tree, *tree, node_ref parent, char *child);
unsigned method(get_value, Tree, *tree, node_ref node);
// Path is of the form "a/b/c" and valid until the tree is updated.
const char *method(get_path, Tree, *tree, node_ref node);
void method(insert, Tree, *tree, node_ref parent, char *child, unsigned value);

#endif
