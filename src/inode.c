#include "header.h"

#include <stdlib.h>
#include <string.h>

struct Inode {
	// Canonical path in project root (allocated).
	char *path;
	// Reference to the file name portion of path.
	const char *name;
	// References to other inodes in this directory (INO_IDs of files).
	// 0 to indicate absence (no hard links exist to the rood directory).
	int parent, next, prev, child;
	// 0 if closed.
	int fd;
	bool opened;
};
typedef struct ProcessRoot {
	Inode *list;
	const char *target;
	// Lookup is the count of active lookups from the kernel.
	// Presses shouldn't be replaced until this drops to zero.
	// Generation is the next generation to be used for that process.
	int length, capacity, generation, lookup;
	bool active, blocked;
} ProcessRoot;

ProcessRoot roots[PROCESS_MAX];
int processes_len, active_processes;
ProcessRoot roots[PROCESS_MAX];

const char *inode_path(Inode *i) { return i->path; }
const char *inode_name(Inode *i) { return i->name; }
int ino_generation(Ino ino) { return roots[ino.process].generation - 1; }
bool root_active(int root) { return roots[root].active; }

Ino fuse2ino(fuse_ino_t ino) {
	return (Ino) { .process = INO_PROCESS(ino), .id = INO_ID(ino) };
}
fuse_ino_t ino2fuse(Ino ino) { return INO(ino.process, ino.id); }
void ino_ref(Ino ino, int count) { roots[ino.process].lookup += count; }

Inode *inode(Ino ino) { return &roots[ino.process].list[ino.id]; }

Ino ino_parent(Ino child) {
	child.id = inode(child)->parent;
	return child;
}

Ino add_root(const char *target) {
	const int initial_cap = 16;
	int root;
	for (root = 0; root < processes_len; ++root) {
		if (!roots[root].active && roots[root].lookup == 0)
			goto found;
	}
	++processes_len;
found:
	roots[root] = (ProcessRoot) {
		.list = malloc(sizeof(Inode) * initial_cap),
		.length = 1,
		.capacity = initial_cap,
		.active = true,
		.generation = roots[root].generation + 1,
		.target = target,
	};
	Inode *out = &roots[root].list[0];
	*out = (Inode) {
		.path = ROOT,
		.name = ROOT_NAME,
	};
	return (Ino) { .id = 0, .process = root };
}

int count_active_roots() {
	int out = 0;
	for (int i = 0; i < processes_len; ++i) {
		out += roots[i].active;
	}
	return out;
}

static Ino add_child(Ino parent, const char *name) {
	ProcessRoot *proc = &roots[parent.process];
	int cap = proc->capacity;
	if (cap == proc->length) {
		cap += cap / 2;
		proc->list = reallocarray(proc->list, cap, sizeof(*proc->list));
		proc->capacity = cap;
	}

	int p_len = strlen(inode(parent)->path);
	int name_len = strlen(name);
	char *path = malloc(p_len + 1 + name_len + 1);
	sprintf(path, "%s/%s", inode(parent)->path, name);
	Ino out_ino = { .process = parent.process, .id = proc->length };
	Inode *out = &proc->list[proc->length++];
	*out = (Inode) {
		.name = &path[p_len + 1],
		.path = path,
		.parent = parent.id,
		.next = inode(parent)->child,
	};
	if (inode(parent)->child != 0) {
		proc->list[inode(parent)->child].prev = out_ino.id;
	}
	inode(parent)->child = out_ino.id;

	return out_ino;
}

static Ino get_child(Ino parent, const char *name, bool *found) {
	ProcessRoot *root = &roots[parent.process];
	Inode *c;
	for (int child = inode(parent)->child; child != 0; child = c->next) {
		c = &root->list[child];
		if (0 == strcmp(name, c->name)) {
			*found = true;
			return (Ino) { .id = child, .process = parent.process };
		}
	}
	*found = false;
	return (Ino) {};
}
Ino ino_child(Ino parent, const char *name) {
	bool found;
	Ino out = get_child(parent, name, &found);
	if (!found)
		out = add_child(parent, name);
	return out;
}
