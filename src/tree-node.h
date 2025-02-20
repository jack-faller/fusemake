#ifndef TREE_NODE_H
#define TREE_NODE_H
#include <stdbool.h>

#define NODE_NULL (~(unsigned)0)
typedef unsigned string_ref;
typedef unsigned node_ref;

// If the node has a value, its prefix string is the full string for the node.
// If the node has no value, the string is shared with some other node and
// prefix length gives the length.
typedef struct Tree_Node {
	string_ref path;
  // Where the prefix starts in the path.
	unsigned prefix_start : 31;
	bool has_value : 1;
	union {
		unsigned value, prefix_length;
	};
	node_ref child_nodes[16];
} Tree_Node;

#endif
